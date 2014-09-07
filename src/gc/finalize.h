void MVM_gc_finalize_set(MVMThreadContext *tc, MVMObject *type, MVMint64 finalize);
void MVM_gc_finalize_add_to_queue(MVMThreadContext *tc, MVMObject *obj);
void MVM_finalize_walk_queues(MVMThreadContext *tc, MVMuint8 gen);
