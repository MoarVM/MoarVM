#include "platform/memmem.h"
#include "moar.h"
#include "platform/io.h"
#include "platform/random.h"
#include "platform/time.h"
#include "platform/mmap.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(_MSC_VER)
#define snprintf _snprintf
#endif

#ifndef _WIN32
#  include <unistd.h>
#else
#  include <process.h>
#endif

#define init_mutex(loc, name) do { \
    if ((init_stat = uv_mutex_init(&loc)) < 0) { \
        fprintf(stderr, "MoarVM: Initialization of " name " mutex failed\n    %s\n", \
            uv_strerror(init_stat)); \
        exit(1); \
    } \
} while (0)
#define init_cond(loc, name) do { \
    if ((init_stat = uv_cond_init(&loc)) < 0) { \
        fprintf(stderr, "MoarVM: Initialization of " name " condition variable failed\n    %s\n", \
            uv_strerror(init_stat)); \
        exit(1); \
    } \
} while (0)

static void setup_std_handles(MVMThreadContext *tc);

static FILE *fopen_perhaps_with_pid(char *env_var, char *path, const char *mode) {
    FILE *result;
    if (strstr(path, "%d")) {
        MVMuint64 path_length = strlen(path);
        MVMuint64 found_percents = 0;
        MVMuint64 i;

        /* Let's sanitize the format string a bit. Must only have
         * a single printf-recognized directive. */
        for (i = 0; i < path_length; i++) {
            if (path[i] == '%') {
                /* %% is all right. */
                if (i + 1 < path_length && path[i + 1] == '%') {
                    i++; continue;
                }
                found_percents++;
            }
        }
        /* We expect to pass only a single argument to snprintf here;
         * just bail out if there's more than one directive. */
        if (found_percents > 1) {
            result = MVM_platform_fopen(path, mode);
        } else {
            char *fixed_path = MVM_malloc(path_length + 16);
            MVMint64 pid = MVM_proc_getpid(NULL);
            /* We make the brave assumption that
             * pids only go up to 16 characters. */
            snprintf(fixed_path, path_length + 16, path, pid);
            result = MVM_platform_fopen(fixed_path, mode);
            MVM_free(fixed_path);
        }
    } else {
        result = MVM_platform_fopen(path, mode);
    }

    if (result)
        return result;
    fprintf(stderr, "MoarVM: Failed to open file `%s` given via `%s`: %s\n",
        path, env_var, strerror(errno));
    exit(1);
}

MVM_STATIC_INLINE MVMuint64 ptr_hash_64_to_64(MVMuint64 u) {
    /* Thomas Wong's hash from
     * https://web.archive.org/web/20120211151329/http://www.concentric.net/~Ttwang/tech/inthash.htm */
    u = (~u) + (u << 21);
    u =   u  ^ (u >> 24);
    u =  (u  + (u <<  3)) + (u << 8);
    u =   u  ^ (u >> 14);
    u =  (u  + (u <<  2)) + (u << 4);
    u =   u  ^ (u >> 28);
    u =   u  + (u << 31);
    return (MVMuint64)u;
}

#ifdef MVM_THREAD_LOCAL
MVM_THREAD_LOCAL MVMThreadContext *MVM_running_threads_context;
#else
uv_key_t MVM_running_threads_context_key;

static void
make_uv_key() {
    int result = uv_key_create(&MVM_running_threads_context_key);
    if (result)
        MVM_panic(1, "uv_key_create failed with code %u", result);
}
#endif

