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
    /* Map the compilation unit into memory and disect it. */
    MVMThreadContext *tc = instance->threads[0];
    MVMCompUnit      *cu = MVM_cu_map_from_file(tc, filename);
    
    /* Run the code.
     * XXX This whole frame creation will need factoring out;
     * this is just to get us able to run something.
     */
    MVMFrame *f = malloc(sizeof(MVMFrame));
    f->tc = tc;
    f->static_info = cu->frames[0];
    MVM_interp_run(tc, f);
}

/* Destroys a VM instance. */
void MVM_vm_destroy_instance(MVMInstance *instance) {
    MVMuint16 i;
    
    /* TODO: Lots of cleanup. */
    
    /* Destory all thread contexts. */
    for (i = 0; i < instance->num_threads; i++)
        MVM_tc_destroy(instance->threads[i]);
    
    /* Clear up VM instance memory. */
    free(instance->threads);
    free(instance);
    
    /* Terminate APR. */
    apr_terminate();
}
