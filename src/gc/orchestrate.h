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

/* This structure tracks the orchestration of the GC runs. */
typedef struct _MVMGCOrchestration {
    /* The number of threads that vote for starting GC. */
    AO_t start_votes_remaining;
    
    /* The number of threads that still need to vote for considering GC done. */
    AO_t finish_votes_remaining;
    
    /* The number of threads that have yet to acknowledge the finish. */
    AO_t finish_ack_remaining;
    
    /* Stage state flag. See MVM_gc_orchestation_stages. */
    MVMuint32 stage;
    
    /* threads stolen */
    MVMThreadContext **stolen;
    MVMuint32 stolen_size;
    MVMuint32 stolen_count;
} MVMGCOrchestration;