/* Create a new instance of the VM. */
MVMInstance * MVM_vm_create_instance(void) {
    MVMInstance *instance;

    char *spesh_log, *spesh_nodelay, *spesh_disable, *spesh_inline_disable,
         *spesh_osr_disable, *spesh_limit, *spesh_blocking, *spesh_inline_log,
         *spesh_pea_disable;
    char *jit_expr_enable, *jit_disable, *jit_last_frame, *jit_last_bb;
    char *dynvar_log;
    int init_stat;

#ifndef MVM_THREAD_LOCAL
    static uv_once_t key_once = UV_ONCE_INIT;
    uv_once(&key_once, make_uv_key);
#endif

    /* Set up instance data structure. */
    instance = MVM_calloc(1, sizeof(MVMInstance));

    /* Create the main thread's ThreadContext and stash it. */
    instance->main_thread = MVM_tc_create(NULL, instance);

    instance->subscriptions.vm_startup_hrtime = uv_hrtime();
    instance->subscriptions.vm_startup_now = MVM_proc_time(instance->main_thread);

#if MVM_HASH_RANDOMIZE
    /* Get the 128-bit hashSecret */
    MVM_getrandom(instance->main_thread, instance->hashSecrets, sizeof(MVMuint64) * 2);
    /* Just in case MVM_getrandom didn't work, XOR it with some (poorly) randomized data */
    instance->hashSecrets[0] ^= ptr_hash_64_to_64((uintptr_t)instance);
    instance->hashSecrets[1] ^= MVM_proc_getpid(instance->main_thread) * MVM_platform_now();
#else
    instance->hashSecrets[0] = 0;
    instance->hashSecrets[1] = 0;
#endif
    instance->main_thread->thread_id = 1;

    /* Next thread to be created gets ID 2 (the main thread got ID 1). */
    MVM_store(&instance->next_user_thread_id, 2);

    /* Set up the permanent roots storage. */
    instance->num_permroots         = 0;
    instance->alloc_permroots       = 16;
    instance->permroots             = MVM_malloc(sizeof(MVMCollectable **) * instance->alloc_permroots);
    instance->permroot_descriptions = MVM_malloc(sizeof(char *) * instance->alloc_permroots);
    init_mutex(instance->mutex_permroots, "permanent roots");

    /* GC orchestration state. */
    init_mutex(instance->mutex_gc_orchestrate, "GC orchestration");
    init_cond(instance->cond_gc_start, "GC start");
    init_cond(instance->cond_gc_finish, "GC finish");
    init_cond(instance->cond_gc_completed, "GC completed");
    init_cond(instance->cond_gc_intrays_clearing, "GC intrays clearing");
    init_cond(instance->cond_blocked_can_continue, "GC thread unblock");

    /* Safe point free list. */
    instance->free_at_safepoint = NULL;
    init_mutex(instance->mutex_free_at_safepoint, "safepoint free list");

    /* Set up REPR registry mutex. */
    init_mutex(instance->mutex_repr_registry, "REPR registry");
    MVM_index_hash_build(instance->main_thread, &instance->repr_hash, MVM_REPR_CORE_COUNT);

    /* Set up HLL config mutex. */
    init_mutex(instance->mutex_hllconfigs, "hll configs");
    MVM_fixkey_hash_build(instance->main_thread, &instance->compiler_hll_configs, sizeof(MVMHLLConfig));
    MVM_fixkey_hash_build(instance->main_thread, &instance->compilee_hll_configs, sizeof(MVMHLLConfig));

    /* Set up DLL registry mutex. */
    init_mutex(instance->mutex_dll_registry, "DLL registry");
    MVM_fixkey_hash_build(instance->main_thread, &instance->dll_registry, sizeof(struct MVMDLLRegistry));

    /* Set up extension registry mutex. */
    init_mutex(instance->mutex_ext_registry, "extension registry");
    MVM_fixkey_hash_build(instance->main_thread, &instance->ext_registry, sizeof(struct MVMExtRegistry));

    /* Set up extension op registry mutex. */
    init_mutex(instance->mutex_extop_registry, "extension op registry");
    MVM_fixkey_hash_build(instance->main_thread, &instance->extop_registry, sizeof(MVMExtOpRegistry));

    /* Set up SC registry mutex. */
    init_mutex(instance->mutex_sc_registry, "sc registry");
    MVM_str_hash_build(instance->main_thread, &instance->sc_weakhash, sizeof(struct MVMSerializationContextWeakHashEntry), 0);

    /* Set up loaded compunits hash mutex. */
    init_mutex(instance->mutex_loaded_compunits, "loaded compunits");
    MVM_fixkey_hash_build(instance->main_thread, &instance->loaded_compunits, sizeof(MVMString *));

    /* Set up container registry. */
    MVM_fixkey_hash_build(instance->main_thread, &instance->container_registry, 0);

    /* Set up persistent object ID hash mutex. */
    init_mutex(instance->mutex_object_ids, "object ID hash");
    MVM_ptr_hash_build(instance->main_thread, &instance->object_ids);

    /* Allocate all things during following setup steps directly in gen2, as
     * they will have program lifetime. */
    MVM_gc_allocate_gen2_default_set(instance->main_thread);

    /* Set up integer constant and string cache. */
    init_mutex(instance->mutex_int_const_cache, "int constant cache");
    instance->int_const_cache = MVM_calloc(1, sizeof(MVMIntConstCache));
    instance->int_to_str_cache = MVM_calloc(MVM_INT_TO_STR_CACHE_SIZE, sizeof(MVMString *));

    /* Initialize Unicode database and NFG. */
    MVM_unicode_init(instance->main_thread);
    MVM_nfg_init(instance->main_thread);

    /* Setup arg handling. */
    MVM_args_setup_identity_map(instance->main_thread);

    /* Bootstrap 6model. It is assumed the GC will not be called during this. */
    MVM_6model_bootstrap(instance->main_thread);

    /* Set up the dispatcher registry, boot dispatchers, and syscalls. */
    MVM_disp_registry_init(instance->main_thread);
    MVM_disp_syscall_setup(instance->main_thread);

    /* Set up main thread's last_payload. */
    instance->main_thread->last_payload = instance->VMNull;

    /* Initialize event loop thread starting mutex. */
    init_mutex(instance->mutex_event_loop, "event loop thread start");

    /* Create main thread object, and also make it the start of the all threads
     * linked list. Set up the mutex to protect it. */
    instance->threads = instance->main_thread->thread_obj = (MVMThread *)
        REPR(instance->boot_types.BOOTThread)->allocate(
            instance->main_thread, STABLE(instance->boot_types.BOOTThread));
    instance->threads->body.stage = MVM_thread_stage_started;
    instance->threads->body.tc = instance->main_thread;
    instance->threads->body.native_thread_id = MVM_platform_thread_id();
    instance->threads->body.thread_id = instance->main_thread->thread_id;
    MVM_set_running_threads_context(instance->main_thread);
    init_mutex(instance->mutex_threads, "threads list");

    /* Create compiler registry */
    instance->compiler_registry = MVM_repr_alloc_init(instance->main_thread, instance->boot_types.BOOTHash);

    /* Set up compiler registr mutex. */
    init_mutex(instance->mutex_compiler_registry, "compiler registry");

    /* Create hll symbol tables */
    instance->hll_syms = MVM_repr_alloc_init(instance->main_thread, instance->boot_types.BOOTHash);

    /* Set up hll symbol tables mutex. */
    init_mutex(instance->mutex_hll_syms, "hll syms");

    /* Create callsite intern pool. */
    instance->callsite_interns = MVM_calloc(1, sizeof(MVMCallsiteInterns));
    init_mutex(instance->mutex_callsite_interns, "callsite interns");

    init_mutex(instance->mutex_property_codes_hash_setup, "unicode db lookup hashes");

    /* There's some callsites we statically use all over the place. Intern
     * them, so that spesh may end up optimizing more "internal" stuff. */
    MVM_callsite_initialize_common(instance->main_thread);

    /* Current instrumentation level starts at 1; used to trigger all frames
     * to be verified before their first run. */
    instance->instrumentation_level = 1;

    /* Spesh enable/disable and debugging configurations. */
    spesh_log = getenv("MVM_SPESH_LOG");
    if (spesh_log && spesh_log[0])
        instance->spesh_log_fh
            = fopen_perhaps_with_pid("MVM_SPESH_LOG", spesh_log, "w");
    spesh_disable = getenv("MVM_SPESH_DISABLE");
    if (!spesh_disable || !spesh_disable[0]) {
        instance->spesh_enabled = 1;
        spesh_inline_disable = getenv("MVM_SPESH_INLINE_DISABLE");
        if (!spesh_inline_disable || !spesh_inline_disable[0])
            instance->spesh_inline_enabled = 1;
        spesh_osr_disable = getenv("MVM_SPESH_OSR_DISABLE");
        if (!spesh_osr_disable || !spesh_osr_disable[0])
            instance->spesh_osr_enabled = 1;
        spesh_pea_disable = getenv("MVM_SPESH_PEA_DISABLE");
        if (!spesh_pea_disable || !spesh_pea_disable[0])
            instance->spesh_pea_enabled = 1;
    }

    init_mutex(instance->mutex_parameterization_add, "parameterization");

    /* Should we specialize without warm up delays? Used to find bugs in the
     * specializer and JIT. */
    spesh_nodelay = getenv("MVM_SPESH_NODELAY");
    if (spesh_nodelay && spesh_nodelay[0]) {
        instance->spesh_nodelay = 1;
    }

    /* Should we limit the number of specialized frames produced? (This is
     * mostly useful for building spesh bug bisect tools.) */
    spesh_limit = getenv("MVM_SPESH_LIMIT");
    if (spesh_limit && spesh_limit[0])
        instance->spesh_limit = atoi(spesh_limit);

    /* Should we enforce that a thread, when sending work to the specialzation
     * worker, block until the specialization worker is done? This is useful
     * for getting more predictable behavior when debugging. */
    spesh_blocking = getenv("MVM_SPESH_BLOCKING");
    if (spesh_blocking && spesh_blocking[0])
        instance->spesh_blocking = 1;

    /* Should we dump details of inlining? */
    spesh_inline_log = getenv("MVM_SPESH_INLINE_LOG");
    if (spesh_inline_log && spesh_inline_log[0])
        instance->spesh_inline_log = 1;

    /* JIT environment/logging setup. */
    jit_disable = getenv("MVM_JIT_DISABLE");
    if (!jit_disable || !jit_disable[0])
        instance->jit_enabled = 1;

    jit_expr_enable = getenv("MVM_JIT_EXPR_ENABLE");
    if (jit_expr_enable && strlen(jit_expr_enable) != 0)
        instance->jit_expr_enabled = 1;


    {
        char *jit_debug = getenv("MVM_JIT_DEBUG");
        if (jit_debug && jit_debug[0])
            instance->jit_debug_enabled = 1;
    }

#if linux
    {
        char *jit_perf_map = getenv("MVM_JIT_PERF_MAP");
        if (jit_perf_map && *jit_perf_map) {
            char perf_map_filename[32];
            snprintf(perf_map_filename, sizeof(perf_map_filename),
                     "/tmp/perf-%"PRIi64".map", MVM_proc_getpid(NULL));
            instance->jit_perf_map = MVM_platform_fopen(perf_map_filename, "w");
        }
    }
#endif

    {
        char *jit_dump_bytecode = getenv("MVM_JIT_DUMP_BYTECODE");
        if (jit_dump_bytecode && jit_dump_bytecode[0]) {
            char tmpdir[1024];
            size_t len = sizeof tmpdir;
            char *jit_bytecode_dir;
            uv_fs_t req;
            uv_os_tmpdir(tmpdir, &len);
            jit_bytecode_dir = MVM_malloc(len + 32);
            snprintf(jit_bytecode_dir, len+32, "%s/moar-jit.%"PRIi64,
                     tmpdir, MVM_proc_getpid(NULL));
            if (uv_fs_mkdir(NULL, &req, jit_bytecode_dir, 0755, NULL) == 0) {
                instance->jit_bytecode_dir = jit_bytecode_dir;
            } else {
                MVM_free(jit_bytecode_dir);
            }
        }
    }

    jit_last_frame = getenv("MVM_JIT_EXPR_LAST_FRAME");
    jit_last_bb    = getenv("MVM_JIT_EXPR_LAST_BB");

    /* what could possibly go wrong in integer formats? */
    instance->jit_expr_last_frame = jit_last_frame != NULL ? atoi(jit_last_frame) : -1;
    instance->jit_expr_last_bb    =    jit_last_bb != NULL ? atoi(jit_last_bb) : -1;
    instance->jit_seq_nr = 1;

    /* add JIT debugging breakpoints */
    {
        char *jit_breakpoints_str = getenv("MVM_JIT_BREAKPOINTS");
        if (jit_breakpoints_str != NULL) {
            MVM_VECTOR_INIT(instance->jit_breakpoints, 4);
        } else {
            instance->jit_breakpoints_num = 0;
            instance->jit_breakpoints     = NULL;
        }
        while (jit_breakpoints_str != NULL && *jit_breakpoints_str) {
            MVMint32 frame_nr, block_nr, nchars;
            MVMint32 result = sscanf(jit_breakpoints_str, "%d/%d%n",
                                     &frame_nr, &block_nr, &nchars);
            if (result < 2)
                break;

            MVM_VECTOR_ENSURE_SPACE(instance->jit_breakpoints, 1);
            instance->jit_breakpoints[instance->jit_breakpoints_num].frame_nr = frame_nr;
            instance->jit_breakpoints[instance->jit_breakpoints_num].block_nr = block_nr;
            instance->jit_breakpoints_num++;

            jit_breakpoints_str += nchars;
            if (*jit_breakpoints_str == ':') {
                jit_breakpoints_str++;
            }
        }
    }

    /* Spesh thread syncing. */
    init_mutex(instance->mutex_spesh_sync, "spesh sync");
    init_cond(instance->cond_spesh_sync, "spesh sync");

    /* Various kinds of debugging that can be enabled. */
    dynvar_log = getenv("MVM_DYNVAR_LOG");
    if (dynvar_log && dynvar_log[0]) {
        instance->dynvar_log_fh = fopen_perhaps_with_pid("MVM_DYNVAR_LOG", dynvar_log, "w");
        fprintf(instance->dynvar_log_fh, "+ x 0 0 0 0 0 %"PRIu64"\n", uv_hrtime());
        fflush(instance->dynvar_log_fh);
        instance->dynvar_log_lasttime = uv_hrtime();
    }
    else
        instance->dynvar_log_fh = NULL;
    instance->nfa_debug_enabled = getenv("MVM_NFA_DEB") ? 1 : 0;
    if (getenv("MVM_CROSS_THREAD_WRITE_LOG")) {
        instance->cross_thread_write_logging = 1;
        instance->cross_thread_write_logging_include_locked =
            getenv("MVM_CROSS_THREAD_WRITE_LOG_INCLUDE_LOCKED") ? 1 : 0;
        instance->instrumentation_level++;
        init_mutex(instance->mutex_cross_thread_write_logging,
            "cross thread write logging output");
    }
    else {
        instance->cross_thread_write_logging = 0;
    }

    if (getenv("MVM_COVERAGE_LOG")) {
        char *coverage_log = getenv("MVM_COVERAGE_LOG");
        instance->coverage_logging = 1;
        instance->instrumentation_level++;
        if (coverage_log[0])
            instance->coverage_log_fh = fopen_perhaps_with_pid("MVM_COVERAGE_LOG", coverage_log, "a");
        else
            instance->coverage_log_fh = stderr;

        instance->coverage_control = 0;
        if (getenv("MVM_COVERAGE_CONTROL")) {
            char *coverage_control = getenv("MVM_COVERAGE_CONTROL");
            if (coverage_control && coverage_control[0])
                instance->coverage_control = atoi(coverage_control);
        }
    }
    else {
        instance->coverage_logging = 0;
    }

    /* Create std[in/out/err]. */
    setup_std_handles(instance->main_thread);

    /* Set up the specialization worker thread and a log for the main thread. */
    MVM_spesh_worker_start(instance->main_thread);
    MVM_spesh_log_initialize_thread(instance->main_thread, 1);

    /* Back to nursery allocation, now we're set up. */
    MVM_gc_allocate_gen2_default_clear(instance->main_thread);

    init_mutex(instance->subscriptions.mutex_event_subscription, "vm event subscription mutex");

    return instance;
}

