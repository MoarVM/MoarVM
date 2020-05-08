/* The call stack consists of a series of regions, which each contain records.
 * These records represent the things that are in dynamic scope. This includes
 * bytecode-level call frames, dispatcher record state, dispatcher program
 * evaluation state, continuation tags, and state associated with argument
 * flattening. */

/* A region of the call stack. */
struct MVMCallStackRegion {
    /* Next call stack region, which we start allocating in if this one is
     * full. NULL if none has been allocated yet. */
    MVMCallStackRegion *next;

    /* Previous call stack region, if any. */
    MVMCallStackRegion *prev;

    /* The place we'll allocate the next frame. */
    char *alloc;

    /* The end of the allocatable region. */
    char *alloc_limit;
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
};

/* A bytecode frame, the MVMFrame being allocated inline on the callstack. */
#define MVM_CALLSTACK_RECORD_FRAME              1
struct MVMCallStackFrame {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The bytecode call frame record itself, inlined directly here. */
    MVMFrame frame;
};

/* A bytecode frame where the MVMFrame was allocated directly on the heap.  */
#define MVM_CALLSTACK_RECORD_HEAP_FRAME         2
struct MVMCallStackHeapFrame {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* A pointer to the heap-allocated frame. */
    MVMFrame *frame;
};

/* A bytecode frame where the MVMFrame was allocated inline on the callstack,
 * but later promoted to the heap. */
#define MVM_CALLSTACK_RECORD_PROMOTED_FRAME     3
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

/* A continuation tag record, used to find the base of a continuation. */
#define MVM_CALLSTACK_RECORD_CONTINUATION_TAG   4
struct MVMCallStackContinuationTag {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* The tag itself. */
    MVMObject *tag;

    /* The active exception handler at the point the tag was taken. */
    MVMActiveHandler *active_handlers;
};

/* An argument flattening record, used to hold the results of flattening a
 * dispatch's arguments. A pointer into this is then used as the args source
 * in the following dispatch. */
#define MVM_CALLSTACK_RECORD_FLATTENING         5
struct MVMCallStackFlattening {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* TODO */
};

/* A dispatch recording phase record, when we're running the dispatch
 * callback and constructing a dispatch program. May hold dispatch
 * state for a resumption. */
#define MVM_CALLSTACK_RECORD_DISPATCH_RECORD    6
struct MVMCallStackDispatchRecord {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* TODO */
};

/* A dispatch program run record, when we're evaluating a dispatch
 * program. May hold dispatch state for a resumption. */
#define MVM_CALLSTACK_RECORD_DISPATCH_RUN       7
struct MVMCallStackDispatchRun {
    /* Commonalities of all records. */
    MVMCallStackRecord common;

    /* TODO */
};

/* Functions for working with call stack regions. */
void MVM_callstack_region_init(MVMThreadContext *tc);
MVMCallStackRegion * MVM_callstack_region_next(MVMThreadContext *tc);
MVMCallStackRegion * MVM_callstack_region_prev(MVMThreadContext *tc);
void MVM_callstack_reset(MVMThreadContext *tc);
void MVM_callstack_region_destroy_all(MVMThreadContext *tc);
