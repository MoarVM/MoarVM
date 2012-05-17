#include <stdio.h>
#include <moarvm.h>

int main(int argc, char *argv[]) {
    MVMInstance *instance;
    
    if (argc != 2) {
        fprintf(stderr, "Usage: moarvm bytecode-file.moarvm\n");
        exit(1);
    }
    
    instance = MVM_vm_create_instance();
    MVM_vm_run_file(instance, argv[1]);
    MVM_vm_destroy_instance(instance);
    
    return 0;
}
