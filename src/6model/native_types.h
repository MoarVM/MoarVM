typedef enum {
    MVM_P6INT_C_TYPE_CHAR       = -1,
    MVM_P6INT_C_TYPE_SHORT      = -2,
    MVM_P6INT_C_TYPE_INT        = -3,
    MVM_P6INT_C_TYPE_LONG       = -4,
    MVM_P6INT_C_TYPE_LONGLONG   = -5,
    MVM_P6INT_C_TYPE_BOOL       = -6,
    MVM_P6INT_C_TYPE_SIZE_T     = -7,
    MVM_P6INT_C_TYPE_ATOMIC_INT = -8,
    MVM_P6INT_C_TYPE_WCHAR_T    = -9,
    MVM_P6INT_C_TYPE_WINT_T     = -10,
    MVM_P6INT_C_TYPE_CHAR16_T   = -11,
    MVM_P6INT_C_TYPE_CHAR32_T   = -12
} MVMIntNativeType;

typedef enum {
    MVM_P6NUM_C_TYPE_FLOAT      = -32,
    MVM_P6NUM_C_TYPE_DOUBLE     = -33,
    MVM_P6NUM_C_TYPE_LONGDOUBLE = -34
} MVMNumNativeType;

typedef enum {
    MVM_P6STR_C_TYPE_CHAR       = -64,
    MVM_P6STR_C_TYPE_WCHAR_T    = -65,
    MVM_P6STR_C_TYPE_CHAR16_T   = -66,
    MVM_P6STR_C_TYPE_CHAR32_T   = -67
} MVMStrNativeType;
