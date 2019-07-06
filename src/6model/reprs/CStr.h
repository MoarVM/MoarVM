#define MVM_CSTR_C_TYPE_CHAR     -1
#define MVM_CSTR_C_TYPE_WCHAR_T  -2
#define MVM_CSTR_C_TYPE_CHAR16_T -3
#define MVM_CSTR_C_TYPE_CHAR32_T -4

/* Representation for C Str. */
struct MVMCStrBody {
    MVMString *source;
    union {
        char      *c;
        MVMwchar  *wide;
        MVMchar16 *u16;
        MVMchar32 *u32;
    } value;
};

struct MVMCStr {
    MVMObject common;
    MVMCStrBody body;
};

struct MVMCStrREPRData {
    MVMint32 type;
};

/* Initializes the CStr REPR. */
const MVMREPROps * MVMCStr_initialize(MVMThreadContext *tc);
