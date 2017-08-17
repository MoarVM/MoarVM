#define MVM_P6INT_C_TYPE_CHAR      -1
#define MVM_P6INT_C_TYPE_SHORT     -2
#define MVM_P6INT_C_TYPE_INT       -3
#define MVM_P6INT_C_TYPE_LONG      -4
#define MVM_P6INT_C_TYPE_LONGLONG  -5
#define MVM_P6INT_C_TYPE_SIZE_T    -6
#define MVM_P6INT_C_TYPE_BOOL      -7
#define MVM_P6INT_C_TYPE_ATOMIC    -8

/* Representation used by P6 native ints. */
struct MVMP6intBody {
    /* Integer storage slot. */
    union {
        MVMint64  i64;
        MVMint32  i32;
        MVMint16  i16;
        MVMint8   i8;
        MVMuint64 u64;
        MVMuint32 u32;
        MVMuint16 u16;
        MVMuint8  u8;
    } value;
};
struct MVMP6int {
    MVMObject common;
    MVMP6intBody body;
};

/* The bit width requirement is shared for all instances of the same type. */
struct MVMP6intREPRData {
    MVMint16       bits;
    MVMint16       is_unsigned;
    MVMStorageSpec storage_spec;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6int_initialize(MVMThreadContext *tc);
