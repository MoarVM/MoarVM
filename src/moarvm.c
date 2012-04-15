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
        exit(0);
    }

    /* Set up instance data structure. */
    instance = malloc(sizeof(MVMInstance));
    memset(instance, 0, sizeof(MVMInstance));
    
    return instance;
}

/* Destroys a VM instance. */
void MVM_vm_destroy_instance(MVMInstance *instance) {
    /* Clear up VM instance memory. */
    free(instance);
    
    /* Terminate APR. */
    apr_terminate();
}
