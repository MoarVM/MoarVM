void MVM_gc_enter_from_allocator(MVMThreadContext *tc);
void MVM_gc_enter_from_interrupt(MVMThreadContext *tc);
void MVM_gc_mark_thread_blocked(MVMThreadContext *tc);
void MVM_gc_mark_thread_unblocked(MVMThreadContext *tc);

struct MVMWorkThread {
    MVMThreadContext *tc;
    void             *limit;
};
