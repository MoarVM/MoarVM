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
    
    /* Stash the main thread's ThreadContext. */
    instance->main_thread = MVM_tc_create(instance);
    
    /* No user threads when we start. */
    instance->num_user_threads = 0;
    
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
    
    /* Set up the second generation allocator. */
    instance->gen2 = MVM_gc_gen2_create(instance);
    
    /* Bootstrap 6model. */
    MVM_6model_bootstrap(instance->main_thread);
    
    return instance;
}

/* Loads bytecode from the specified file name and runs it. */
void MVM_vm_run_file(MVMInstance *instance, char *filename) {
    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->main_thread;
    MVMCompUnit      *cu = MVM_cu_map_from_file(tc, filename);
    
    /* Run the first frame. */
    MVM_interp_run(tc, cu->frames[0]);
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
    
    /* Destroy the second generation allocator. */
    MVM_gc_gen2_destroy(instance, instance->gen2);
    
    /* Free APR pool. */
    apr_pool_destroy(instance->apr_pool);
    
    /* Clear up VM instance memory. */
    if (instance->user_threads)
        free(instance->user_threads);
    free(instance);
    
    /* Terminate APR. */
    apr_terminate();
}
