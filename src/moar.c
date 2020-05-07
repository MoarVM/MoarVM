#include "moar.h"
#include "platform/io.h"
#include <platform/threads.h>
#include "platform/random.h"
#include "platform/time.h"
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
            char *fixed_path = malloc(path_length + 16);
            MVMint64 pid = MVM_proc_getpid(NULL);
            /* We make the brave assumption that
             * pids only go up to 16 characters. */
            snprintf(fixed_path, path_length + 16, path, pid);
            result = MVM_platform_fopen(fixed_path, mode);
            free(fixed_path);
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

/* Create a new instance of the VM. */
MVMInstance * MVM_vm_create_instance(void) {
    MVMInstance *instance;

    char *spesh_log, *spesh_nodelay, *spesh_disable, *spesh_inline_disable,
         *spesh_osr_disable, *spesh_limit, *spesh_blocking, *spesh_inline_log,
         *spesh_pea_disable;
    char *jit_expr_disable, *jit_disable, *jit_last_frame, *jit_last_bb;
    char *dynvar_log;
    int init_stat;

    /* Set up instance data structure. */
    instance = MVM_calloc(1, sizeof(MVMInstance));

    /* Create the main thread's ThreadContext and stash it. */
    instance->main_thread = MVM_tc_create(NULL, instance);

    instance->subscriptions.vm_startup_hrtime = uv_hrtime();
    instance->subscriptions.vm_startup_now = MVM_proc_time_n(instance->main_thread);

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
    init_mutex(instance->mutex_free_at_safepoint, "safepoint free list");

    /* Create fixed size allocator. */
    instance->fsa = MVM_fixed_size_create(instance->main_thread);

    /* Set up REPR registry mutex. */
    init_mutex(instance->mutex_repr_registry, "REPR registry");

    /* Set up HLL config mutex. */
    init_mutex(instance->mutex_hllconfigs, "hll configs");

    /* Set up DLL registry mutex. */
    init_mutex(instance->mutex_dll_registry, "REPR registry");

    /* Set up extension registry mutex. */
    init_mutex(instance->mutex_ext_registry, "extension registry");

    /* Set up extension op registry mutex. */
    init_mutex(instance->mutex_extop_registry, "extension op registry");

    /* Set up SC registry mutex. */
    init_mutex(instance->mutex_sc_registry, "sc registry");

    /* Set up loaded compunits hash mutex. */
    init_mutex(instance->mutex_loaded_compunits, "loaded compunits");

    /* Set up container registry mutex. */
    init_mutex(instance->mutex_container_registry, "container registry");

    /* Set up persistent object ID hash mutex. */
    init_mutex(instance->mutex_object_ids, "object ID hash");

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

    /* Set up the dispatcher registry and the boot dispatchers. */
    MVM_disp_registry_init(instance->main_thread);

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

    /* There's some callsites we statically use all over the place. Intern
     * them, so that spesh may end up optimizing more "internal" stuff. */
    MVM_callsite_initialize_common(instance->main_thread);

    /* Multi-cache additions mutex. */
    init_mutex(instance->mutex_multi_cache_add, "multi-cache addition");

    /* Current instrumentation level starts at 1; used to trigger all frames
     * to be verified before their first run. */
    instance->instrumentation_level = 1;

    /* Mutex for spesh installations, and check if we've a file we
     * should log specializations to. */
    init_mutex(instance->mutex_spesh_install, "spesh installations");
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

    jit_expr_disable = getenv("MVM_JIT_EXPR_DISABLE");
    if (!jit_expr_disable || strlen(jit_expr_disable) == 0)
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
    MVM_frame_invoke(tc, (MVMStaticFrame *)data, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS), NULL, NULL, NULL, -1);
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
    MVMCompUnit      *cu = MVM_cu_map_from_file(tc, filename);

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

