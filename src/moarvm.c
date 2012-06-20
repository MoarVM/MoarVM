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
    if (apr_pool_create(&instance->apr_pool, NULL) != APR_SUCCESS) {
        char error[256];
        fprintf(stderr, "MoarVM: Initialization of APR pool failed\n    %s\n",
            apr_strerror(apr_init_stat, error, 256));
        exit(1);
    }
    
    /* The main (current) thread gets a ThreadContext. */
    instance->num_threads = 1;
    instance->threads     = malloc(sizeof(MVMThreadContext *));
    instance->threads[0]  = MVM_tc_create(instance);
    
    /* Bootstrap 6model. */
    MVM_6model_bootstrap(instance->threads[0]);
    
    return instance;
}

/* Loads bytecode from the specified file name and runs it. */
void MVM_vm_run_file(MVMInstance *instance, char *filename) {
    /* Map the compilation unit into memory and dissect it. */
    MVMThreadContext *tc = instance->threads[0];
    MVMCompUnit      *cu = MVM_cu_map_from_file(tc, filename);
    
    /* Run the first frame. */
    MVM_interp_run(tc, cu->frames[0]);
}

/* Destroys a VM instance. */
void MVM_vm_destroy_instance(MVMInstance *instance) {
    MVMuint16 i;
    
    /* TODO: Lots of cleanup. */
    
    /* Destroy all thread contexts. */
    for (i = 0; i < instance->num_threads; i++)
        MVM_tc_destroy(instance->threads[i]);
    
    /* Free APR pool. */
    apr_pool_destroy(instance->apr_pool);
    
    /* Clear up VM instance memory. */
    free(instance->threads);
    free(instance);
    
    /* Terminate APR. */
    apr_terminate();
}
