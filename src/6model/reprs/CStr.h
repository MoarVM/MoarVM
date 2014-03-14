/* Representation for C Str. */
struct MVMCStrBody {
    MVMString *orig;
    char      *cstr;
};

struct MVMCStr {
    MVMObject common;
    MVMCStrBody body;
};

/* Initializes the CStr REPR. */
const MVMREPROps * MVMCStr_initialize(MVMThreadContext *tc);
