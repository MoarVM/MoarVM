/* Representation used by P6 native strings. */
typedef struct _P6strBody {
    MVMString *value;
} P6strBody;
typedef struct _P6str {
    MVMObject common;
    P6strBody body;
} P6str;

/* Function for REPR setup. */
MVMREPROps * P6str_initialize(MVMThreadContext *tc);
