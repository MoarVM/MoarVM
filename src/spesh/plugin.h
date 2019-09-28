/* Pairing of instruction position with the guard set at that position. */
struct MVMSpeshPluginPosition {
    MVMSpeshPluginGuardSet *guard_set;
    MVMuint32 bytecode_position;
};

/* The guard set held at a particular position. This is an array of guards
 * clauses to evaluate, with a count of them to know when we hit the end.
 * We just append new guard sets to the end. */
struct MVMSpeshPluginGuardSet {
    MVMSpeshPluginGuard *guards;
    MVMuint32 num_guards;
};

/* The maximum number of guards a spesh plugin can set up. */
#define MVM_SPESH_PLUGIN_GUARD_LIMIT    16

/* Types of guard that we have. */
#define MVM_SPESH_PLUGIN_GUARD_RESULT   0   /* Node indicating a match */
#define MVM_SPESH_PLUGIN_GUARD_OBJ      1   /* Literal object match */
#define MVM_SPESH_PLUGIN_GUARD_NOTOBJ   2   /* Literal object non-match */
#define MVM_SPESH_PLUGIN_GUARD_TYPE     3   /* Exact type match guard */
#define MVM_SPESH_PLUGIN_GUARD_CONC     4   /* Concrete object guard */
#define MVM_SPESH_PLUGIN_GUARD_TYPEOBJ  5   /* Type object guard */
#define MVM_SPESH_PLUGIN_GUARD_GETATTR  6   /* Gets an attribute for testing */

/* An individual guard. */
struct MVMSpeshPluginGuard {
    /* What kind of guard is this? */
    MVMuint16 kind;

    /* What arg should we read the value to test from? Up to the number of
     * callsite positionals will read from the args buffer; beyond that will
     * read from the buffer of results from a GETATTR guard (those are
     * appended in the order they happen). */
    MVMuint16 test_idx;

    /* If we fail this, how many nodes should we skip over? */
    MVMuint16 skip_on_fail;

    /* Union used depending on the kind of guard. */
    union {
        /* The result to return; used for RESULT guard. */
        MVMObject *result;
        /* The object literal to test against; used for OBJ guard. */
        MVMObject *object;
        /* The type to test against, used for TYPE guard. */
        MVMSTable *type;
        /* The attribute to load for further testing. */
        struct {
            MVMObject *class_handle;
            MVMString *name;
        } attr;
    } u;
};

/* Functions called from the interpreter as spesh plugins are encountered and
 * guards recorded. */
void MVM_spesh_plugin_register(MVMThreadContext *tc, MVMString *language,
    MVMString *name, MVMObject *plugin);
void MVM_spesh_plugin_resolve(MVMThreadContext *tc, MVMString *name, MVMRegister *result,
    MVMuint8 *op_addr, MVMuint8 *next_addr, MVMCallsite *callsite);
void MVM_spesh_plugin_resolve_spesh(MVMThreadContext *tc, MVMString *name, MVMRegister *result,
    MVMuint32 bytecode_offset, MVMStaticFrame *sf, MVMuint8 *next_addr, MVMCallsite *callsite);
void MVM_spesh_plugin_resolve_jit(MVMThreadContext *tc, MVMString *name, MVMRegister *result,
        MVMuint32 position, MVMStaticFrame *sf, MVMCallsite *callsite);
void MVM_spesh_plugin_addguard_type(MVMThreadContext *tc, MVMObject *guardee, MVMObject *type);
void MVM_spesh_plugin_addguard_concrete(MVMThreadContext *tc, MVMObject *guardee);
void MVM_spesh_plugin_addguard_typeobj(MVMThreadContext *tc, MVMObject *guardee);
void MVM_spesh_plugin_addguard_obj(MVMThreadContext *tc, MVMObject *guardee);
void MVM_spesh_plugin_addguard_notobj(MVMThreadContext *tc, MVMObject *guardee, MVMObject *not);
MVMObject * MVM_spesh_plugin_addguard_getattr(MVMThreadContext *tc, MVMObject *guardee,
    MVMObject *class_handle, MVMString *name);

/* Rewriting of spesh resolve instructions in the spesh graph. */
void MVM_spesh_plugin_rewrite_resolve(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
    MVMSpeshIns *ins, MVMuint32 bytecode_offset, MVMint32 guard_index);

/* Functions for dealing with spesh plugin state. */
void MVM_spesh_plugin_guard_list_mark(MVMThreadContext *tc, MVMSpeshPluginGuard *guards,
    MVMuint32 num_guards, MVMGCWorklist *worklist);
