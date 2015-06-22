/* A VMArray represents a dynamic array, which can be resized over time. We do
 * not need to promise thread safety on the operations, but we do need to be
 * absolutely sure that we will never SEGV by reading out of bounds. Therefore
 * the dynamic array points to a piece of memory containing a non-resizable
 * array. We only ever free this at a safepoint, meaning that other threads
 * doing an access to a previous "version" will never be looking at freed
 * memory. Further, in a multi-threaded context, we will do an atomic swap
 * of the current "version" to ensure that we don't get leaks. It's worth
 * noting that this is in many ways similar to the approach taken by, say,
 * the CLR and JVM to ensure that even if things like List<T> are not ever
 * thread safe, they're still memory-safe in the light of mis-use. */
struct MVMArrayBody {
    MVMArrayData *data;
};
struct MVMArray {
    MVMObject common;
    MVMArrayBody body;
};

/* Data for the "current version" of the dynamic array, with a fixed size. */
struct MVMArrayData {
    /* Number of elements (from user's point of view); mutable. */
    MVMuint64   elems;

    /* Slot index of first element; mutable. */
    MVMuint64   start;

    /* Size of slots array; immutable. */
    MVMuint64   ssize;

    /* Union of various types of storage we may have. Immutable, size is the
     * sszie above. */
    /* TODO: Hang this off the end of the same memory blob as the size info
     * above, to save an allocation. */
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

/* Types of things we may be storing. */
#define MVM_ARRAY_OBJ   0
#define MVM_ARRAY_STR   1
#define MVM_ARRAY_I64   2
#define MVM_ARRAY_I32   3
#define MVM_ARRAY_I16   4
#define MVM_ARRAY_I8    5
#define MVM_ARRAY_N64   6
#define MVM_ARRAY_N32   7
#define MVM_ARRAY_U64   8
#define MVM_ARRAY_U32   9
#define MVM_ARRAY_U16   10
#define MVM_ARRAY_U8    11
#define MVM_ARRAY_U4    12
#define MVM_ARRAY_U2    13
#define MVM_ARRAY_U1    14
#define MVM_ARRAY_I4    15
#define MVM_ARRAY_I2    16
#define MVM_ARRAY_I1    17

/* Function for REPR setup. */
const MVMREPROps * MVMArray_initialize(MVMThreadContext *tc);

/* Array REPR data specifies the type of array elements we have. */
struct MVMArrayREPRData {
    /* The size of each element. */
    size_t elem_size;

    /* What type of slots we have. */
    MVMuint8 slot_type;

    /* Type object for the element type. */
    MVMObject *elem_type;
};

/* Functions related to working with object with array representation. */
MVM_STATIC_INLINE MVMArrayData * MVM_array_get_data(MVMThreadContext *tc, MVMArray *array) {
    return array->body.data;
}
void MVM_array_get_slots_and_elems(MVMThreadContext *tc, MVMArray *array, void **slots_out, MVMint64 *elems_out);
void MVM_array_set_data(MVMThreadContext *tc, MVMArray *array, void *data, MVMint64 elems);
