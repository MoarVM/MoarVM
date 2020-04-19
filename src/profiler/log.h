/* Per-thread profiling data. */
struct MVMProfileThreadData {
    /* The current call graph node we're in. */
    MVMProfileCallNode *current_call;

    /* Cache whether moar currently has confprogs enabled */
    MVMuint8 is_dynamic_confprog_installed;
    MVMuint8 is_static_confprog_installed;

    /* The thread ID of the thread responsible for spawning this thread. */
    MVMuint32 parent_thread_id;

    /* The root of the call graph. */
    MVMProfileCallNode *call_graph;

    /* The time we started profiling. */
    MVMuint64 start_time;

    /* The time we finished profiling, if we got there already. */
    MVMuint64 end_time;

    /* The next two arrays are there to remove the need for the GC
     * to walk the entire call graph every time.
     */

    /* Replaces static frame pointers in call nodes with an index into
     * this array */
    MVM_VECTOR_DECL(MVMStaticFrame *, staticframe_array);

    /* Replaces type pointers in call nodes allocation entries with an
     * index into this array */
    MVM_VECTOR_DECL(MVMObject *, type_array);

    /* When a confprog is deciding what frames to enter, exiting a frame
     * when there's no node on the call stack can either be a run-time error,
     * or it can be completely normal. To differentiate, we have to count the
     * number of times we entered a frame without setting up a call graph node.
     */
    MVMuint32 non_calltree_depth;

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

    /* We would like to split "promoted bytes" into managed and
     * unmanaged, so that subtracting it from the nursery size
     * doesn't give us negative values. */
    MVMuint64 gc_promoted_unmanaged_bytes;

    /* We have to make sure to not count the newest allocation infinitely
     * often if there's a conditionally-allocating operation (like getlex)
     * that gets called multiple times with no actual allocations in between */
    MVMObject *last_counted_allocation;

    /* Used to pass generated data structure from the gc-orchestrated
     * dumping function back to the dump function that ends the profile */
    MVMObject *collected_data;
};

/* Information collected about a GC run. */
struct MVMProfileGC {
    /* How long the collection took. */
    MVMuint64 time;

    /* When, relative to program start, did this GC take place? */
    MVMuint64 abstime;

    /* Was it a full collection? */
    MVMuint16 full;

    /* Was this thread responsible? */
    MVMuint16 responsible;

    /* Which GC run does this belong to?
     * (Good to know in multithreaded situations where
     * some threads have their work stolen) */
    AO_t gc_seq_num;

    /* Nursery statistics. */
    MVMuint32 cleared_bytes;
    MVMuint32 retained_bytes;
    MVMuint32 promoted_bytes;

    MVMuint64 promoted_unmanaged_bytes;

    /* Inter-generation links count */
    MVMuint32 num_gen2roots;

    /* sum of num_gen2roots of all TCs that had work stolen by
     * this thread */
    MVMuint32 num_stolen_gen2roots;

    MVMProfileDeallocationCount *deallocs;
    MVMuint32 num_dealloc;
    MVMuint32 alloc_dealloc; /* haha */
};

/* Call graph node, which is kept per thread. */
struct MVMProfileCallNode {
    /* The frame this data is for.
     * If this CallNode is for a native call, this is NULL. */
    MVMuint32 sf_idx;
    /* The timestamp when we entered the node. */
    MVMuint64 cur_entry_time;

    /* Time we should skip since cur_entry_time because execution was
     * suspended due to GC or spesh. */
    MVMuint64 cur_skip_time;

    /* Entry mode, persisted for the sake of continuations. */
    MVMuint64 entry_mode;

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

    /* If the static frame is NULL, we're collecting data on a native call */
    char *native_target_name;

    /* When was this node first entered, ever? */
    MVMuint64 first_entry_time;

};

/* Allocation counts for a call node. */
struct MVMProfileAllocationCount {
    /* The type we're counting allocations of. */
    MVMuint32 type_idx;

    /* The number of allocations we've counted. */
    /* a) in regularly interpreted code */
    MVMuint64 allocations_interp;

    /* b) in spesh'd code */
    MVMuint64 allocations_spesh;

    /* c) in jitted code */
    MVMuint64 allocations_jit;

    /* The number of allocations elimianted thanks to scalar replacement. */
    MVMuint64 scalar_replaced;
};

struct MVMProfileDeallocationCount {
    MVMObject *type;

    /* How often was this type freed from the nursery with
     * the "seen in nursery" flag not set? */
    MVMuint32 deallocs_nursery_fresh;

    /* How often was this type freed from the nursery with
     * the "seen in nursery" flag set? */
    MVMuint32 deallocs_nursery_seen;

    /* How often was this type freed in the old generation? */
    MVMuint32 deallocs_gen2;
};

/* When a continuation is taken, we attach one of these to it. It carries the
 * data needed to restore profiler state if the continuation is invoked. */
struct MVMProfileContinuationData {
    /* List of static frames we should restore, in reverse order. */
    MVMStaticFrame **sfs;

    /* Entry modes to restore also. */
    MVMuint64 *modes;

    /* Number of static frames in the list. */
    MVMuint64 num_sfs;
};

/* Ways we might enter a frame. */
#define MVM_PROFILE_ENTER_NORMAL        0
#define MVM_PROFILE_ENTER_SPESH         1
#define MVM_PROFILE_ENTER_SPESH_INLINE  2
#define MVM_PROFILE_ENTER_JIT           3
#define MVM_PROFILE_ENTER_JIT_INLINE    4

/* Logging functions. */
void MVM_profile_log_enter(MVMThreadContext *tc, MVMStaticFrame *sf, MVMuint64 mode);
void MVM_profile_log_enter_native(MVMThreadContext *tc, MVMObject *nativecallsite);
void MVM_profile_log_exit(MVMThreadContext *tc);
void MVM_profile_log_unwind(MVMThreadContext *tc);
MVMProfileContinuationData * MVM_profile_log_continuation_control(MVMThreadContext *tc, const MVMFrame *root_frame);
void MVM_profile_log_continuation_invoke(MVMThreadContext *tc, const MVMProfileContinuationData *cd);
void MVM_profile_log_thread_created(MVMThreadContext *tc, MVMThreadContext *child_tc);
void MVM_profile_log_allocated(MVMThreadContext *tc, MVMObject *obj);
void MVM_profile_log_scalar_replaced(MVMThreadContext *tc, MVMSTable *st);
void MVM_profiler_log_gc_start(MVMThreadContext *tc, MVMuint32 full, MVMuint32 this_thread_responsible);
void MVM_profiler_log_gc_end(MVMThreadContext *tc);
void MVM_profiler_log_gen2_roots(MVMThreadContext *tc, MVMuint64 amount, MVMThreadContext *other);
void MVM_profiler_log_gc_deallocate(MVMThreadContext *tc, MVMObject *object);
void MVM_profiler_log_unmanaged_data_promoted(MVMThreadContext *tc, MVMuint64 amount);
void MVM_profiler_log_spesh_start(MVMThreadContext *tc);
void MVM_profiler_log_spesh_end(MVMThreadContext *tc);
void MVM_profiler_log_osr(MVMThreadContext *tc, MVMuint64 jitted);
void MVM_profiler_log_deopt_one(MVMThreadContext *tc);
void MVM_profiler_log_deopt_all(MVMThreadContext *tc);
