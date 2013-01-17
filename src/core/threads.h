struct _MVMObject * MVM_thread_start(MVMThreadContext *tc, struct _MVMObject *invokee, struct _MVMObject *result_type);
void MVM_thread_join(MVMThreadContext *tc, struct _MVMObject *thread);
void MVM_thread_add_starting_threads_to_worklist(MVMThreadContext *tc, struct _MVMGCWorklist *worklist);