/* Body of a multi-dim array is two blobs of memory: one holding the sizes of
 * the dimensions, and another holding the storage. The number of dimensions
 * is part of the type. */
struct MVMMultiDimArrayViewBody {
    /* What object are we viewing? */
    MVMObject *target;

    /* The sizes of the dimensions. */
    MVMint64 *dimensions;

    /* How to step through the underlying 1D array to get from
     * the next entry along each of the dimensions.
     * This lets us ignore dimensions, re-order dimensions,
     * step through dimensions backwards or 2 at a time, etc etc.
     * Note that this is pre-multiplied. */
    MVMint64 *strides;

    /* If we want to be able to step through individual dimensions
     * backwards, or if we don't start at 0;0;0 in general, we need
     * to have a different starting position */
    MVMint64 initial_position;
};

struct MVMMultiDimArrayView {
    MVMObject common;
    MVMMultiDimArrayViewBody body;
};

/* REPR data specifies the type of array elements and number of dimensions we
 * have (the actual size of the dimensions is part of the value). */
struct MVMMultiDimArrayViewREPRData {
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
const MVMREPROps * MVMMultiDimArrayView_initialize(MVMThreadContext *tc);