/* Set up some standard file handles. */
static void setup_std_handles(MVMThreadContext *tc) {
    tc->instance->stdin_handle  = MVM_file_get_stdstream(tc, 0);
    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&tc->instance->stdin_handle,
        "stdin handle");

    tc->instance->stdout_handle = MVM_file_get_stdstream(tc, 1);
    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&tc->instance->stdout_handle,
        "stdout handle");

    tc->instance->stderr_handle = MVM_file_get_stdstream(tc, 2);
    MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&tc->instance->stderr_handle,
        "stderr handle");
}

/* This callback is passed to the interpreter code. It takes care of making
 * the initial invocation. */
static void toplevel_initial_invoke(MVMThreadContext *tc, void *data) {
    /* Create initial frame, which sets up all of the interpreter state also. */
    MVM_frame_dispatch_zero_args(tc, ((MVMStaticFrame *)data)->body.static_code);
}

/* Run deserialization frame, if there is one. Disable specialization
 * during this time, so we don't waste time logging one-shot setup
 * code. */
static void run_deserialization_frame(MVMThreadContext *tc, MVMCompUnit *cu) {
    if (cu->body.deserialize_frame) {
        MVMint8 spesh_enabled_orig = tc->instance->spesh_enabled;
        tc->instance->spesh_enabled = 0;
        MVM_interp_run(tc, toplevel_initial_invoke, cu->body.deserialize_frame, NULL);
        tc->instance->spesh_enabled = spesh_enabled_orig;
    }
}

/* Loads bytecode from the specified file name and runs it. */
void MVM_vm_run_file(MVMInstance *instance, const char *filename) {
    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->main_thread;
    MVMCompUnit      *cu = MVM_cu_map_from_file(tc, filename, 0);

    /* The call to MVM_string_utf8_decode() may allocate, invalidating the
       location cu->body.filename */
    MVMString *const str = MVM_string_utf8_c8_decode(tc, instance->VMString, filename, strlen(filename));
    cu->body.filename = str;
    MVM_gc_write_barrier_hit(tc, (MVMCollectable *)cu);

    /* Run the deserialization frame, if any. */
    run_deserialization_frame(tc, cu);

    /* Run the entry-point frame. */
    MVM_interp_run(tc, toplevel_initial_invoke, cu->body.main_frame, NULL);
}

/* Loads bytecode from memory and runs it. */
void MVM_vm_run_bytecode(MVMInstance *instance, MVMuint8 *bytes, MVMuint32 size) {
    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->main_thread;
    MVMCompUnit      *cu = MVM_cu_from_bytes(tc, bytes, size);

    /* Run the deserialization frame, if any. */
    run_deserialization_frame(tc, cu);

    /* Run the entry-point frame. */
    MVM_interp_run(tc, toplevel_initial_invoke, cu->body.main_frame, NULL);
}

/* TODO move code to other places to not need copypastes */

/* Some constants. */
#define HEADER_SIZE                 92
#define MIN_BYTECODE_VERSION        7
#define MAX_BYTECODE_VERSION        7
#define FRAME_HEADER_SIZE           (11 * 4 + 3 * 2 + 4)
#define FRAME_HANDLER_SIZE          (4 * 4 + 2 * 2)
#define FRAME_SLV_SIZE              (2 * 2 + 2 * 4)
#define FRAME_DEBUG_NAME_SIZE       (2 + 4)
#define SCDEP_HEADER_OFFSET         12
#define EXTOP_HEADER_OFFSET         20
#define FRAME_HEADER_OFFSET         28
#define CALLSITE_HEADER_OFFSET      36
#define STRING_HEADER_OFFSET        44
#define SCDATA_HEADER_OFFSET        52
#define BYTECODE_HEADER_OFFSET      60
#define ANNOTATION_HEADER_OFFSET    68
#define HLL_NAME_HEADER_OFFSET      76
#define SPECIAL_FRAME_HEADER_OFFSET 80

/* Frame flags. */
#define FRAME_FLAG_EXIT_HANDLER     1
#define FRAME_FLAG_IS_THUNK         2
#define FRAME_FLAG_NO_INLINE        8

#define HEADER_SIZE                 (4 * 18)
#define DEP_TABLE_ENTRY_SIZE        8
#define STABLES_TABLE_ENTRY_SIZE    12
#define OBJECTS_TABLE_ENTRY_SIZE    8
#define CLOSURES_TABLE_ENTRY_SIZE   28
#define CONTEXTS_TABLE_ENTRY_SIZE   16
#define REPOS_TABLE_ENTRY_SIZE      16

/* For the packed format, for "small" values of si and idx */
#define OBJECTS_TABLE_ENTRY_SC_MASK     0x7FF
#define OBJECTS_TABLE_ENTRY_SC_IDX_MASK 0x000FFFFF
#define OBJECTS_TABLE_ENTRY_SC_MAX      0x7FE
#define OBJECTS_TABLE_ENTRY_SC_IDX_MAX  0x000FFFFF
#define OBJECTS_TABLE_ENTRY_SC_SHIFT    20
#define OBJECTS_TABLE_ENTRY_SC_OVERFLOW 0x7FF
#define OBJECTS_TABLE_ENTRY_IS_CONCRETE 0x80000000

#define PACKED_SC_IDX_MASK  0x000FFFFF
#define PACKED_SC_MAX       0xFFE
#define PACKED_SC_IDX_MAX   0x000FFFFF
#define PACKED_SC_SHIFT     20
#define PACKED_SC_OVERFLOW  ((unsigned)0xFFF)

