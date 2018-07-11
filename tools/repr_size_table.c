#include "moar.h"


int main (int argc, char **argv) {
    MVMInstance *instance = MVM_vm_create_instance();
    MVMuint32 i, j;
    fprintf(stderr,
            "| REPR                     | size | aligned | waste | ratio | \n"
            "|--------------------------+------|---------|-------|-------|\n");
    for (i = 0, j = 0; j < instance->num_reprs; i++) {
        MVMReprRegistry *registered = instance->repr_list[i];
        if (registered != NULL) {
            MVMObject *object = registered->repr->type_object_for(instance->main_thread, NULL);
            MVMuint32 size = object->st->size;
            MVMuint32 aligned = (size & 63) ? (size & ~63) + 64 : size;
            MVMuint32 waste = aligned - size;
            MVMuint32 ratio = aligned ? (100 * waste) / aligned : 0;
            fprintf(stderr, "| %-24s | %4u | %7u | %5u | %4u%% |\n",
                    registered->repr->name, size, aligned, waste, ratio);
            j++;
        }
    }
    /* MVM_vm_destroy_instance(instance); */
}
