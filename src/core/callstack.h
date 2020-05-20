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
 * dispatch's arguments. A pointer into this is then used as the args source
 * in the following dispatch. */
#define MVM_CALLSTACK_RECORD_FLATTENING         7
struct MVMCallStackFlattening {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* TODO */
};

/* A dispatch recording phase record, when we're running the dispatch
 * callback and constructing a dispatch program. May hold dispatch
 * state for a resumption. */
#define MVM_CALLSTACK_RECORD_DISPATCH_RECORD    8
struct MVMCallStackDispatchRecord {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The argument capture going into the dispatch (always an object, but
     * stored this way to let this address be safely used as an argument
     * buffer. */
    MVMRegister initial_capture;

    /* An MVMArray of derived captures, produced during the dispatch
     * process. */
    MVMObject *derived_captures;

    /* The outcome of the dispatch. */
    MVMDispProgramOutcome outcome;
    /* TODO */
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

    /* The outcome of the dispatch. */
    MVMDispProgramOutcome outcome;
    /* TODO */
};

/* Functions for working with the call stack. */
void MVM_callstack_init(MVMThreadContext *tc);
MVMCallStackFrame * MVM_callstack_allocate_frame(MVMThreadContext *tc);
MVMCallStackHeapFrame * MVM_callstack_allocate_heap_frame(MVMThreadContext *tc);
MVMCallStackDispatchRecord * MVM_callstack_allocate_dispatch_record(MVMThreadContext *tc);
void MVM_callstack_new_continuation_region(MVMThreadContext *tc, MVMObject *tag);
MVMCallStackRegion * MVM_callstack_continuation_slice(MVMThreadContext *tc, MVMObject *tag,
        MVMActiveHandler **active_handlers);
void MVM_callstack_continuation_append(MVMThreadContext *tc, MVMCallStackRegion *first_region,
        MVMCallStackRecord *stack_top, MVMObject *update_tag);
MVMFrame * MVM_callstack_first_frame_in_region(MVMThreadContext *tc, MVMCallStackRegion *region);
MVMCallStackDispatchRecord * MVM_callstack_find_topmost_dispatch_recording(MVMThreadContext *tc);
MVMFrame * MVM_callstack_unwind_frame(MVMThreadContext *tc);
void MVM_callstack_unwind_dispatcher(MVMThreadContext *tc);
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
