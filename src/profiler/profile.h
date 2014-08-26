void MVM_profile_start(MVMThreadContext *tc, MVMObject *config);
MVMObject * MVM_profile_end(MVMThreadContext *tc);
void MVM_profile_mark_data(MVMThreadContext *tc, MVMGCWorklist *worklist);
