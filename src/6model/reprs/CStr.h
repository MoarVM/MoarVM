/* Representation for C Str. */
struct MVMCStrBody {
    char *cstr;
};

struct MVMCStr {
    MVMObject common;
    MVMCStrBody body;
};

/* Not needed yet.
typedef struct {
} MVMCStrREPRData;*/

/* Initializes the CStr REPR. */
const MVMREPROps * MVMCStr_initialize(MVMThreadContext *tc);
