#include "moarvm.h"

MVMObject * MVM_ptr_alloc_blob(MVMThreadContext *tc, MVMuint64 size) {
    MVMMemBlob *blob;
    void *storage = malloc(size);

    if (!storage)
        MVM_exception_throw_adhoc(tc,
                "failed to allocate memory blob of size %" PRIu64, size);

    blob = (MVMMemBlob *)MVM_gc_allocate_object(tc,
            STABLE(tc->instance->raw_types.RawMemBlob));

    blob->body.address = malloc(size);
    blob->body.size = size;
    blob->body.refmap = NULL;

    return (MVMObject *)blob;
}