static MVMuint32 read_uint32(MVMuint8 *src) {
#ifdef MVM_BIGENDIAN
    MVMuint32 value;
    size_t i;
    MVMuint8 *destbytes = (MVMuint8 *)&value;
    for (i = 0; i < 4; i++)
         destbytes[4 - i - 1] = src[i];
    return value;
#else
    return *((MVMuint32 *)src);
#endif
}
/* Reads an int32 from a buffer. */
static MVMint32 read_int32(const MVMuint8 *buffer, size_t offset) {
    MVMint32 value;
    memcpy(&value, buffer + offset, 4);
#ifdef MVM_BIGENDIAN
    switch_endian((char *)&value, 4);
#endif
    return value;
}


/* copies memory dependent on endianness */
static void memcpy_endian(void *dest, MVMuint8 *src, size_t size) {
#ifdef MVM_BIGENDIAN
    size_t i;
    MVMuint8 *destbytes = (MVMuint8 *)dest;
    for (i = 0; i < size; i++)
        destbytes[size - i - 1] = src[i];
#else
    memcpy(dest, src, size);
#endif
}

/* Reads an uint16 from a buffer. */
static MVMuint16 read_int16(MVMuint8 *buffer, size_t offset) {
    MVMuint16 value;
    memcpy_endian(&value, buffer + offset, 2);
    return value;
}

/* Reads an uint8 from a buffer. */
static MVMuint8 read_int8(MVMuint8 *buffer, size_t offset) {
    return buffer[offset];
}

static void hxdmp(MVMuint8 *data, MVMuint32 amount, const char *nl) {
    MVMuint32 idx = 0;
    for (idx = 0; idx < amount; idx += 16) {
        fprintf(stdout, "%06x:  ", idx);
        for (MVMuint8 subidx = 0; subidx < 16; subidx++) {
            if (idx + subidx < amount) {
                fprintf(stdout, "%02x", data[idx + subidx]);
            }
            else {
                fputs("  ", stdout);
            }
            if (subidx % 2 == 1) {
                fputs(" ", stdout);
            }
            if (subidx % 8 == 7) {
                fputs(" ", stdout);
            }
        }
        fputs(" | ", stdout);
        for (MVMuint8 subidx = 0; subidx < 16; subidx++) {
            if (idx + subidx < amount) {
                MVMuint8 bit = data[idx + subidx];
                if (bit >= ' ' && bit <= '~') {
                    fputc(bit, stdout);
                }
                else {
                    fputc('.', stdout);
                }
            }
        }
        if (idx + 16 < amount) {
            fputs(nl, stdout);
        }
    }
}

/* Describes the current reader state. */
typedef struct {
    /* General info. */
    MVMuint32 version;

    /* The string heap. */
    MVMuint8  *string_seg;
    MVMuint32  expected_strings;

    /* The SC dependencies segment. */
    MVMuint32  expected_scs;
    MVMuint8  *sc_seg;

    /* The extension ops segment. */
    MVMuint8 *extop_seg;
    MVMuint32 expected_extops;

    /* The frame segment. */
    MVMuint32  expected_frames;
    MVMuint8  *frame_seg;
    MVMuint16 *frame_outer_fixups;

    /* The callsites segment. */
    MVMuint8  *callsite_seg;
    MVMuint32  expected_callsites;

    /* The bytecode segment. */
    MVMuint32  bytecode_size;
    MVMuint8  *bytecode_seg;

    /* The annotations segment */
    MVMuint8  *annotation_seg;
    MVMuint32  annotation_size;

    /* HLL name string index */
    MVMuint32  hll_str_idx;

    /* The limit we can not read beyond. */
    MVMuint8 *read_limit;

    /* Array of frames. */
    MVMStaticFrame **frames;

    /* Special frame indexes */
    MVMuint32  mainline_frame;
    MVMuint32  main_frame;
    MVMuint32  load_frame;
    MVMuint32  deserialize_frame;

} ReaderState;

/* Cleans up reader state. */
static void cleanup_all(ReaderState *rs) {
    MVM_free(rs->frames);
    MVM_free(rs->frame_outer_fixups);
    MVM_free(rs);
}

/* Ensures we can read a certain amount of bytes without overrunning the end
 * of the stream. */
MVM_STATIC_INLINE void ensure_can_read(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs, MVMuint8 *pos, MVMuint32 size) {
    if (pos + size > rs->read_limit) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Read past end of bytecode stream");
    }
}

/* Dissects the bytecode stream and hands back a reader pointing to the
 * various parts of it. */
static ReaderState * dissect_bytecode(MVMThreadContext *tc, MVMCompUnit *cu) {
    MVMCompUnitBody *cu_body = &cu->body;
    ReaderState *rs = NULL;
    MVMuint32 version, offset, size;

    /* Sanity checks. */
    if (cu_body->data_size < HEADER_SIZE)
        MVM_exception_throw_adhoc(tc, "Bytecode stream shorter than header");
    if (memcmp(cu_body->data_start, "MOARVM\r\n", 8) != 0)
        MVM_exception_throw_adhoc(tc, "Bytecode stream corrupt (missing magic string)");
    version = read_int32(cu_body->data_start, 8);
    if (version < MIN_BYTECODE_VERSION)
        MVM_exception_throw_adhoc(tc, "Bytecode stream version too low");
    if (version > MAX_BYTECODE_VERSION)
        MVM_exception_throw_adhoc(tc, "Bytecode stream version too high");

    /* Allocate reader state. */
    rs = (ReaderState *)MVM_calloc(1, sizeof(ReaderState));
    rs->version = version;
    rs->read_limit = cu_body->data_start + cu_body->data_size;
    cu->body.bytecode_version = version;

    /* Locate SC dependencies segment. */
    offset = read_int32(cu_body->data_start, SCDEP_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Serialization contexts segment starts after end of stream");
    }
    rs->sc_seg       = cu_body->data_start + offset;
    rs->expected_scs = read_int32(cu_body->data_start, SCDEP_HEADER_OFFSET + 4);

    /* Locate extension ops segment. */
    offset = read_int32(cu_body->data_start, EXTOP_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Extension ops segment starts after end of stream");
    }
    rs->extop_seg       = cu_body->data_start + offset;
    rs->expected_extops = read_int32(cu_body->data_start, EXTOP_HEADER_OFFSET + 4);

    /* Locate frames segment. */
    offset = read_int32(cu_body->data_start, FRAME_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Frames segment starts after end of stream");
    }
    rs->frame_seg       = cu_body->data_start + offset;
    rs->expected_frames = read_int32(cu_body->data_start, FRAME_HEADER_OFFSET + 4);

    /* Locate callsites segment. */
    offset = read_int32(cu_body->data_start, CALLSITE_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Callsites segment starts after end of stream");
    }
    rs->callsite_seg       = cu_body->data_start + offset;
    rs->expected_callsites = read_int32(cu_body->data_start, CALLSITE_HEADER_OFFSET + 4);

    /* Locate strings segment. */
    offset = read_int32(cu_body->data_start, STRING_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Strings segment starts after end of stream");
    }
    rs->string_seg       = cu_body->data_start + offset;
    rs->expected_strings = read_int32(cu_body->data_start, STRING_HEADER_OFFSET + 4);

    /* Get SC data, if any. */
    offset = read_int32(cu_body->data_start, SCDATA_HEADER_OFFSET);
    size = read_int32(cu_body->data_start, SCDATA_HEADER_OFFSET + 4);
    if (offset > cu_body->data_size || offset + size > cu_body->data_size) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Serialized data segment overflows end of stream");
    }
    if (offset) {
        cu_body->serialized = cu_body->data_start + offset;
        cu_body->serialized_size = size;
    }

    /* Locate bytecode segment. */
    offset = read_int32(cu_body->data_start, BYTECODE_HEADER_OFFSET);
    size = read_int32(cu_body->data_start, BYTECODE_HEADER_OFFSET + 4);
    if (offset > cu_body->data_size || offset + size > cu_body->data_size) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Bytecode segment overflows end of stream");
    }
    rs->bytecode_seg  = cu_body->data_start + offset;
    rs->bytecode_size = size;

    /* Locate annotations segment. */
    offset = read_int32(cu_body->data_start, ANNOTATION_HEADER_OFFSET);
    size = read_int32(cu_body->data_start, ANNOTATION_HEADER_OFFSET + 4);
    if (offset > cu_body->data_size || offset + size > cu_body->data_size) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Annotation segment overflows end of stream");
    }
    rs->annotation_seg  = cu_body->data_start + offset;
    rs->annotation_size = size;

    /* Locate HLL name */
    rs->hll_str_idx = read_int32(cu_body->data_start, HLL_NAME_HEADER_OFFSET);

    /* Locate special frame indexes. Note, they are 0 for none, and the
     * index + 1 if there is one. */
    rs->mainline_frame = read_int32(cu_body->data_start, SPECIAL_FRAME_HEADER_OFFSET);

    rs->main_frame = read_int32(cu_body->data_start, SPECIAL_FRAME_HEADER_OFFSET + 4);
    rs->load_frame = read_int32(cu_body->data_start, SPECIAL_FRAME_HEADER_OFFSET + 8);
    rs->deserialize_frame = read_int32(cu_body->data_start, SPECIAL_FRAME_HEADER_OFFSET + 12);
    if (rs->mainline_frame > rs->expected_frames
            || rs->main_frame > rs->expected_frames
            || rs->load_frame > rs->expected_frames
            || rs->deserialize_frame > rs->expected_frames) {
        cleanup_all(rs);
        MVM_exception_throw_adhoc(tc, "Special frame index out of bounds");
    }

    return rs;
}

