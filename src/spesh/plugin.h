void MVM_spesh_plugin_register(MVMThreadContext *tc, MVMString *language,
    MVMString *name, MVMObject *plugin);
MVMObject * MVM_spesh_plugin_resolve(MVMThreadContext *tc, MVMString *name);
void MVM_spesh_plugin_addguard_type(MVMThreadContext *tc, MVMObject *guardee, MVMObject *type);
void MVM_spesh_plugin_addguard_concrete(MVMThreadContext *tc, MVMObject *guardee);
void MVM_spesh_plugin_addguard_typeobj(MVMThreadContext *tc, MVMObject *guardee);
void MVM_spesh_plugin_addguard_obj(MVMThreadContext *tc, MVMObject *guardee);
MVMObject * MVM_spesh_plugin_addguard_getattr(MVMThreadContext *tc, MVMObject *guardee,
    MVMObject *class_handle, MVMString *name);
