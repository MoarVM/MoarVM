/* The call stack consists of a series of regions, which each contain records.
 * These records represent the things that are in dynamic scope. This includes
 * bytecode-level call frames, dispatcher record state, dispatcher program
 * evaluation state, continuation tags, and state associated with argument
 * flattening. There is always a "start of segment" record, which we use to
 * know when we are moving back to a prior segment. */

/* A region of the call stack. */
struct MVMCallStackRegion {
    /* Next call stack region, which we start allocating in if this one is
     * full. NULL if none has been allocated yet. */
    MVMCallStackRegion *next;

    /* Previous call stack region, if any. */
    MVMCallStackRegion *prev;

    /* The start of the memory area of the region. */
    char *start;

    /* The place we'll allocate the next frame. */
    char *alloc;

    /* The end of the allocatable region. */
    char *alloc_limit;

#if MVM_PTR_SIZE == 4
    MVMuint32 thingy_to_ensure_8_byte_alignment;
#endif
};

/* The default size of a call stack region. */
#define MVM_CALLSTACK_REGION_SIZE 131072

/*
 * We have various kinds of call stack records. They all have a common
 * prefix, which we use to know what we have, and also to find the prior
 * frame.
 */
struct MVMCallStackRecord {
    /* The previous call stack record; NULL if this is the first frame
     * of the segment. */
    MVMCallStackRecord *prev;

    /* The kind of record this is (constants defined below with each record
     * type). */
    MVMuint8 kind;

    /* Deoptimization of the whole call stack proceeds lazily, by marking a
     * frame that needs deopt when it's unwound to with the deopt kind (in
     * the kind field). In that case, this field holds the frame kind that
     * we started out with. */
    MVMuint8 orig_kind;
};

/* The start of a callstack as a whole, for seeing when we've reached the
 * last frame of it. */
#define MVM_CALLSTACK_RECORD_START              0
struct MVMCallStackStart {
    /* Commonalities of all records. */
    MVMCallStackRecord common;
};

/* The start of a callstack region, used so we can keep the current region
 * state in sync when we unwind. */
#define MVM_CALLSTACK_RECORD_START_REGION       1
struct MVMCallStackRegionStart {
    /* Commonalities of all records. */
    MVMCallStackRecord common;
};

/* A bytecode frame, the MVMFrame being allocated inline on the callstack. */
#define MVM_CALLSTACK_RECORD_FRAME              2
struct MVMCallStackFrame {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The bytecode call frame record itself, inlined directly here. */
    MVMFrame frame;
};

/* A bytecode frame where the MVMFrame was allocated directly on the heap.  */
#define MVM_CALLSTACK_RECORD_HEAP_FRAME         3
struct MVMCallStackHeapFrame {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* A pointer to the heap-allocated frame. */
    MVMFrame *frame;
};

/* A bytecode frame where the MVMFrame was allocated inline on the callstack,
 * but later promoted to the heap. */
#define MVM_CALLSTACK_RECORD_PROMOTED_FRAME     4
struct MVMCallStackPromotedFrame {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    union {
        /* A pointer to the heap-allocated frame. */
        MVMFrame *frame;
        /* The now-unused MVMFrame, used to keep the size of this record
         * representative. */
        MVMFrame dead;
    };
};

/* A frame that needs lazily deoptimizing when we return into it. The orig_kind
 * then indicates what kind of thing it is. */
#define MVM_CALLSTACK_RECORD_DEOPT_FRAME        5

/* A continuation tag record, used to find the base of a continuation. This
 * is also a kind of callstack region start. */
#define MVM_CALLSTACK_RECORD_CONTINUATION_TAG   6
struct MVMCallStackContinuationTag {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The tag itself. */
    MVMObject *tag;

    /* The active exception handler at the point of the reset. */
    MVMActiveHandler *active_handlers;
};

/* An argument flattening record, used to hold the results of flattening a
 * dispatch's arguments. The layout is:
 * 1. This struct.
 * 2. Callsite flags, rounded up to nearest 8 bytes.
 * 3. Arguments buffer.
 * Thus this is a record of dynamic length. */