/* Loads bytecode from the specified file name and dumps it. */
void MVM_vm_dump_file(MVMInstance *instance, const char *filename) {
    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->main_thread;
    void        *block       = NULL;
    void        *handle      = NULL;
    uv_file      fd;
    MVMuint64    size;
    uv_fs_t req;

    /* Ensure the file exists, and get its size. */
    if (uv_fs_stat(NULL, &req, filename, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "While looking for '%s': %s", filename, uv_strerror(req.result));
    }

    size = req.statbuf.st_size;
    /* Map the bytecode file into memory. */
    if ((fd = uv_fs_open(NULL, &req, filename, O_RDONLY, 0, NULL)) < 0) {
        MVM_exception_throw_adhoc(tc, "While trying to open '%s': %s", filename, uv_strerror(req.result));
    }

    block = MVM_platform_map_file(fd, &handle, (size_t)size, 0);

    if (block == NULL) {
#if defined(_WIN32)
        MVM_exception_throw_adhoc(tc, "Could not map file '%s' into memory: %lu", filename, GetLastError());
#else
        MVM_exception_throw_adhoc(tc, "Could not map file '%s' into memory: %s", filename, strerror(errno));
#endif
    }

    /* Look for MOARVM magic string from the start of the file. */
    char *needle = "MOARVM\r\n";

    void *bytecode_start = MVM_memmem(block, size, (void *)needle, strlen(needle));

    if (bytecode_start == NULL) {
        MVM_exception_throw_adhoc(tc, "Could not find moarvm bytecode header anywhere in %s", filename);
    }

    size_t offset = (intptr_t)bytecode_start - (intptr_t)block;

    MVMCompUnit      *cu = MVM_cu_map_from_file_handle(tc, fd, offset);

    if (getenv("MVM_DUMP_MODE")) {
        MVMThreadContext *tc = instance->main_thread;
        tc->interp_cu = &cu;
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable**)&cu, "compunit to dump");
        MVMSerializationContext *sc = (MVMSerializationContext*)MVM_sc_create(tc, instance->str_consts.empty);
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable**)&sc, "sc of provided moar file");
        MVMObject *code_list = MVM_gc_allocate_object(tc, STABLE(instance->boot_types.BOOTArray));
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable**)&code_list, "empty list of code objects");
        MVMObject *rc_list = MVM_gc_allocate_object(tc, STABLE(instance->boot_types.BOOTArray));
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable**)&rc_list, "list for repo conflicts");
        sc->body->fake_mode = 1;
        MVM_serialization_deserialize(tc, sc, instance->VMNull, code_list, rc_list, NULL);
        MVMSerializationReader *sr = sc->body->sr;
        MVMCompUnitBody *cubody = &cu->body;
        MVMuint8 *root = cubody->data_start;

        ReaderState *rs = dissect_bytecode(tc, cu);

        fprintf(stdout, "Reader Version: %d\n", sr->root.version);
        fprintf(stdout, "Bytecode File Parts:\n");
        fprintf(stdout, "  Section               %7s  %6s\n", "offset", "length");
        fprintf(stdout, "  SC Dependencies:      %7lx  %6lx  (%d dependencies)\n",  (MVMuint8 *)rs->sc_seg - root, rs->extop_seg - rs->sc_seg, sr->root.num_dependencies);
        fprintf(stdout, "  Extension Ops:        %7lx  %6lx  (%d extops)\n",        (MVMuint8 *)rs->extop_seg - root, rs->frame_seg - rs->extop_seg, rs->expected_extops);
        fprintf(stdout, "  Frames Data:          %7lx  %6lx  (%d frames)\n",        (MVMuint8 *)rs->frame_seg - root, rs->callsite_seg - rs->frame_seg, rs->expected_frames);
        fprintf(stdout, "  Callsites:            %7lx  %6lx  (%d callsites)\n",     (MVMuint8 *)rs->callsite_seg - root, rs->string_seg - rs->callsite_seg, rs->expected_callsites);
        fprintf(stdout, "  String heap:          %7lx  %6lx  (%d strings)\n",       (MVMuint8 *)rs->string_seg - root, cubody->serialized - rs->string_seg, rs->expected_strings);
        fprintf(stdout, "  Serialization Data:   %7lx  %6lx\n",                     (MVMuint8 *)cubody->serialized - root, rs->bytecode_seg - cubody->serialized);
        fprintf(stdout, "  Bytecode:             %7lx  %6lx\n",                     (MVMuint8 *)rs->bytecode_seg - root, rs->annotation_seg - rs->bytecode_seg);
        fprintf(stdout, "  Annotations:          %7lx  %6lx\n",                     (MVMuint8 *)rs->annotation_seg - root, root + cubody->data_size - (MVMuint8*)sr->root.dependencies_table);
        fprintf(stdout, "  End of File:          %7x\n",                                        cubody->data_size);
        fprintf(stdout, "\n");
        fprintf(stdout, "Serialization Roots (starts at %lx):\n", (MVMuint8*)sr->data - root);
        fprintf(stdout, "  Section               %7s  %6s\n", "offset", "length");
        fprintf(stdout, "  Dependencies Table:   %7lx  %6lx  (%d dependencies)\n", (MVMuint8 *)sr->root.dependencies_table - root, sr->root.stables_table - sr->root.dependencies_table, sr->root.num_dependencies);
        fprintf(stdout, "  STables Table:        %7lx  %6lx  (%d stables)\n", (MVMuint8 *)sr->root.stables_table - root, sr->root.stables_data - sr->root.stables_table, sr->root.num_stables);
        fprintf(stdout, "  STable Data:          %7lx  %6lx\n", (MVMuint8 *)sr->root.stables_data - root, sr->root.objects_table - sr->root.stables_data);
        fprintf(stdout, "  Objects Table:        %7lx  %6lx  (%d objects)\n", (MVMuint8 *)sr->root.objects_table - root, sr->root.objects_data - sr->root.objects_table, sr->root.num_objects);
        fprintf(stdout, "  Objects Data:         %7lx  %6lx\n", (MVMuint8 *)sr->root.objects_data - root, sr->root.closures_table - sr->root.objects_data);
        fprintf(stdout, "  Closures Table:       %7lx  %6lx  (%d closures)\n", (MVMuint8 *)sr->root.closures_table - root, sr->root.contexts_table - sr->root.closures_table, sr->root.num_closures);
        fprintf(stdout, "  Contexts Table:       %7lx  %6lx  (%d contexts)\n", (MVMuint8 *)sr->root.contexts_table - root, sr->root.contexts_data - sr->root.contexts_table, sr->root.num_contexts);
        fprintf(stdout, "  Contexts Data:        %7lx  %6lx\n", (MVMuint8 *)sr->root.contexts_data - root, sr->root.repos_table - sr->root.contexts_data);
        fprintf(stdout, "  Repossessions Table:  %7lx  %6lx  (%d repros)\n", (MVMuint8 *)sr->root.repos_table - root, sr->root.param_interns_data - sr->root.repos_table, sr->root.num_repos);
        fprintf(stdout, "  Param Interns Table:  %7lx  %6lx  (%d interns)\n", (MVMuint8 *)sr->root.param_interns_data - root, sr->param_interns_data_end - sr->root.param_interns_data, sr->root.num_param_interns);
        fprintf(stdout, "  End of data:          %7lx\n", (MVMuint8 *)sr->param_interns_data_end - root);

        fprintf(stdout, "Start of File Data:\n  ");
        hxdmp(root, rs->sc_seg - root, "\n  "); fputs("\n", stdout);

        fprintf(stdout, "SC Dependencies:      %7lx\n  ", (MVMuint8 *)rs->sc_seg - root);
        hxdmp((MVMuint8 *)rs->sc_seg,  rs->extop_seg - rs->sc_seg, "\n  "); fputs("\n", stdout);
        fprintf(stdout, "Extension Ops:        %7lx\n  ", (MVMuint8 *)rs->extop_seg - root);
        hxdmp((MVMuint8 *)rs->extop_seg, rs->frame_seg - rs->extop_seg, "\n  ");
        fputs("\n", stdout);

        /*fprintf(stdout, "Frames Data:          %7lx\n  ", (MVMuint8 *)rs->frame_seg - root);
        hxdmp((MVMuint8 *)rs->frame_seg,  rs->callsite_seg - rs->frame_seg, "\n  ");
        fputs("\n", stdout);*/
        fprintf(stdout, "Frames (%d frames):\n", cubody->num_frames);
        fprintf(stdout, "  %5s  %5s   %7s   %6s   %4s  %4s  %4s  %4s  %5s  %5s\n",
                                        "idx", "outr", "bc pos", "bc len", "#loc", "#lex", "#ant", "#hnd", "cuuid", "name");
        char *emptystr = "";
        for (MVMuint32 fidx = 0; fidx < cubody->num_frames; fidx++) {
            MVMStaticFrameBody *f = &((MVMCode *)cubody->coderefs[fidx])->body.sf->body;
            char *name = emptystr;
            char *outercuuid = emptystr;
            if (f->name->body.num_graphs > 0) {
                name = MVM_string_utf8_c8_encode_C_string(tc, f->name);
            }
            char *cuuid = MVM_string_utf8_c8_encode_C_string(tc, f->cuuid);
            if (f->outer)
                outercuuid = MVM_string_utf8_c8_encode_C_string(tc, f->outer->body.cuuid);
            fprintf(stdout, "  %5u  %5s   %7lx   %6x   %4u  %4u  %4u  %4u  %5s  '%s'\n",
                    fidx, outercuuid, f->orig_bytecode - rs->bytecode_seg, f->bytecode_size,
                    f->num_locals, f->num_lexicals, f->num_annotations, f->num_handlers,
                    cuuid, name);
            if (name != emptystr)
                MVM_free(name);
            MVM_free(cuuid);
            if (outercuuid != emptystr)
                MVM_free(outercuuid);
        }

        fprintf(stdout, "Callsites:            %7lx\n  ", (MVMuint8 *)rs->callsite_seg - root);
        hxdmp((MVMuint8 *)rs->callsite_seg,  rs->string_seg - rs->callsite_seg, "\n  "); fputs("\n", stdout);
        fprintf(stdout, "Bytecode:             %7lx\n  ", (MVMuint8 *)rs->bytecode_seg - root);

        MVMuint8 *previous_bytecode_offset = rs->bytecode_seg;
        for (MVMuint32 fidx = 0; fidx < cubody->num_frames; fidx++) {
            MVMStaticFrameBody *f = &((MVMCode *)cubody->coderefs[fidx])->body.sf->body;
            if (previous_bytecode_offset != 0) {
                hxdmp(previous_bytecode_offset, f->orig_bytecode - previous_bytecode_offset, "\n    ");
                fputs("\n", stdout);
            }

            char *name = emptystr;
            if (f->name->body.num_graphs > 0) {
                name = MVM_string_utf8_c8_encode_C_string(tc, f->name);
            }
            char *cuuid = MVM_string_utf8_c8_encode_C_string(tc, f->cuuid);
            fprintf(stdout, "  Frame %5d at offset %6lx (cuuid: %6s) '%s'\n    ", fidx, f->orig_bytecode - rs->bytecode_seg, cuuid, name);
            if (name != emptystr)
                MVM_free(name);
            MVM_free(cuuid);

            previous_bytecode_offset = f->orig_bytecode;
        }
        hxdmp((MVMuint8 *)rs->bytecode_seg, rs->annotation_seg - rs->bytecode_seg, "\n  "); fputs("\n", stdout);
        fprintf(stdout, "Annotations:          %7lx\n  ", (MVMuint8 *)rs->annotation_seg - root);
        hxdmp((MVMuint8 *)rs->annotation_seg, cubody->data_size - (((MVMuint8*)rs->annotation_seg - root)), "\n  "); fputs("\n", stdout);


        char **strings = MVM_calloc(cu->body.num_strings, sizeof(char *));
        fprintf(stdout, "String Heap starts at %lx has %u strings in it.\n", cu->body.string_heap_start - cu->body.data_start, cu->body.num_strings);
        MVMuint8 *limit = cu->body.string_heap_read_limit;
        MVMuint32 str_idx = 0;
        for (MVMuint8 *str_pos = cu->body.string_heap_start; str_idx < cu->body.num_strings;) {
            if (str_pos + 4 < limit) {
                MVMuint32 bytes = read_uint32(str_pos) >> 1;
                fprintf(stdout, "  String % 4d at offs %6lx\n", str_idx, str_pos + 4 - cu->body.string_heap_start);
                fputs("    ", stdout);
                MVMuint64 amount = bytes + (bytes & 3 ? 4 - (bytes & 3) : 0);
                hxdmp(str_pos + 4, amount, "\n    ");
                fputc('\n', stdout);
                strings[str_idx] = MVM_calloc(amount + 1, sizeof(char));
                strncpy(strings[str_idx], (char *)(str_pos + 4), amount);
                str_pos += 4 + amount;
                str_idx++;
            }
            else {
                fprintf(stdout, "oops, ran out of the string heap ...");
            }
        }


        char **stables_reprs = MVM_calloc(sc->body->num_stables, sizeof(char *));

        fprintf(stdout, "Step Two: STables\n");
        MVMint32 previous_reprdata_offset = 0;
        for (MVMuint32 stidx = 0; stidx < sc->body->num_stables; stidx++) {
            // MVMSTable *st = MVM_serialization_demand_stable(tc, sc, stidx);
            MVMuint8 *st_table_row = (MVMuint8*)sr->root.stables_table + stidx * STABLES_TABLE_ENTRY_SIZE;
            MVMint32 repr_stridx = read_int32(st_table_row, 0);
            MVMint32 reprdata_offset = read_int32(st_table_row, 8);

            if (previous_reprdata_offset != 0) {
                /*fprintf(stdout, "... dumping from %x for %x to %x\n",
                        previous_reprdata_offset,
                        reprdata_offset - previous_reprdata_offset,
                        reprdata_offset);*/
                fputs("    ", stdout);
                hxdmp((MVMuint8 *)sr->root.stables_data + previous_reprdata_offset, reprdata_offset - previous_reprdata_offset, "\n    ");
                fputs("\n", stdout);
            }

            if (stidx < sc->body->num_param_intern_st_lookup && sc->body->param_intern_st_lookup[stidx]) {
                fprintf(stdout, "% 3d.: (ST %s +param) %lx\n", stidx, strings[repr_stridx - 1], st_table_row - root);
            }
            else {
                fprintf(stdout, "% 3d.: (ST %s) %lx\n", stidx, strings[repr_stridx - 1], st_table_row - root);
            }
            fprintf(stdout, "   reprdata at offset %lx\n", sr->root.stables_data - sr->data + reprdata_offset);
            stables_reprs[stidx] = strings[repr_stridx - 1];
            previous_reprdata_offset = reprdata_offset;
        }
        char *reprdata_offset = sr->stables_data_end;
        fputs("    ", stdout);
        hxdmp((MVMuint8 *)sr->root.stables_data + previous_reprdata_offset,  reprdata_offset - (sr->root.stables_data + previous_reprdata_offset), "\n    ");
        fputs("\n", stdout);

        fprintf(stdout, "Step Three: Objects\n");
        fputs("  ", stdout);
        hxdmp((MVMuint8 *)sr->root.objects_table, sr->root.num_objects * OBJECTS_TABLE_ENTRY_SIZE, "\n  ");
        //MVMuint8 * obj_table_row = (MVMuint8*)sr->root.objects_table + objidx * OBJECTS_TABLE_ENTRY_SIZE;
        //for (MVMint32 objidx = 0; objidx < sr->root.num_objects; objidx++) {
        //    MVMuint32 si;        /* The SC in the dependencies table, + 1 */
        //    MVMuint32 si_idx;    /* The index in that SC */
        //
        //    if (objidx % 200 == 0)
        //        fprintf(stdout, "OBJIDX Conc?  SC num  SCIDX\n");
        //
        //    MVMuint8 * obj_table_row = (MVMuint8*)sr->root.objects_table + objidx * OBJECTS_TABLE_ENTRY_SIZE;
        //    const MVMuint32 packed = read_int32(obj_table_row, 0);
        //
        //    MVMuint8 isconcrete = packed & OBJECTS_TABLE_ENTRY_IS_CONCRETE;
        //
        //    si = (packed >> OBJECTS_TABLE_ENTRY_SC_SHIFT) & OBJECTS_TABLE_ENTRY_SC_MASK;
        //    if (si == OBJECTS_TABLE_ENTRY_SC_OVERFLOW) {
        //        const char *const overflow_data
        //            = sr->root.objects_data + read_int32(obj_table_row, 4) - 8;
        //        si = read_int32(overflow_data, 0);
        //        si_idx = read_int32(overflow_data, 4);
        //    } else {
        //        si_idx = packed & OBJECTS_TABLE_ENTRY_SC_IDX_MASK;
        //    }
        //
        //    fprintf(stdout, "  %4x:  %s  %4d  %4d    %s\n", objidx, isconcrete ? "O" : "T", si, si_idx, si == 0 ? stables_reprs[si_idx] : "");
        //}

    }
    else {
        char *dump = MVM_bytecode_dump(tc, cu);
        size_t dumplen = strlen(dump);
        size_t position = 0;

        /* libuv already set up stdout to be nonblocking, but it can very well be
        * we encounter EAGAIN (Resource temporarily unavailable), so we need to
        * loop over our buffer, which can be quite big.
        *
        * The CORE.setting.moarvm has - as of writing this - about 32 megs of
        * output from dumping.
        */
        while (position < dumplen) {
            size_t written = write(1, dump + position, dumplen - position);
            if (written > 0)
                position += written;
        }

        MVM_free(dump);
    }
}

