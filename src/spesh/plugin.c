#include "moar.h"

/* The spesh plugin mechanism allows for extending spesh to be able to reason
 * about high-level language semantics that would be challenging to implement
 * inside of the specializer, perhaps because we don't communicate sufficient
 * information for it to do what is required. */

/* Registers a new spesh plugin. */
void MVM_spesh_plugin_register(MVMThreadContext *tc, MVMString *language,
        MVMString *name, MVMObject *plugin) {
    MVMHLLConfig *hll = MVM_hll_get_config_for(tc, language);
    uv_mutex_lock(&tc->instance->mutex_hllconfigs);
    if (!hll->spesh_plugins)
        hll->spesh_plugins = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
    MVM_repr_bind_key_o(tc, hll->spesh_plugins, name, plugin);
    uv_mutex_unlock(&tc->instance->mutex_hllconfigs);
}

/* Resolves a spesh plugin for the current HLL. */
MVMObject * MVM_spesh_plugin_resolve(MVMThreadContext *tc, MVMString *name) {
    MVMHLLConfig *hll = MVM_hll_current(tc);
    MVMObject *resolved = NULL;
    uv_mutex_lock(&tc->instance->mutex_hllconfigs);
    if (hll->spesh_plugins)
        resolved = MVM_repr_at_key_o(tc, hll->spesh_plugins, name);
    uv_mutex_unlock(&tc->instance->mutex_hllconfigs);
    if (MVM_is_null(tc, resolved)) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "No such spesh plugin '%s' for current language",
            c_name);
    }
    return resolved;
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
