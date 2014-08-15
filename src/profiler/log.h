/* Per-thread profiling data. */
struct MVMProfileThreadData {
    /* The root of the call graph. */
    MVMProfileCallNode *call_graph;

    /* The current call graph node we're in. */
    MVMProfileCallNode *current_call;

    /* The time we started profiling. */
    MVMuint64 start_time;

    /* The time we finished profiling, if we got there already. */
    MVMuint64 end_time;

    /* Garbage collection time measurements. */
    MVMProfileGC *gcs;
    MVMuint32 num_gcs;
    MVMuint32 alloc_gcs;

    /* Amount of time spent in spesh. */
    MVMuint64 spesh_time;

    /* Current spesh work start time, if any. */
    MVMuint64 cur_spesh_start_time;

    /* Current GC start time, if any. */
    MVMuint64 cur_gc_start_time;
};

/* Information collected about a GC run. */
struct MVMProfileGC {
    /* How long the collection took. */
    MVMuint64 time;

    /* Was it a full collection? */
    MVMuint32 full;

    /* Nursery statistics. */
    MVMuint32 cleared_bytes;
    MVMuint32 retained_bytes;
    MVMuint32 promoted_bytes;
};

/* Call graph node, which is kept per thread. */
struct MVMProfileCallNode {
    /* The frame this data is for. */ 
    MVMStaticFrame *sf;

    /* The timestamp when we entered the node. */
    MVMuint64 cur_entry_time;

    /* Time we should skip since cur_entry_time because execution was
     * suspended due to GC or spesh. */
    MVMuint64 cur_skip_time;

    /* The node in the profiling call graph that we came from. */
    MVMProfileCallNode *pred;

    /* Successor nodes so far. */
    MVMProfileCallNode **succ;

    /* Number of successors we have, and have allocated space for. */
    MVMuint32 num_succ;
    MVMuint32 alloc_succ;

    /* Allocations of different types, and the number of allocation
     * counts we have so far. */
    MVMProfileAllocationCount *alloc;
    MVMuint32 num_alloc;
    MVMuint32 alloc_alloc;

    /* The total inclusive time so far spent in this node. */
    MVMuint64 total_time;

    /* The total number of times this node was entered. */
    MVMuint64 total_entries;

    /* Entries that were to specialized bytecode. */
    MVMuint64 specialized_entries;

    /* Entries that were inlined. */
    MVMuint64 inlined_entries;

    /* Entries that were to JITted code. */
    MVMuint64 jit_entries;

    /* Number of times OSR took place. */
    MVMuint64 osr_count;

    /* Number of times deopt_one happened. */
    MVMuint64 deopt_one_count;

    /* Number of times deopt_all happened. */
    MVMuint64 deopt_all_count;
};

/* Allocation counts for a call node. */
struct MVMProfileAllocationCount {
    /* The type we're counting allocations of. */
    MVMObject *type;

    /* The number of allocations we've counted. */
    MVMuint64 allocations;
};

/* Ways we might enter a frame. */
#define MVM_PROFILE_ENTER_NORMAL        0
#define MVM_PROFILE_ENTER_SPESH         1
#define MVM_PROFILE_ENTER_SPESH_INLINE  2
#define MVM_PROFILE_ENTER_JIT           3
#define MVM_PROFILE_ENTER_JIT_INLINE    4

/* Logging functions. */
void MVM_profile_log_enter(MVMThreadContext *tc, MVMStaticFrame *sf, MVMuint64 mode);
void MVM_profile_log_exit(MVMThreadContext *tc);
void MVM_profile_log_allocated(MVMThreadContext *tc, MVMObject *obj);
void MVM_profiler_log_gc_start(MVMThreadContext *tc, MVMuint32 full);
void MVM_profiler_log_gc_end(MVMThreadContext *tc);
