void MVM_gc_enter_from_allocator(MVMThreadContext *tc);
void MVM_gc_enter_from_interrupt(MVMThreadContext *tc);
void MVM_gc_mark_thread_blocked(MVMThreadContext *tc);
void MVM_gc_mark_thread_unblocked(MVMThreadContext *tc);
void MVM_gc_mark_thread_dying(MVMThreadContext *tc);

typedef enum {
    MVM_gc_stage_started = 0,
    MVM_gc_stage_finished = 1,
    MVM_gc_stage_initializing = 2
} MVM_gc_orchestation_stages;

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
    
    /* Stage state flag. See MVM_gc_orchestation_stages. */
    MVMuint32 stage;
} MVMGCOrchestration;
