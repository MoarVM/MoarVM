/* Representation for code in the VM. Holds an MVMStaticFrame along
 * with an optional outer pointer if this is a closure. */
struct MVMCodeBody {
    MVMStaticFrame *sf;
    MVMFrame       *outer;
    MVMObject      *code_object;
};
struct MVMCode {
    MVMObject common;
    MVMCodeBody body;
};

/* Function for REPR setup. */
MVMREPROps * MVMCode_initialize(MVMThreadContext *tc);
