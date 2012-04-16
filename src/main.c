#include <stdio.h>
#include <moarvm.h>

int main() {
    MVMInstance *instance = MVM_vm_create_instance();
    printf("Created VM instance (but it does nothing)\n");
    MVM_vm_destroy_instance(instance);
    printf("Cleared up OK\n");
    return 0;
}
