#include "moarvm.h"

#define init_mutex(loc, name) do { \
    if ((init_stat = uv_mutex_init(&loc)) < 0) { \
        fprintf(stderr, "MoarVM: Initialization of " name " mutex failed\n    %s\n", \
            uv_strerror(init_stat)); \
        exit(1); \
	} \
} while (0)

/* Create a new instance of the VM. */
static void string_consts(MVMThreadContext *tc);
MVMInstance * MVM_vm_create_instance(void) {
    MVMInstance *instance;
    int init_stat;

    /* Set up instance data structure. */
    instance = calloc(1, sizeof(MVMInstance));
    instance->boot_types = calloc(1, sizeof(MVMBootTypes));

    /* Create the main thread's ThreadContext and stash it. */
    instance->main_thread = MVM_tc_create(instance);

    /* No user threads when we start, and next thread to be created gets ID 1
     * (the main thread got ID 0). */
    instance->num_user_threads    = 0;
    instance->next_user_thread_id = 1;

    /* Set up the permanent roots storage. */
    instance->num_permroots   = 0;
    instance->alloc_permroots = 16;
    instance->permroots       = malloc(sizeof(MVMCollectable **) * instance->alloc_permroots);
    init_mutex(instance->mutex_permroots, "permanent roots");

    /* Set up HLL config mutex. */
    init_mutex(instance->mutex_hllconfigs, "hll configs");

    /* Set up weak reference hash mutex. */
    init_mutex(instance->mutex_sc_weakhash, "sc weakhash");

    /* Set up loaded compunits hash mutex. */
    init_mutex(instance->mutex_loaded_compunits, "loaded compunits");

    /* Set up container registry mutex. */
    init_mutex(instance->mutex_container_registry, "container registry");

    /* Bootstrap 6model. It is assumed the GC will not be called during this. */
    MVM_6model_bootstrap(instance->main_thread);

    /* Fix up main thread's usecapture. */
    instance->main_thread->cur_usecapture = MVM_repr_alloc_init(instance->main_thread, instance->CallCapture);

    /* get libuv default event loop. */
    instance->default_loop = instance->main_thread->loop;

    /* Create main thread object, and also make it the start of the all threads
     * linked list. */
    instance->threads =
        instance->main_thread->thread_obj = (MVMThread *)
            REPR(instance->boot_types->BOOTThread)->allocate(
                instance->main_thread, STABLE(instance->boot_types->BOOTThread));
    instance->threads->body.stage = MVM_thread_stage_started;
    instance->threads->body.tc = instance->main_thread;

    /* Create compiler registry */
    instance->compiler_registry = MVM_repr_alloc_init(instance->main_thread, instance->boot_types->BOOTHash);

    /* Set up compiler registr mutex. */
    init_mutex(instance->mutex_compiler_registry, "compiler registry");

    /* Create hll symbol tables */
    instance->hll_syms = MVM_repr_alloc_init(instance->main_thread, instance->boot_types->BOOTHash);

    /* Set up hll symbol tables mutex. */
    init_mutex(instance->mutex_hll_syms, "hll syms");

    /* Initialize string cclass handling. */
    MVM_string_cclass_init(instance->main_thread);

    /* Set up some string constants commonly used. */
    string_consts(instance->main_thread);

    return instance;
}

/* Sets up some string constants. */
static void string_consts(MVMThreadContext *tc) {
    MVMStringConsts *s = malloc(sizeof(MVMStringConsts));

    s->empty = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&s->empty);

    s->Str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Str");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&s->Str);

    s->Num = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "Num");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&s->Num);

    tc->instance->str_consts = s;
}

/* This callback is passed to the interpreter code. It takes care of making
 * the initial invocation. */
static void toplevel_initial_invoke(MVMThreadContext *tc, void *data) {
    /* Dummy, 0-arg callsite. */
    static MVMCallsite no_arg_callsite = { NULL, 0, 0 };

    /* Create initial frame, which sets up all of the interpreter state also. */
    MVM_frame_invoke(tc, (MVMStaticFrame *)data, &no_arg_callsite, NULL, NULL, NULL);
}

/* Loads bytecode from the specified file name and runs it. */
void MVM_vm_run_file(MVMInstance *instance, const char *filename) {
    MVMStaticFrame *start_frame;

    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->main_thread;
    MVMCompUnit      *cu = MVM_cu_map_from_file(tc, filename);

    cu->body.filename = MVM_string_utf8_decode(tc, instance->VMString, filename, strlen(filename));

    /* Run deserialization frame, if there is one. */
    if (cu->body.deserialize_frame) {
        MVMROOT(tc, cu, {
            MVM_interp_run(tc, &toplevel_initial_invoke, cu->body.deserialize_frame);
        });
    }

    /* Run the frame marked main, or if there is none then fall back to the
     * first frame. */
    start_frame = cu->body.main_frame ? cu->body.main_frame : cu->body.frames[0];
    MVM_interp_run(tc, &toplevel_initial_invoke, start_frame);
}

/* Loads bytecode from the specified file name and dumps it. */
void MVM_vm_dump_file(MVMInstance *instance, const char *filename) {
    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->main_thread;
    MVMCompUnit      *cu = MVM_cu_map_from_file(tc, filename);
    char *dump = MVM_bytecode_dump(tc, cu);

    printf("%s", dump);
    free(dump);
}

/* Destroys a VM instance. */
void MVM_vm_destroy_instance(MVMInstance *instance) {
    MVMuint16 i;

    /* Free various instance-wide storage. */
    MVM_checked_free_null(instance->boot_types);
    MVM_checked_free_null(instance->str_consts);
    MVM_checked_free_null(instance->repr_registry);
    MVM_HASH_DESTROY(hash_handle, MVMREPRHashEntry, instance->repr_name_to_id_hash);

    /* Destroy main thread contexts. */
    MVM_tc_destroy(instance->main_thread);

    /* Clean up GC permanent roots related resources. */
    uv_mutex_destroy(&instance->mutex_permroots);
    free(instance->permroots);

    /* Clear up VM instance memory. */
    free(instance);

}
