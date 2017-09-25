void MVM_profile_dump_instrumented_data(MVMThreadContext *tc);
void MVM_profile_instrument(MVMThreadContext *tc, MVMStaticFrame *sf);
void MVM_profile_ensure_uninstrumented(MVMThreadContext *tc, MVMStaticFrame *sf);
void MVM_profile_instrumented_start(MVMThreadContext *tc, MVMObject *config);
MVMObject * MVM_profile_instrumented_end(MVMThreadContext *tc);
void MVM_profile_instrumented_mark_data(MVMThreadContext *tc, MVMGCWorklist *worklist);
