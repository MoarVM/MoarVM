#include "moarvm.h"

/* Create a new instance of the VM. */
MVMInstance * MVM_vm_create_instance(void) {
    MVMInstance *instance;
    apr_status_t apr_init_stat;
    
    /* Set up APR related bits. */
    apr_init_stat = apr_initialize();
    if (apr_init_stat != APR_SUCCESS) {
        char error[256];
        fprintf(stderr, "MoarVM: Initialization of APR failed\n    %s\n",
            apr_strerror(apr_init_stat, error, 256));
        exit(1);
    }

    /* Set up instance data structure. */
    instance = calloc(1, sizeof(MVMInstance));
    instance->boot_types = calloc(1, sizeof(struct _MVMBootTypes));
    
    /* Allocate instance APR pool. */
    if ((apr_init_stat = apr_pool_create(&instance->apr_pool, NULL)) != APR_SUCCESS) {
        char error[256];
        fprintf(stderr, "MoarVM: Initialization of APR pool failed\n    %s\n",
            apr_strerror(apr_init_stat, error, 256));
        exit(1);
    }
    
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
    if ((apr_init_stat = apr_thread_mutex_create(&instance->mutex_permroots, APR_THREAD_MUTEX_DEFAULT, instance->apr_pool)) != APR_SUCCESS) {
        char error[256];
        fprintf(stderr, "MoarVM: Initialization of permanent roots mutex failed\n    %s\n",
            apr_strerror(apr_init_stat, error, 256));
        exit(1);
	}

    /* Bootstrap 6model. It is assumed the GC will not be called during this. */
    MVM_6model_bootstrap(instance->main_thread);
    
    instance->running_threads = 
        instance->main_thread->thread_obj = (MVMThread *)
            REPR(instance->boot_types->BOOTThread)->allocate(
                instance->main_thread, STABLE(instance->boot_types->BOOTThread));
    instance->running_threads->body.stage = MVM_thread_stage_started;
    instance->running_threads->body.tc = instance->main_thread;
    
    return instance;
}

/* This callback is passed to the interpreter code. It takes care of making
 * the initial invocation. */
static void toplevel_initial_invoke(MVMThreadContext *tc, void *data) {
    /* Dummy, 0-arg callsite. */
    static MVMCallsite no_arg_callsite;
    no_arg_callsite.arg_flags = NULL;
    no_arg_callsite.arg_count = 0;
    no_arg_callsite.num_pos   = 0;
    
    /* Create initial frame, which sets up all of the interpreter state also. */
    MVM_frame_invoke(tc, (MVMStaticFrame *)data, &no_arg_callsite, NULL, NULL, NULL);
}

/* Loads bytecode from the specified file name and runs it. */
void MVM_vm_run_file(MVMInstance *instance, char *filename) {
    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->main_thread;
    MVMCompUnit      *cu = MVM_cu_map_from_file(tc, filename);
    
    /* Run the first frame. */
    MVM_interp_run(tc, &toplevel_initial_invoke, cu->frames[0]);
}

/* Loads bytecode from the specified file name and dumps it. */
void MVM_vm_dump_file(MVMInstance *instance, char *filename) {
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
    
    /* TODO: Lots of cleanup. */
    
    /* Destroy main thread contexts. */
    MVM_tc_destroy(instance->main_thread);
    
    /* Clean up GC permanent roots related resources. */
    apr_thread_mutex_destroy(instance->mutex_permroots);
    free(instance->permroots);

    /* Free APR pool. */
    apr_pool_destroy(instance->apr_pool);
    
    /* Clear up VM instance memory. */
    free(instance);
    
    /* Terminate APR. */
    apr_terminate();
}