#define MVM_CALLSTACK_RECORD_FLATTENING         7
struct MVMCallStackFlattening {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* A callsite that we produce, flattened in here to avoid a malloc. We
     * may actually end up not using it, if it matches an interned one. The
     * arg flags are allocated on the end of this record. */
    MVMCallsite produced_cs;

    /* The args for the dispatch. The callsite is either a pointer to
     * produced_cs above, or to an interned callsite. The map is the
     * identity mapping. The source points to an area at the end of
     * this record. */
    MVMArgs arg_info;
};

/* A dispatch recording phase record, when we're running the dispatch
 * callback and constructing a dispatch program. May hold dispatch
 * state for a resumption. */
#define MVM_CALLSTACK_RECORD_DISPATCH_RECORD    8
struct MVMCallStackDispatchRecord {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The initial arguments to the dispatch. */
    MVMArgs arg_info;

    /* What was the original expected return type? Used because we set it to
     * void during the running of dispatch callbacks. */
    MVMReturnType orig_return_type;

    /* The current recording state. Keeps track of the captures
     * derived and guards established. */
    MVMDispProgramRecording rec;

    /* The current dispatcher that we're running. */
    MVMDispDefinition *current_disp;

    /* Register holding the current capture. Starts out as the initial capture
     * and then, in the case we delegate, becomes the one sent to the delegate
     * dispatcher. */
    MVMRegister current_capture;

    /* The outcome of the dispatch. */
    MVMDispProgramOutcome outcome;

    /* The inline cache entry to transition. */
    MVMDispInlineCacheEntry **ic_entry_ptr;

    /* The initial inline cache record we had prior to dispatch (cannot be
     * safely obtaiend from ic_entry_ptr, since another thread may have done
     * a competing transition). */
    MVMDispInlineCacheEntry *ic_entry;

    /* The static frame owning the inline cache (needed for upholding the
     * inter-gen GC invariant). */
    MVMStaticFrame *update_sf;

    /* The produced dispatch program. Only set if it resumable and so we may
     * need to refer to it; NULL otherwise. */
    MVMDispProgram *produced_dp;

    /* If there is a produced dispatch program here, was it installed? If not,
     * we need to clear it up as we unwind this frame.  */
    MVMuint8 produced_dp_installed;

    /* Temporaries as the dispatch program would write them, but only for the
     * case where they are used for making resume init state available. NULL
     * otherwise. */
    MVMRegister *temps;

    /* If this dispatch gets resumed, this will hold the dispatch state (the
     * "moving part" that tracks, for example, where we are in a deferral
     * walk through the MRO). We null out the definition at record creation;
     * if that is null, the rest of this is to be considered invalid. */
    MVMDispResumptionState resumption_state;
};

/* A dispatch record frame is turned into this once the dispatch has already
 * been recorded and its outcome put into effect. In this situation, we simply
 * need to remove it from the call stack. */
#define MVM_CALLSTACK_RECORD_DISPATCH_RECORDED  9

/* A dispatch program run record, when we're evaluating a dispatch
 * program. May hold dispatch state for a resumption. */
#define MVM_CALLSTACK_RECORD_DISPATCH_RUN       10
struct MVMCallStackDispatchRun {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The initial arguments to the dispatch. */
    MVMArgs arg_info;

    /* The number of temporaries allocated. */
    MVMuint32 num_temps;

    /* Temporaries (actually allocated after this record, which is variable
     * length). */
    MVMRegister *temps;

    /* If we are running a resumption of an existing dispatch, this is the
     * resumption data. */
    MVMDispResumptionData resumption_data;

    /* If this dispatch gets resumed, this will hold the dispatch state (the
     * "moving part" that tracks, for example, where we are in a deferral
     * walk through the MRO). We null out the definition at record creation;
     * if that is null, the rest of this is to be considered invalid. */
    MVMDispResumptionState resumption_state;

