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
#define MVM_CALLSTACK_DEFAULT_REGION_SIZE 131072

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

/* A bytecode frame, the MVMFrame being allocated inline on the callstack.
 * It is followed by space for the work area (registers) and the lexical
 * environment (also registers). The work area lives on the callstack no
 * matter if the frame ends up heap promoted; the environment will be
 * copied into a heap location for safety reasons upon promotion. */
#define MVM_CALLSTACK_RECORD_FRAME              2
struct MVMCallStackFrame {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The bytecode call frame record itself, inlined directly here. */
    MVMFrame frame;
};

/* A bytecode frame where the MVMFrame was allocated directly on the heap.
 * It is followed by space for the work area (registers). Unlike a frame on
 * the stack, it is not followed by an environment. */
#define MVM_CALLSTACK_RECORD_HEAP_FRAME         3
struct MVMCallStackHeapFrame {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* A pointer to the heap-allocated frame. */
    MVMFrame *frame;
};

/* A bytecode frame where the MVMFrame was allocated inline on the callstack,
 * but later promoted to the heap. The work registers still live directly
 * after it; the space for the environment remains allocated, but is not used
 * (it's evacuated to the heap). */
#define MVM_CALLSTACK_RECORD_PROMOTED_FRAME     4
struct MVMCallStackPromotedFrame {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    union {
        /* A pointer to the heap-allocated frame. */
        MVMFrame *frame;
        /* The now-unused MVMFrame, used to keep the size of this record
         * representative. Further, the alloc_work is needed for the sake
         * of knowing the record size. */
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

    /* The initial dispatcher (the one declared at the callsite). */
    MVMDispDefinition *initial_disp;

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

/* This record appears on the callstack before a frame, and indicates that
 * we wish to enact a dispatch resumption after paramter binding - either
 * only on failure, or always. */
#define MVM_CALLSTACK_RECORD_BIND_CONTROL       11
typedef enum {
    MVM_BIND_CONTROL_FRESH_FAIL, /* Record created in this state (failures only) or... */
    MVM_BIND_CONTROL_FRESH_ALL,  /* ...for both success and failure */
    MVM_BIND_CONTROL_FAILED,     /* If there's a bind failure it is set to this... */
    MVM_BIND_CONTROL_SUCCEEDED,  /* If there's a bind success it is set to this... */
    MVM_BIND_CONTROL_EXHAUSTED   /* ...but is only effective for one call, so ends up here */
} MVMBindControlState;
struct MVMCallStackBindControl {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The current state of the bind control record. */
    MVMBindControlState state;

    /* The flag to pass if we do resume upon a bind failure. */
    MVMRegister failure_flag;

    /* The flag to pass if we do resume upon a bind success. */
    MVMRegister success_flag;

    /* If we do resume, this is the inline cache entry pointer we hang the
     * resumption dispatch program off, along with the static frame it lives
     * in (needed for memory management). */
    MVMDispInlineCacheEntry **ice_ptr;
    MVMStaticFrame *sf;
};

/* When we make a "call" from C code into bytecode, and there are arguments,
 * this kind of record provides storage of them, and ensures they are marked.
 * The argument storage follows the record. */
#define MVM_CALLSTACK_RECORD_ARGS_FROM_C        12
struct MVMCallStackArgsFromC {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The arguments. */
    MVMArgs args;
};

/* When we perform a deoptimization and uninline a call that set up resume
 * init args, we need to evacuate those and the slot for state storage from
 * the "composite" frame we are breaking apart during the uninline. To ease
 * this process we have a distinct kind of call stack record; it would be
 * possible to reconstruct the dispatch run call stack record in a sufficiently
 * convincing way, but deopt is already difficult enough to reason about. */
#define MVM_CALLSTACK_RECORD_DEOPTED_RESUME_INIT 13
struct MVMCallStackDeoptedResumeInit {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The dispatch program with resume init args. */
    MVMDispProgram *dp;

    /* The particular dispatch resumption that we have the init args for
     * here. */
    MVMDispProgramResumption *dpr;

