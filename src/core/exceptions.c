#include "moarvm.h"

/* Panics and shuts down the VM. Don't do this unless it's something quite
 * unrecoverable.
 * TODO: Some hook for embedders.
 */
void MVM_panic(char *reason) {
    fprintf(stderr, reason);
    exit(1);
}