/* Loads bytecode from the specified file name and dumps it. */
void MVM_vm_dump_file(MVMInstance *instance, const char *filename) {
    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->main_thread;
    MVMCompUnit      *cu = MVM_cu_map_from_file(tc, filename);
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

/* Exits the process as quickly as is gracefully possible, respecting that
 * foreground threads should join first. Leaves all cleanup to the OS, as it
 * will be able to do it much more swiftly than we could. This is typically
 * not the right thing for embedding; see MVM_vm_destroy_instance for that. */
void MVM_vm_exit(MVMInstance *instance) {
    /* Join any foreground threads and flush standard handles. */
    MVM_thread_join_foreground(instance->main_thread);
    MVM_io_flush_standard_handles(instance->main_thread);

    /* Close any spesh or jit log. */
    if (instance->spesh_log_fh)
        fclose(instance->spesh_log_fh);
    if (instance->dynvar_log_fh) {
        fprintf(instance->dynvar_log_fh, "- x 0 0 0 0 %"PRId64" %"PRIu64" %"PRIu64"\n", instance->dynvar_log_lasttime, uv_hrtime(), uv_hrtime());
        fclose(instance->dynvar_log_fh);
    }

    /* And, we're done. */
    exit(0);
}

static void cleanup_callsite_interns(MVMInstance *instance) {
    int i;

    for (i = 0; i < MVM_INTERN_ARITY_LIMIT; i++) {
        int callsite_count = instance->callsite_interns->num_by_arity[i];
        int j;

        if (callsite_count) {
            MVMCallsite **callsites = instance->callsite_interns->by_arity[i];

            for (j = 0; j < callsite_count; j++) {
                MVMCallsite *callsite = callsites[j];

                if (MVM_callsite_is_common(callsite)) {
                    continue;
                }

                MVM_callsite_destroy(callsite);
            }

            MVM_free(callsites);
        }
    }
    MVM_free(instance->callsite_interns);
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

    /* Run the GC global destruction phase. After this,
     * no 6model object pointers should be accessed. */
    MVM_gc_global_destruction(instance->main_thread);

    /* Clean up dispatcher registry and args identity map. */
    MVM_disp_registry_destroy(instance->main_thread);
    MVM_args_destroy_identity_map(instance->main_thread);

    /* Cleanup REPR registry */
    uv_mutex_destroy(&instance->mutex_repr_registry);
    MVM_HASH_DESTROY(instance->main_thread, hash_handle, MVMReprRegistry, instance->repr_hash);
    MVM_free(instance->repr_list);

    /* Clean up GC related resources. */
    uv_mutex_destroy(&instance->mutex_permroots);
    MVM_free(instance->permroots);
    MVM_free(instance->permroot_descriptions);
    uv_cond_destroy(&instance->cond_gc_start);
    uv_cond_destroy(&instance->cond_gc_finish);
    uv_cond_destroy(&instance->cond_gc_intrays_clearing);
    uv_cond_destroy(&instance->cond_blocked_can_continue);
    uv_mutex_destroy(&instance->mutex_gc_orchestrate);

    /* Clean up safepoint free vector. */
    MVM_VECTOR_DESTROY(instance->free_at_safepoint);
    uv_mutex_destroy(&instance->mutex_free_at_safepoint);

    /* Clean up Hash of HLLConfig. */
    uv_mutex_destroy(&instance->mutex_hllconfigs);
    MVM_HASH_DESTROY(instance->main_thread, hash_handle, MVMHLLConfig, instance->compiler_hll_configs);
    MVM_HASH_DESTROY(instance->main_thread, hash_handle, MVMHLLConfig, instance->compilee_hll_configs);

    /* Clean up Hash of DLLs. */
    uv_mutex_destroy(&instance->mutex_dll_registry);
    MVM_HASH_DESTROY(instance->main_thread, hash_handle, MVMDLLRegistry, instance->dll_registry);

    /* Clean up Hash of extensions. */
    uv_mutex_destroy(&instance->mutex_ext_registry);
    MVM_HASH_DESTROY(instance->main_thread, hash_handle, MVMExtRegistry, instance->ext_registry);

    /* Clean up Hash of extension ops. */
    uv_mutex_destroy(&instance->mutex_extop_registry);
    MVM_HASH_DESTROY(instance->main_thread, hash_handle, MVMExtOpRegistry, instance->extop_registry);

    /* Clean up Hash of all known serialization contexts; all SCs list is in
     * FSA space and so cleaned up with that. */
    uv_mutex_destroy(&instance->mutex_sc_registry);
    MVM_HASH_DESTROY(instance->main_thread, hash_handle, MVMSerializationContextBody, instance->sc_weakhash);

    /* Clean up Hash of filenames of compunits loaded from disk. */
    uv_mutex_destroy(&instance->mutex_loaded_compunits);
    MVM_HASH_DESTROY(instance->main_thread, hash_handle, MVMLoadedCompUnitName, instance->loaded_compunits);

    /* Clean up Container registry. */
    uv_mutex_destroy(&instance->mutex_container_registry);
    MVM_HASH_DESTROY(instance->main_thread, hash_handle, MVMContainerRegistry, instance->container_registry);

    /* Clean up Hash of compiler objects keyed by name. */
    uv_mutex_destroy(&instance->mutex_compiler_registry);

    /* Clean up Hash of hashes of symbol tables per hll. */
    uv_mutex_destroy(&instance->mutex_hll_syms);

    /* Clean up multi cache addition mutex. */
    uv_mutex_destroy(&instance->mutex_multi_cache_add);

    /* Clean up parameterization addition mutex. */
    uv_mutex_destroy(&instance->mutex_parameterization_add);

    /* Clean up interned callsites */
    uv_mutex_destroy(&instance->mutex_callsite_interns);
    cleanup_callsite_interns(instance);

    /* Release this interpreter's hold on Unicode database */
    MVM_unicode_release(instance->main_thread);

    /* Clean up spesh mutexes and close any log. */
    uv_mutex_destroy(&instance->mutex_spesh_install);
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

    /* Destroy main thread contexts and thread list mutex. */
    MVM_tc_destroy(instance->main_thread);
    uv_mutex_destroy(&instance->mutex_threads);

    /* Clean up fixed size allocator */
    MVM_fixed_size_destroy(instance->fsa);

    uv_mutex_destroy(&instance->subscriptions.mutex_event_subscription);

    /* Clear up VM instance memory. */
    MVM_free(instance);
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

    MVMROOT2(tc, queue, config, {
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
        MVMROOT(tc, gcevent, {
            speshoverviewevent = MVM_string_utf8_decode(tc, tc->instance->VMString, "speshoverviewevent", 18);
            MVMROOT(tc, speshoverviewevent, {
                startup_time = MVM_string_utf8_decode(tc, tc->instance->VMString, "startup_time", 12);
            });
        });

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
            MVMROOT3(tc, gcevent, speshoverviewevent, startup_time, {
                    value = MVM_repr_box_num(tc, tc->instance->boot_types.BOOTNum, tc->instance->subscriptions.vm_startup_now);
            });

            if (MVM_is_null(tc, value)) {
                uv_mutex_unlock(&tc->instance->subscriptions.mutex_event_subscription);
                MVM_exception_throw_adhoc(tc, "vmeventsubscribe was unable to create a Num object to hold the vm startup time.");
            }
            MVM_repr_bind_key_o(tc, config, startup_time, value);
        }
    });

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
