/* Concurrent use of a VMArray is erroneous. This debugging option will
 * catch bad usages. (Eventually, we will refactor VMArray to not have
 * this issue.) */
#define MVM_ARRAY_CONC_DEBUG 0

/* Representation used by VM-level arrays. Adopted from QRPA work by
 * Patrick Michaud. */
struct MVMArrayBody {
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
        MVMuint64  *u64;
        MVMuint32  *u32;
        MVMuint16  *u16;
        MVMuint8   *u8;
        void       *any;
    } slots;

#if MVM_ARRAY_CONC_DEBUG
    AO_t in_use;
#endif 
};
struct MVMArray {
    MVMObject common;
    MVMArrayBody body;
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
void MVM_VMArray_at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind);
void *MVM_VMArray_find_fast_impl_for_jit(MVMThreadContext *tc, MVMSTable *st, MVMint16 op, MVMuint16 kind);
