MVMObject * MVM_coroutine_create(MVMThreadContext *tc, MVMObject *code);
void MVM_coroutine_yield(MVMThreadContext *tc,  MVMObject *args);
void MVM_coroutine_resume(MVMThreadContext *tc, MVMObject *code, MVMObject *args, MVMRegister *res_reg);

void MVM_coroutine_free_tags(MVMThreadContext *tc, MVMFrame *frame);
