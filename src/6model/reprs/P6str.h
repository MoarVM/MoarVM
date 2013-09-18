/* Representation used by P6 native strings. */
struct MVMP6strBody {
    MVMString *value;
};
struct MVMP6str {
    MVMObject common;
    MVMP6strBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6str_initialize(MVMThreadContext *tc);
