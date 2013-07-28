/* Representation used by VM-level arrays. Adopted from QRPA work by
 * Patrick Michaud. */
typedef struct _MVMArrayBody {
    /* number of elements (from user's point of view) */
    MVMuint64   elems;

    /* slot index of first element */
    MVMuint64   start;

    /* size of slots array */
    MVMuint64   ssize;

    /* slot array; union of various types of storage we may have. */
    union {
        MVMObject **o;
        MVMString **s;
        MVMint64   *i64;
        MVMint32   *i32;
        MVMint16   *i16;
        MVMint8    *i8;
        MVMnum64   *n64;
        MVMnum32   *n32;
        void       *any;
    } slots;
} MVMArrayBody;
typedef struct _MVMArray {
    MVMObject common;
    MVMArrayBody body;
} MVMArray;

/* Types of things we may be storing. */
#define MVM_ARRAY_OBJ   0
#define MVM_ARRAY_STR   1
#define MVM_ARRAY_I64   2
#define MVM_ARRAY_I32   3
#define MVM_ARRAY_I16   4
#define MVM_ARRAY_I8    5
#define MVM_ARRAY_N64   6
#define MVM_ARRAY_N32   7

/* Function for REPR setup. */
MVMREPROps * MVMArray_initialize(MVMThreadContext *tc);

/* Array REPR data specifies the type of array elements we have. */
typedef struct {
    /* The size of each element. */
    size_t elem_size;

    /* What type of slots we have. */
    MVMuint8 slot_type;
} MVMArrayREPRData;