    /* A slot for the state for this resumption. */
    MVMObject *state;

    /* The arguments, allocated dynamically after this based on the size of
     * the callsite. Any slots in here that are from dispatch program
     * constants are junk and should not be looked at. */
    MVMRegister *args;
};

/* Record to indicate the boundary of a nested runloop. */
#define MVM_CALLSTACK_RECORD_NESTED_RUNLOOP 14
struct MVMCallStackNestedRunloop {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The frame to stop at during unwinding. */
    MVMFrame *cur_frame;
};

/* Sometimes we wish to take a special action after execution of a frame.
 * This is typically used when the VM needs to invoke something and then
 * take action based upon the result, to avoid creating nested runloops
 * (which would be a continuation barrier). The record has variable size; the
 * fixed part here is followed by some state as required by the particular
 * use of the mechanism. */
#define MVM_CALLSTACK_RECORD_SPECIAL_RETURN 15
struct MVMCallStackSpecialReturn {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* If we want to invoke a special handler upon a return, this function
     * pointer is set. */
    MVMSpecialReturn special_return;

    /* If we want to invoke a special handler upon unwinding, this function
     * pointer is set. */
    MVMSpecialReturn special_unwind;

    /* Function pointer to something that will GC mark the special return
     * data. */
    MVMSpecialReturnMark mark_data;

