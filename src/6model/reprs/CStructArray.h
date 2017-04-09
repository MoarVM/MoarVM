/* Body of a CStructArray. */
struct MVMCStructArrayBody {
    /* The storage of C-land elements. */
    void *storage;

    /* The storage of Perl-land elements */
    MVMObject **child_objs;

    /* Are we managing the memory for this array ourselves, or does it come
     * from C? */
    MVMint32 managed;

    /* The number of elements we've allocated. If we do not know,
     * because the array was returned to us from elsewhere and we
     * are not managing it's memory, this is 0. */
    MVMint32 allocated;

    /* The number of elements we have, if known. Invalid if we
     * are not managing the array. */
    MVMint32 elems;
};

struct MVMCStructArray {
    MVMObject common;
    MVMCStructArrayBody body;
};

/* What kind of element do we have? */
#define MVM_CSTRUCTARRAY_ELEM_KIND_CSTRUCT 1

/* The CStructArray REPR data contains a little info about the type of array
 * that we have. */
struct MVMCStructArrayREPRData {
    /* The number of bytes in size that an element is. */
    MVMint32 elem_size;

    /* The type of an element. */
    MVMObject *elem_type;

    /* What kind of element is it (lets us quickly know how to handle access
     * to it). */
    MVMint32 elem_kind;
};

/* Initializes the CStructArray REPR. */
const MVMREPROps * MVMCStructArray_initialize(MVMThreadContext *tc);