/* Exits the process as quickly as is gracefully possible, respecting that
 * foreground threads should join first. Leaves all cleanup to the OS, as it
 * will be able to do it much more swiftly than we could. This is typically
 * not the right thing for embedding; see MVM_vm_destroy_instance for that. */
void MVM_vm_exit(MVMInstance *instance) {
    /* Join any foreground threads and flush standard handles. */
    MVM_thread_join_foreground(instance->main_thread);
    MVM_io_flush_standard_handles(instance->main_thread);

    /* Close any spesh or jit log. */
    if (instance->spesh_log_fh) {
        /* Need to properly shut down spesh, otherwise we may segfault trying
         * to write to a closed file handle before we exit completely */
        MVM_spesh_worker_stop(instance->main_thread);
        MVM_spesh_worker_join(instance->main_thread);
        fclose(instance->spesh_log_fh);
    }
    if (instance->dynvar_log_fh) {
        fprintf(instance->dynvar_log_fh, "- x 0 0 0 0 %"PRId64" %"PRIu64" %"PRIu64"\n", instance->dynvar_log_lasttime, uv_hrtime(), uv_hrtime());
        fclose(instance->dynvar_log_fh);
    }

    /* And, we're done. */
    exit(0);
}

static void free_lib(MVMThreadContext *tc, void *entry_v, void *arg) {
    struct MVMDLLRegistry *entry = entry_v;
    MVM_nativecall_free_lib(entry->lib);
}