    /* The size of the special return data following this record. */
    size_t data_size;
};

/* Functions for working with the call stack. */
void MVM_callstack_init(MVMThreadContext *tc);
MVMCallStackRecord * MVM_callstack_allocate_nested_runloop(MVMThreadContext *tc);
MVMCallStackFrame * MVM_callstack_allocate_frame(MVMThreadContext *tc, MVMuint32 work_size,
        MVMuint32 env_size);
MVMCallStackHeapFrame * MVM_callstack_allocate_heap_frame(MVMThreadContext *tc,
        MVMuint32 work_size);
MVMint32 MVM_callstack_ensure_work_and_env_space(MVMThreadContext *tc, MVMuint32 needed_work,
        MVMuint32 needed_env);
MVM_PUBLIC void * MVM_callstack_allocate_special_return(MVMThreadContext *tc,
        MVMSpecialReturn special_return, MVMSpecialReturn special_unwind,
        MVMSpecialReturnMark mark_data, size_t data_size);
MVMCallStackDispatchRecord * MVM_callstack_allocate_dispatch_record(MVMThreadContext *tc);
MVMCallStackDispatchRun * MVM_callstack_allocate_dispatch_run(MVMThreadContext *tc,
        MVMuint32 num_temps);
MVMCallStackFlattening * MVM_callstack_allocate_flattening(MVMThreadContext *tc,
        MVMuint16 num_args, MVMuint16 num_pos);
MVMCallStackBindControl * MVM_callstack_allocate_bind_control_failure_only(MVMThreadContext *tc,
        MVMint64 failure_flag);
MVMCallStackBindControl * MVM_callstack_allocate_bind_control(MVMThreadContext *tc,
        MVMint64 flag, MVMint64 success_flag);
MVMCallStackArgsFromC * MVM_callstack_allocate_args_from_c(MVMThreadContext *tc,
        MVMCallsite *cs);
MVMCallStackDeoptedResumeInit * MVM_callstack_allocate_deopted_resume_init(
        MVMThreadContext *tc, MVMSpeshResumeInit *ri);
void MVM_callstack_new_continuation_region(MVMThreadContext *tc, MVMObject *tag);
MVMCallStackRegion * MVM_callstack_continuation_slice(MVMThreadContext *tc, MVMObject *tag,
        MVMActiveHandler **active_handlers);
void MVM_callstack_continuation_append(MVMThreadContext *tc, MVMCallStackRegion *first_region,
        MVMCallStackRecord *stack_top, MVMObject *update_tag);
MVMFrame * MVM_callstack_first_frame_from_region(MVMThreadContext *tc, MVMCallStackRegion *region);
MVMCallStackDispatchRecord * MVM_callstack_find_topmost_dispatch_recording(MVMThreadContext *tc);
MVMuint64 MVM_callstack_unwind_frame(MVMThreadContext *tc, MVMuint8 exceptional);
void MVM_callstack_unwind_to_frame(MVMThreadContext *tc, MVMuint8 exceptional);
void MVM_callstack_unwind_dispatch_record(MVMThreadContext *tc);
void MVM_callstack_unwind_dispatch_run(MVMThreadContext *tc);
void MVM_callstack_unwind_failed_dispatch_run(MVMThreadContext *tc);
void MVM_callstack_unwind_nested_runloop(MVMThreadContext *tc);
void MVM_callstack_mark_current_thread(MVMThreadContext *tc, MVMGCWorklist *worklist,
        MVMHeapSnapshotState *snapshot);
void MVM_callstack_mark_detached(MVMThreadContext *tc, MVMCallStackRecord *stack_top,
        MVMGCWorklist *worklist);
void MVM_callstack_free_detached_regions(MVMThreadContext *tc, MVMCallStackRegion *first_region,
        MVMCallStackRecord *stack_top);
void MVM_callstack_destroy(MVMThreadContext *tc);
MVM_STATIC_INLINE void *MVM_callstack_get_special_return_data(MVMThreadContext *tc,
        MVMCallStackRecord *record, MVMSpecialReturn special_return) {
    if (record->kind == MVM_CALLSTACK_RECORD_SPECIAL_RETURN &&
            ((MVMCallStackSpecialReturn*)record)->special_return == special_return) {
        return (char *)record + sizeof(MVMCallStackSpecialReturn);
    }
    return 0;
}

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

/* Create an iterator over bytecode frames and special return frames  on the
 * call stack. */
MVM_STATIC_INLINE void MVM_callstack_iter_frame_or_special_init(
        MVMThreadContext *tc, MVMCallStackIterator *iter,
        MVMCallStackRecord *start) {
    iter->start = start;
    iter->current = NULL;
    iter->filter = (1 << MVM_CALLSTACK_RECORD_FRAME |
                    1 << MVM_CALLSTACK_RECORD_HEAP_FRAME |
                    1 << MVM_CALLSTACK_RECORD_PROMOTED_FRAME |
                    1 << MVM_CALLSTACK_RECORD_DEOPT_FRAME |
                    1 << MVM_CALLSTACK_RECORD_SPECIAL_RETURN);
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

/* Create an iterator over call stack records that we may want to consider if
 * we are doing a resume. */
MVM_STATIC_INLINE void MVM_callstack_iter_resumeable_init(MVMThreadContext *tc,
        MVMCallStackIterator *iter, MVMCallStackRecord *start) {
    iter->start = start;
    iter->current = NULL;
    iter->filter = (1 << MVM_CALLSTACK_RECORD_DISPATCH_RECORDED |
                    1 << MVM_CALLSTACK_RECORD_DISPATCH_RUN |
                    1 << MVM_CALLSTACK_RECORD_FRAME |
                    1 << MVM_CALLSTACK_RECORD_HEAP_FRAME |
                    1 << MVM_CALLSTACK_RECORD_PROMOTED_FRAME |
                    1 << MVM_CALLSTACK_RECORD_DEOPT_FRAME |
                    1 << MVM_CALLSTACK_RECORD_BIND_CONTROL |
                    1 << MVM_CALLSTACK_RECORD_DEOPTED_RESUME_INIT);
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

MVM_STATIC_INLINE MVMCallStackRecord * MVM_callstack_prev_significant_record(
    MVMThreadContext *tc, MVMCallStackRecord *record) {
    MVMCallStackRecord *prev = record->prev;
    if (prev && prev->kind == MVM_CALLSTACK_RECORD_START_REGION)
        prev = prev->prev;
    return prev;
}

/* Migration to callstack-based special return in Rakudo extops. */
#define MVM_CALLSTACK_SPECIAL_RETURN 1
