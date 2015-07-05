/* Body of a multi-dim array is two blobs of memory: one holding the sizes of
 * the dimensions, and another holding the storage. The number of dimensions
 * is part of the type. */
struct MVMMultiDimArrayBody {
    /* The sizes of the dimensions. */
    MVMint64 *dimensions;

    /* 1D array of slots, which is the storage. We do the math on the
     * dimensions to get a mapping into here. Note that this memory is
     * fixed in size and never reallocated over the life of the array. */
    union {
        MVMObject **o;
        MVMString **s;
        MVMint64   *i64;
        MVMint32   *i32;
        MVMint16   *i16;
        MVMint8    *i8;
        MVMnum64   *n64;
        MVMnum32   *n32;
        MVMuint64  *u64;
        MVMuint32  *u32;
        MVMuint16  *u16;
        MVMuint8   *u8;
        void       *any;
    } slots;
};

struct MVMMultiDimArray {
    MVMObject common;
    MVMMultiDimArrayBody body;
};

/* REPR data specifies the type of array elements and number of dimensions we
 * have (the actual size of the dimensions is part of the value). */
struct MVMMultiDimArrayREPRData {
    /* Number of dimensions we have. */
    MVMint64 num_dimensions;

    /* The size of each element. */
    size_t elem_size;

    /* What type of slots we have. */
    MVMuint8 slot_type;

    /* Type object for the element type. */
    MVMObject *elem_type;
};

/* Initializes the MultiDimArray REPR. */
const MVMREPROps * MVMMultiDimArray_initialize(MVMThreadContext *tc);