    /* The resumption nesting level, for when we have multiple resumable
     * dispatchers working within a single dispatch. */
    MVMuint32 resumption_level;

    /* The dispatch program that was chosen (used to know how to mark the
     * temporaries, if needed). */
    MVMDispProgram *chosen_dp;

    /* If we rewrote the args into the temporaries, the callsite to mark
     * them with. Junk if the chosen_dp has not got any arg temporaries. */
    MVMCallsite *temp_mark_callsite;
};

/* This record appears on the callstack before a frame, and indicates that,
 * should the frame have a bind failure, we wish to enact a dispatch
 * resumption. */
#define MVM_CALLSTACK_RECORD_BIND_FAILURE       11
typedef enum {
    MVM_BIND_FAILURE_FRESH,      /* Record created in this state */
    MVM_BIND_FAILURE_FAILED,     /* If there's a bind failure it is set to this... */
    MVM_BIND_FAILURE_EXHAUSTED   /* ...but is only effective for one call, so ends up here */
} MVMBindFailureState;
struct MVMCallStackBindFailure {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The current state of the bind failure record. */
    MVMBindFailureState state;

    /* The flag to pass if we do resume upon a bind failure. */
    MVMRegister flag;

    /* If we do fail, this is the inline cache entry pointer we hang the
     * resumption dispatch program off, along with the static frame it
     * lives in (needed for memory management). */
    MVMDispInlineCacheEntry **ice_ptr;
    MVMStaticFrame *sf;
};

/* Functions for working with the call stack. */
void MVM_callstack_init(MVMThreadContext *tc);
MVMCallStackFrame * MVM_callstack_allocate_frame(MVMThreadContext *tc);
MVMCallStackHeapFrame * MVM_callstack_allocate_heap_frame(MVMThreadContext *tc);
MVMCallStackDispatchRecord * MVM_callstack_allocate_dispatch_record(MVMThreadContext *tc);
MVMCallStackDispatchRun * MVM_callstack_allocate_dispatch_run(MVMThreadContext *tc,
        MVMuint32 num_temps);
MVMCallStackFlattening * MVM_callstack_allocate_flattening(MVMThreadContext *tc,
        MVMuint16 num_args, MVMuint16 num_pos);
MVMCallStackBindFailure * MVM_callstack_allocate_bind_failure(MVMThreadContext *tc,
        MVMint64 flag);
void MVM_callstack_new_continuation_region(MVMThreadContext *tc, MVMObject *tag);
MVMCallStackRegion * MVM_callstack_continuation_slice(MVMThreadContext *tc, MVMObject *tag,
        MVMActiveHandler **active_handlers);
void MVM_callstack_continuation_append(MVMThreadContext *tc, MVMCallStackRegion *first_region,
        MVMCallStackRecord *stack_top, MVMObject *update_tag);
MVMFrame * MVM_callstack_first_frame_in_region(MVMThreadContext *tc, MVMCallStackRegion *region);
MVMCallStackDispatchRecord * MVM_callstack_find_topmost_dispatch_recording(MVMThreadContext *tc);
MVMFrame * MVM_callstack_unwind_frame(MVMThreadContext *tc, MVMuint8 exceptional, MVMuint32 *thunked);
void MVM_callstack_unwind_dispatch_record(MVMThreadContext *tc, MVMuint32 *thunked);
void MVM_callstack_unwind_dispatch_run(MVMThreadContext *tc);
void MVM_callstack_unwind_failed_dispatch_run(MVMThreadContext *tc);
void MVM_callstack_mark_current_thread(MVMThreadContext *tc, MVMGCWorklist *worklist,
        MVMHeapSnapshotState *snapshot);
void MVM_callstack_mark_detached(MVMThreadContext *tc, MVMCallStackRecord *stack_top,
        MVMGCWorklist *worklist);
void MVM_callstack_free_detached_regions(MVMThreadContext *tc, MVMCallStackRegion *first_region,
        MVMCallStackRecord *stack_top);
