void MVM_gc_enter_from_allocator(MVMThreadContext *tc);
void MVM_gc_enter_from_interrupt(MVMThreadContext *tc);
void MVM_gc_mark_thread_blocked(MVMThreadContext *tc);
void MVM_gc_mark_thread_unblocked(MVMThreadContext *tc);

/* This structure tracks the orchestration of a given GC run. */
typedef struct _MVMGCOrchestration {    
    /* The number of threads that we expect to join in with GC. */
    MVMuint32 expected_gc_threads;

    /* The number of threads that vote for starting GC. */
    MVMuint32 start_votes;
    
    /* The number of threads that vote for considering GC done. */
    MVMuint32 finish_votes;
    
    /* The number of threads that have yet to acknowledge the finish. */
    MVMuint32 finish_ack_remaining;
    
    /* Stage state flag. When it equals 1, it indicates the coordinator
     * decided this GC run is finished. When 2, it is being initialized.
     * 0 means not yet finished. */
    void *stage;
} MVMGCOrchestration;
