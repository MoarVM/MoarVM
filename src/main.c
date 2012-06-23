#include <stdio.h>
#include <moarvm.h>

int main(int argc, char *argv[]) {
    MVMInstance *instance;
    
    if (argc != 2) {
        fprintf(stderr, "Usage: moarvm bytecode-file.moarvm\n");
        exit(1);
    }
    
    instance = MVM_vm_create_instance();
    {
        MVMThreadContext *tc = instance->threads[0];
        void *limit = tc->nursery_alloc;
        MVM_gc_nursery_collect(tc);
        MVM_gc_nursery_free_uncopied(tc, limit);
    }
    MVM_vm_run_file(instance, argv[1]);
    MVM_vm_destroy_instance(instance);
    
    return 0;
}