/* Destroys a VM instance. This must be called only from the main thread. It
 * should clear up all resources and free all memory; in practice, it falls
 * short of this goal at the moment. */
void MVM_vm_destroy_instance(MVMInstance *instance) {
    /* Join any foreground threads and flush standard handles. */
    MVM_thread_join_foreground(instance->main_thread);
    MVM_io_flush_standard_handles(instance->main_thread);

    /* Stop system threads */
    MVM_spesh_worker_stop(instance->main_thread);
    MVM_spesh_worker_join(instance->main_thread);
    MVM_io_eventloop_destroy(instance->main_thread);

    /* Run the normal GC one more time to actually collect the spesh thread */
    MVM_gc_enter_from_allocator(instance->main_thread);

    MVM_profile_instrumented_free_data(instance->main_thread);

    /* Run the GC global destruction phase. After this,
     * no 6model object pointers should be accessed. */
    MVM_gc_global_destruction(instance->main_thread);
    MVM_ptr_hash_demolish(instance->main_thread, &instance->object_ids);
    MVM_sc_all_scs_destroy(instance->main_thread);

    /* Clean up dispatcher registry and args identity map. */
    MVM_disp_registry_destroy(instance->main_thread);
    MVM_args_destroy_identity_map(instance->main_thread);

    /* Cleanup REPR registry */
    uv_mutex_destroy(&instance->mutex_repr_registry);
    MVM_index_hash_demolish(instance->main_thread, &instance->repr_hash);
    MVM_free(instance->repr_names);
    MVM_free(instance->repr_vtables);

    /* Clean up GC related resources. */
    uv_mutex_destroy(&instance->mutex_permroots);
    MVM_free(instance->permroots);
    MVM_free(instance->permroot_descriptions);
    uv_cond_destroy(&instance->cond_gc_start);
    uv_cond_destroy(&instance->cond_gc_finish);
    uv_cond_destroy(&instance->cond_gc_intrays_clearing);
    uv_cond_destroy(&instance->cond_blocked_can_continue);
    uv_mutex_destroy(&instance->mutex_gc_orchestrate);

    /* Clean up Hash of HLLConfig. */
    uv_mutex_destroy(&instance->mutex_hllconfigs);
    MVM_fixkey_hash_demolish(instance->main_thread, &instance->compiler_hll_configs);
    MVM_fixkey_hash_demolish(instance->main_thread, &instance->compilee_hll_configs);

    /* Clean up Hash of DLLs. */
    uv_mutex_destroy(&instance->mutex_dll_registry);
    MVM_fixkey_hash_foreach(instance->main_thread, &instance->dll_registry, free_lib, NULL);
    MVM_fixkey_hash_demolish(instance->main_thread, &instance->dll_registry);

    /* Clean up Hash of extensions. */
    uv_mutex_destroy(&instance->mutex_ext_registry);
    MVM_fixkey_hash_demolish(instance->main_thread, &instance->ext_registry);

    /* Clean up Hash of extension ops. */
    uv_mutex_destroy(&instance->mutex_extop_registry);
    MVM_fixkey_hash_demolish(instance->main_thread, &instance->extop_registry);

    /* Clean up Hash of all known serialization contexts. */
    uv_mutex_destroy(&instance->mutex_sc_registry);
    MVM_str_hash_demolish(instance->main_thread, &instance->sc_weakhash);

    /* Clean up Hash of filenames of compunits loaded from disk. */
    uv_mutex_destroy(&instance->mutex_loaded_compunits);
    MVM_fixkey_hash_demolish(instance->main_thread, &instance->loaded_compunits);

    /* Clean up Container registry. */
    MVM_fixkey_hash_demolish(instance->main_thread, &instance->container_registry);
    /* Clean up Hash of compiler objects keyed by name. */
    uv_mutex_destroy(&instance->mutex_compiler_registry);

    /* Clean up Hash of hashes of symbol tables per hll. */
    uv_mutex_destroy(&instance->mutex_hll_syms);

    /* Clean up parameterization addition mutex. */
    uv_mutex_destroy(&instance->mutex_parameterization_add);

    /* Clean up interned callsites */
    uv_mutex_destroy(&instance->mutex_callsite_interns);
    MVM_callsite_cleanup_interns(instance);

    uv_mutex_destroy(&instance->mutex_property_codes_hash_setup);

    /* Clean up syscall registry. */
    MVM_fixkey_hash_demolish(instance->main_thread, &instance->syscalls);

    /* Clean up Unicode hashes. */
    for (int i = 0; i < MVM_NUM_PROPERTY_CODES; i++) {
        MVM_uni_hash_demolish(instance->main_thread, &instance->unicode_property_values_hashes[i]);
    }
    MVM_free_null(instance->unicode_property_values_hashes);

    MVM_uni_hash_demolish(instance->main_thread, &instance->property_codes_by_names_aliases);
    MVM_uni_hash_demolish(instance->main_thread, &instance->property_codes_by_seq_names);
    MVM_uni_hash_demolish(instance->main_thread, &instance->codepoints_by_name);

    /* Clean up spesh mutexes and close any log. */
    uv_cond_destroy(&instance->cond_spesh_sync);
    uv_mutex_destroy(&instance->mutex_spesh_sync);
    if (instance->spesh_log_fh)
        fclose(instance->spesh_log_fh);
    if (instance->jit_perf_map)
        fclose(instance->jit_perf_map);
    if (instance->dynvar_log_fh)
        fclose(instance->dynvar_log_fh);
    if (instance->jit_bytecode_dir)
        MVM_free(instance->jit_bytecode_dir);
    if (instance->jit_breakpoints) {
        MVM_VECTOR_DESTROY(instance->jit_breakpoints);
    }

    /* Clean up cross-thread-write-logging mutex */
    uv_mutex_destroy(&instance->mutex_cross_thread_write_logging);

    /* Clean up NFG. */
    uv_mutex_destroy(&instance->nfg->update_mutex);
    MVM_nfg_destroy(instance->main_thread);

    /* Clean up integer constant and string cache. */
    uv_mutex_destroy(&instance->mutex_int_const_cache);
    MVM_free(instance->int_const_cache);
    MVM_free(instance->int_to_str_cache);

    /* Clean up event loop mutex. */
    uv_mutex_destroy(&instance->mutex_event_loop);

    /* Clean up safepoint free list. */
    uv_mutex_destroy(&instance->mutex_free_at_safepoint);
    MVM_alloc_safepoint(instance->main_thread);

    /* Destroy main thread contexts and thread list mutex. */
    MVM_tc_destroy(instance->main_thread);
    uv_mutex_destroy(&instance->mutex_threads);

    uv_mutex_destroy(&instance->subscriptions.mutex_event_subscription);

    /* Clear up VM instance memory. */
    MVM_free(instance);

#ifdef MVM_USE_MIMALLOC
    /* Ask mimalloc to release to the OS any already freed memory it's holding onto.
     * This probably isn't strictly necessary, but might help in case any analyzers
     * (e.g., valgrind, heaptrack) don't support mimalloc well. */
    mi_collect(true);
#endif
}

