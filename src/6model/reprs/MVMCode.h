/* Representation for code in the VM. Holds an MVMStaticFrame along
 * with an optional outer pointer if this is a closure. */
struct MVMCodeBody {
    MVMStaticFrame *sf;
    MVMFrame       *outer;
    MVMObject      *code_object;
    MVMString      *name;
    MVMRegister    *state_vars;
    MVMuint8       *state_vars_is_hll_init;
    MVMuint16       is_static;
    MVMuint16       is_compiler_stub;
};
struct MVMCode {
    MVMObject common;
    MVMCodeBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCode_initialize(MVMThreadContext *tc);

MVM_PUBLIC MVMObject * MVM_code_location(MVMThreadContext *tc, MVMObject *code);
void MVM_code_location_out(MVMThreadContext *tc, MVMObject *code,
                           MVMString **file_out, MVMint32 *line_out);
