/* Representation of a reference to a native value. */
struct MVMNativeRefBody {
    union {
        struct {
            MVMFrame *frame;
            MVMRegister *var;
        } reg_or_lex;
        struct {
            MVMObject *obj;
            MVMObject *class_handle;
            MVMString *name;
        } attribute;
        struct {
            MVMObject *obj;
            MVMint64 idx;
        } positional;
    } u;
};
struct MVMNativeRef {
    MVMObject common;
    MVMNativeRefBody body;
};

/* Kinds of native reference. */
#define MVM_NATIVEREF_REG_OR_LEX    1
#define MVM_NATIVEREF_ATTRIBUTE     2
#define MVM_NATIVEREF_POSITIONAL    3

/* REPR data for a native reference. */
struct MVMNativeRefREPRData {
    /* The primitive type of native reference this is (one of the values that
     * is valid for MVMStorageSpec.boxed_primitive). */
    MVMuint16 primitive_type;

    /* The kind of reference this is. */
    MVMuint16 ref_kind;
};

/* Function for REPR setup. */
const MVMREPROps * MVMNativeRef_initialize(MVMThreadContext *tc);

/* Operations on a nativeref REPR. */
void MVM_nativeref_ensure(MVMThreadContext *tc, MVMObject *val, MVMuint16 wantprim, MVMuint16 wantkind, char *guilty);
MVMObject * MVM_nativeref_reg_i(MVMThreadContext *tc, MVMFrame *frame, MVMRegister *reg);
MVMObject * MVM_nativeref_reg_n(MVMThreadContext *tc, MVMFrame *frame, MVMRegister *reg);
MVMObject * MVM_nativeref_reg_s(MVMThreadContext *tc, MVMFrame *frame, MVMRegister *reg);
MVMObject * MVM_nativeref_lex_i(MVMThreadContext *tc, MVMuint16 outers, MVMuint16 idx);
MVMObject * MVM_nativeref_lex_n(MVMThreadContext *tc, MVMuint16 outers, MVMuint16 idx);
MVMObject * MVM_nativeref_lex_s(MVMThreadContext *tc, MVMuint16 outers, MVMuint16 idx);
MVMObject * MVM_nativeref_lex_name_i(MVMThreadContext *tc, MVMString *name);
MVMObject * MVM_nativeref_lex_name_n(MVMThreadContext *tc, MVMString *name);
MVMObject * MVM_nativeref_lex_name_s(MVMThreadContext *tc, MVMString *name);
MVMObject * MVM_nativeref_attr_i(MVMThreadContext *tc, MVMObject *obj, MVMObject *class_handle, MVMString *name);
MVMObject * MVM_nativeref_attr_n(MVMThreadContext *tc, MVMObject *obj, MVMObject *class_handle, MVMString *name);
MVMObject * MVM_nativeref_attr_s(MVMThreadContext *tc, MVMObject *obj, MVMObject *class_handle, MVMString *name);
MVMObject * MVM_nativeref_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVMObject * MVM_nativeref_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVMObject * MVM_nativeref_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVMint64 MVM_nativeref_read_reg_or_lex_i(MVMThreadContext *tc, MVMObject *ref);
MVMnum64 MVM_nativeref_read_reg_or_lex_n(MVMThreadContext *tc, MVMObject *ref);
MVMString * MVM_nativeref_read_reg_or_lex_s(MVMThreadContext *tc, MVMObject *ref);
MVMint64 MVM_nativeref_read_attribute_i(MVMThreadContext *tc, MVMObject *ref);
MVMnum64 MVM_nativeref_read_attribute_n(MVMThreadContext *tc, MVMObject *ref);
MVMString * MVM_nativeref_read_attribute_s(MVMThreadContext *tc, MVMObject *ref);
MVMint64 MVM_nativeref_read_positional_i(MVMThreadContext *tc, MVMObject *ref);
MVMnum64 MVM_nativeref_read_positional_n(MVMThreadContext *tc, MVMObject *ref);
MVMString * MVM_nativeref_read_positional_s(MVMThreadContext *tc, MVMObject *ref);
void MVM_nativeref_write_reg_or_lex_i(MVMThreadContext *tc, MVMObject *ref, MVMint64 value);
void MVM_nativeref_write_reg_or_lex_n(MVMThreadContext *tc, MVMObject *ref, MVMnum64 value);
void MVM_nativeref_write_reg_or_lex_s(MVMThreadContext *tc, MVMObject *ref, MVMString *value);
void MVM_nativeref_write_attribute_i(MVMThreadContext *tc, MVMObject *ref, MVMint64 value);
void MVM_nativeref_write_attribute_n(MVMThreadContext *tc, MVMObject *ref, MVMnum64 value);
void MVM_nativeref_write_attribute_s(MVMThreadContext *tc, MVMObject *ref, MVMString *value);
void MVM_nativeref_write_positional_i(MVMThreadContext *tc, MVMObject *ref, MVMint64 value);
void MVM_nativeref_write_positional_n(MVMThreadContext *tc, MVMObject *ref, MVMnum64 value);
void MVM_nativeref_write_positional_s(MVMThreadContext *tc, MVMObject *ref, MVMString *value);