void MVM_vm_set_clargs(MVMInstance *instance, int argc, char **argv) {
    instance->num_clargs = argc;
    instance->raw_clargs = argv;
}

void MVM_vm_set_exec_name(MVMInstance *instance, const char *exec_name) {
    instance->exec_name = exec_name;
}

void MVM_vm_set_prog_name(MVMInstance *instance, const char *prog_name) {
    instance->prog_name = prog_name;
}

void MVM_vm_event_subscription_configure(MVMThreadContext *tc, MVMObject *queue, MVMObject *config) {
    MVMString *gcevent;
    MVMString *speshoverviewevent;
    MVMString *startup_time;

    MVMROOT2(tc, queue, config) {
        if (!IS_CONCRETE(config)) {
            MVM_exception_throw_adhoc(tc, "vmeventsubscribe requires a concrete configuration hash (got a %s type object)", MVM_6model_get_debug_name(tc, config));
        }

        if ((REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue && !MVM_is_null(tc, queue)) || !IS_CONCRETE(queue)) {
            MVM_exception_throw_adhoc(tc, "vmeventsubscribe requires a concrete ConcBlockingQueue (got a %s)", MVM_6model_get_debug_name(tc, queue));
        }

        uv_mutex_lock(&tc->instance->subscriptions.mutex_event_subscription);

        if (REPR(queue)->ID == MVM_REPR_ID_ConcBlockingQueue && IS_CONCRETE(queue)) {
            tc->instance->subscriptions.subscription_queue = queue;
        }

        gcevent = MVM_string_utf8_decode(tc, tc->instance->VMString, "gcevent", 7);
        MVMROOT(tc, gcevent) {
            speshoverviewevent = MVM_string_utf8_decode(tc, tc->instance->VMString, "speshoverviewevent", 18);
            MVMROOT(tc, speshoverviewevent) {
                startup_time = MVM_string_utf8_decode(tc, tc->instance->VMString, "startup_time", 12);
            }
        }

        if (MVM_repr_exists_key(tc, config, gcevent)) {
            MVMObject *value = MVM_repr_at_key_o(tc, config, gcevent);

            if (MVM_is_null(tc, value)) {
                tc->instance->subscriptions.GCEvent = NULL;
            }
            else if (REPR(value)->ID == MVM_REPR_ID_VMArray && !IS_CONCRETE(value) && (((MVMArrayREPRData *)STABLE(value)->REPR_data)->slot_type == MVM_ARRAY_I64 || ((MVMArrayREPRData *)STABLE(value)->REPR_data)->slot_type == MVM_ARRAY_U64)) {
                tc->instance->subscriptions.GCEvent = value;
            }
            else {
                uv_mutex_unlock(&tc->instance->subscriptions.mutex_event_subscription);
                MVM_exception_throw_adhoc(tc, "vmeventsubscribe expects value at 'gcevent' key to be null (to unsubscribe) or a VMArray of int64 type object, got a %s%s%s (%s)", IS_CONCRETE(value) ? "concrete " : "", MVM_6model_get_debug_name(tc, value), IS_CONCRETE(value) ? "" : " type object", REPR(value)->name);
            }
        }

        if (MVM_repr_exists_key(tc, config, speshoverviewevent)) {
            MVMObject *value = MVM_repr_at_key_o(tc, config, speshoverviewevent);

            if (MVM_is_null(tc, value)) {
                tc->instance->subscriptions.SpeshOverviewEvent = NULL;
            }
            else if (REPR(value)->ID == MVM_REPR_ID_VMArray && !IS_CONCRETE(value) && (((MVMArrayREPRData *)STABLE(value)->REPR_data)->slot_type == MVM_ARRAY_I64 || ((MVMArrayREPRData *)STABLE(value)->REPR_data)->slot_type == MVM_ARRAY_U64)) {
                tc->instance->subscriptions.SpeshOverviewEvent = value;
            }
            else {
                uv_mutex_unlock(&tc->instance->subscriptions.mutex_event_subscription);
                MVM_exception_throw_adhoc(tc, "vmeventsubscribe expects value at 'speshoverviewevent' key to be null (to unsubscribe) or a VMArray of int64 type object, got a %s%s%s (%s)", IS_CONCRETE(value) ? "concrete " : "", MVM_6model_get_debug_name(tc, value), IS_CONCRETE(value) ? "" : " type object", REPR(value)->name);
            }
        }

        if (MVM_repr_exists_key(tc, config, startup_time)) {
            /* Value is ignored, it will just be overwritten. */
            MVMObject *value = NULL;
            MVMROOT3(tc, gcevent, speshoverviewevent, startup_time) {
                    value = MVM_repr_box_num(tc, tc->instance->boot_types.BOOTNum, tc->instance->subscriptions.vm_startup_now);
            }

            if (MVM_is_null(tc, value)) {
                uv_mutex_unlock(&tc->instance->subscriptions.mutex_event_subscription);
                MVM_exception_throw_adhoc(tc, "vmeventsubscribe was unable to create a Num object to hold the vm startup time.");
            }
            MVM_repr_bind_key_o(tc, config, startup_time, value);
        }
    }

    uv_mutex_unlock(&tc->instance->subscriptions.mutex_event_subscription);
}

void MVM_vm_set_lib_path(MVMInstance *instance, int count, const char **lib_path) {
    enum { MAX_COUNT = sizeof instance->lib_path / sizeof *instance->lib_path };

    int i = 0;

    if (count > MAX_COUNT)
        MVM_panic(1, "Cannot set more than %i library paths", MAX_COUNT);

    for (; i < count; ++i)
        instance->lib_path[i] = lib_path[i];

    /* Clear remainder to allow repeated calls */
    for (; i < MAX_COUNT; ++i)
        instance->lib_path[i] = NULL;
}

int MVM_exepath(char* buffer, size_t* size) {
    return uv_exepath(buffer, size);
}

#ifdef _WIN32
int MVM_set_std_handles_to_nul() {
    if (!MVM_set_std_handle_to_nul(stdin,  0, 1, STD_INPUT_HANDLE))  return 0;
    if (!MVM_set_std_handle_to_nul(stdout, 1, 0, STD_OUTPUT_HANDLE)) return 0;
    if (!MVM_set_std_handle_to_nul(stderr, 2, 0, STD_ERROR_HANDLE))  return 0;
    return 1;
}
#endif
