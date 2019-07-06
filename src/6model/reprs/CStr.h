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
    MVMint32  type;
    MVMuint64 length;
};

/* Initializes the CStr REPR. */
const MVMREPROps * MVMCStr_initialize(MVMThreadContext *tc);