void MVM_callstack_destroy(MVMThreadContext *tc);
MVM_STATIC_INLINE MVMuint8 MVM_callstack_kind_ignoring_deopt(MVMCallStackRecord *record) {
    return record->kind == MVM_CALLSTACK_RECORD_DEOPT_FRAME ? record->orig_kind : record->kind;
}
MVM_STATIC_INLINE MVMFrame * MVM_callstack_record_to_frame(MVMCallStackRecord *record) {
    switch (MVM_callstack_kind_ignoring_deopt(record)) {
        case MVM_CALLSTACK_RECORD_FRAME:
            return &(((MVMCallStackFrame *)record)->frame);
        case MVM_CALLSTACK_RECORD_HEAP_FRAME:
            return ((MVMCallStackHeapFrame *)record)->frame;
        case MVM_CALLSTACK_RECORD_PROMOTED_FRAME:
            return ((MVMCallStackPromotedFrame *)record)->frame;
        default:
            MVM_panic(1, "No frame at top of callstack");
    }
}
MVM_STATIC_INLINE MVMFrame * MVM_callstack_current_frame(MVMThreadContext *tc) {
    return MVM_callstack_record_to_frame(tc->stack_top);
}

/* Call stack iterator. */
struct MVMCallStackIterator {
    MVMCallStackRecord *start;
    MVMCallStackRecord *current;
    MVMuint64 filter;
};

/* Create an iterator over a certain kind of record. */
MVM_STATIC_INLINE void MVM_callstack_iter_one_kind_init(MVMThreadContext *tc,
        MVMCallStackIterator *iter, MVMCallStackRecord *start, MVMuint8 kind) {
    iter->start = start;
    iter->current = NULL;
    iter->filter = 1 << kind;
}

/* Create an iterator over bytecode frames on the call stack. */
MVM_STATIC_INLINE void MVM_callstack_iter_frame_init(MVMThreadContext *tc,
        MVMCallStackIterator *iter, MVMCallStackRecord *start) {
    iter->start = start;
    iter->current = NULL;
    iter->filter = (1 << MVM_CALLSTACK_RECORD_FRAME |
                    1 << MVM_CALLSTACK_RECORD_HEAP_FRAME |
                    1 << MVM_CALLSTACK_RECORD_PROMOTED_FRAME |
                    1 << MVM_CALLSTACK_RECORD_DEOPT_FRAME);
}

/* Create an iterator over dispatch frames on the call stack. */
MVM_STATIC_INLINE void MVM_callstack_iter_dispatch_init(MVMThreadContext *tc,
        MVMCallStackIterator *iter, MVMCallStackRecord *start) {
    iter->start = start;
    iter->current = NULL;
    iter->filter = (1 << MVM_CALLSTACK_RECORD_DISPATCH_RECORDED |
                    1 << MVM_CALLSTACK_RECORD_DISPATCH_RUN);
}

/* Move to the next applicable record. Should be called before reading a current
 * record. Calling it again after it has returned a flase value is undefined. */
MVM_STATIC_INLINE MVMint32 MVM_callstack_iter_move_next(MVMThreadContext *tc,
        MVMCallStackIterator *iter) {
    iter->current = iter->current ? iter->current->prev : iter->start;
    while (iter->current && !(iter->filter & (1 << iter->current->kind)))
        iter->current = iter->current->prev;
    return iter->current != NULL;
}

/* Get the current item in the iteration. */
MVM_STATIC_INLINE MVMCallStackRecord * MVM_callstack_iter_current(MVMThreadContext *tc,
        MVMCallStackIterator *iter) {
    return iter->current;
}

/* Get the current frame in the iteration (illegal if we are not only walking
 * frames). */
MVM_STATIC_INLINE MVMFrame * MVM_callstack_iter_current_frame(MVMThreadContext *tc,
        MVMCallStackIterator *iter) {
    return MVM_callstack_record_to_frame(iter->current);
}
