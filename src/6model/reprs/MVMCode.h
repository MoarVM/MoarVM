/* Representation for code in the VM. Holds an MVMStaticFrame along
 * with an optional outer pointer if this is a closure. */
typedef struct _MVMCodeBody {
    MVMStaticFrame *sf;
    MVMFrame       *outer;
    MVMObject      *code_object;
    MVMuint16       is_static;
    MVMuint16       is_compiler_stub;
} MVMCodeBody;
typedef struct _MVMCode {
    MVMObject common;
    MVMCodeBody body;
} MVMCode;

/* Function for REPR setup. */
MVMREPROps * MVMCode_initialize(MVMThreadContext *tc);
