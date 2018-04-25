#include "moar.h"

/* The spesh plugin mechanism allows for extending spesh to be able to reason
 * about high-level language semantics that would be challenging to implement
 * inside of the specializer, perhaps because we don't communicate sufficient
 * information for it to do what is required. */

/* Registers a new spesh plugin. */
void MVM_spesh_plugin_register(MVMThreadContext *tc, MVMString *language,
        MVMString *name, MVMObject *plugin) {
}

/* Resolves a spesh plugin for the current HLL. */
MVMObject * MVM_spesh_plugin_resolve(MVMThreadContext *tc, MVMString *name) {
    MVM_exception_throw_adhoc(tc, "NYI");
}

/* Adds a guard that the guardee must have exactly the specified type. Will
 * throw if we are not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_type(MVMThreadContext *tc, MVMObject *guardee, MVMObject *type) {
}

/* Adds a guard that the guardee must be concrete. Will throw if we are not
 * currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_concrete(MVMThreadContext *tc, MVMObject *guardee) {
}

/* Adds a guard that the guardee must not be concrete. Will throw if we are
 * not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_typeobj(MVMThreadContext *tc, MVMObject *guardee) {
}

/* Adds a guard that the guardee must exactly match the provided object
 * literal. Will throw if we are not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_obj(MVMThreadContext *tc, MVMObject *guardee) {
}

/* Gets an attribute and adds that object to the set of objects that we may
 * guard against. Will throw if we are not currently inside of a spesh
 * plugin. */
MVMObject * MVM_spesh_plugin_addguard_getattr(MVMThreadContext *tc, MVMObject *guardee,
            MVMObject *class_handle, MVMString *name) {
    return MVM_repr_get_attr_o(tc, guardee, class_handle, name, MVM_NO_HINT);
}
