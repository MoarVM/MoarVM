void MVM_nativecall_build(MVMThreadContext *tc, MVMObject *site, MVMString *lib,
    MVMString *sym, MVMString *conv, MVMObject *arg_spec, MVMObject *ret_spec);
MVMObject * MVM_nativecall_invoke(MVMThreadContext *tc, MVMObject *ret_type,
    MVMObject *site, MVMObject *args);
void MVM_nativecall_refresh(MVMThreadContext *tc, MVMObject *cthingy);
