/* Data structure that hangs off a static frame's spesh state, holding the
 * guard tree at each applicable bytecode position. It is allocated using the
 * FSA, and freed using the safepointing mechanism; this allows, providing a
 * single read is done before operating, for safe concurrent use. Updated
 * versions are installed using atomic operations. */
struct MVMSpeshPluginState {
    /* Array of position states. Held in bytecode index order, which
     * allows for binary searching. */
    MVMSpeshPluginPosition *positions;

    /* Number of bytecode positions we have state at. */
    MVMuint32 num_positions;
};

/* Pairing of instruction position with the guard set at that position. */
struct MVMSpeshPluginPosition {
    MVMSpeshPluginGuardSet *guard_set;
    MVMuint32 bytecode_position;
};

/* The guard set held at a particular position. */
struct MVMSpeshPluginGuardSet {
    /* TODO Actually implement the guard structure. For now, just always
     * return the same thing once resolved. */
    MVMObject *resolved;
};

/* Functions called from the interpreter as spesh plugins are encountered and
 * guards recorded. */
void MVM_spesh_plugin_register(MVMThreadContext *tc, MVMString *language,
    MVMString *name, MVMObject *plugin);
void MVM_spesh_plugin_resolve(MVMThreadContext *tc, MVMString *name, MVMRegister *result,
    MVMuint8 *op_addr, MVMuint8 *next_addr, MVMCallsite *callsite);
void MVM_spesh_plugin_addguard_type(MVMThreadContext *tc, MVMObject *guardee, MVMObject *type);
void MVM_spesh_plugin_addguard_concrete(MVMThreadContext *tc, MVMObject *guardee);
void MVM_spesh_plugin_addguard_typeobj(MVMThreadContext *tc, MVMObject *guardee);
void MVM_spesh_plugin_addguard_obj(MVMThreadContext *tc, MVMObject *guardee);
MVMObject * MVM_spesh_plugin_addguard_getattr(MVMThreadContext *tc, MVMObject *guardee,
    MVMObject *class_handle, MVMString *name);

/* Functions for dealing with spesh plugin state. */
void MVM_spesh_plugin_state_mark(MVMThreadContext *tc, MVMSpeshPluginState *ps, MVMGCWorklist *worklist);
void MVM_spesh_plugin_state_free(MVMThreadContext *tc, MVMSpeshPluginState *ps);
