MVMObject * MVM_thread_start(MVMThreadContext *tc, MVMObject *invokee, MVMObject *result_type);
void MVM_thread_join(MVMThreadContext *tc, MVMObject *thread);
void MVM_thread_cleanup_threads_list(MVMThreadContext *tc, MVMThread **head);