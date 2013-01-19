struct _MVMObject * MVM_thread_start(MVMThreadContext *tc, struct _MVMObject *invokee, struct _MVMObject *result_type);
void MVM_thread_join(MVMThreadContext *tc, struct _MVMObject *thread);
void MVM_thread_cleanup_threads_list(MVMThreadContext *tc, struct _MVMThread **head);