#include "moar.h"

/* Allocates a new call stack region, not incorporated into the regions double
 * linked list yet. */
static MVMCallStackRegion * allocate_region(void) {
    MVMCallStackRegion *region = MVM_malloc(MVM_CALLSTACK_REGION_SIZE);
    region->prev = region->next = NULL;
    region->start = (char *)region + sizeof(MVMCallStackRegion);
    region->alloc = region->start;
    region->alloc_limit = (char *)region + MVM_CALLSTACK_REGION_SIZE;
    return region;
}

/* Allocates a record in the current call stack region and returns it. Doesn't
 * check if growth is needed. Sets its previous record to the current stack
 * top, but does not itself update the stack top. */
static MVMCallStackRecord * allocate_record_unchecked(MVMThreadContext *tc, MVMuint8 kind, size_t size) {
    MVMCallStackRegion *region = tc->stack_current_region;
    MVMCallStackRecord *record = (MVMCallStackRecord *)region->alloc;
    record->prev = tc->stack_top;
    record->kind = kind;
    region->alloc += size;
    return record;
}

/* Moves to a new callstack region, creating the region if required. */
static void next_region(MVMThreadContext *tc) {
    MVMCallStackRegion *region = tc->stack_current_region;
    if (!region->next) {
        MVMCallStackRegion *next = allocate_region();
        region->next = next;
        next->prev = region;
    }
    tc->stack_current_region = region->next;
}

/* Allocates a record, placing it in the current call stack region if possible
 * but moving to the next one if not. Sets its previous record to the current
 * stack top, but does not itself update the stack top. */
static MVMCallStackRecord * allocate_record(MVMThreadContext *tc, MVMuint8 kind, size_t size) {
    MVMCallStackRegion *region = tc->stack_current_region;
    if ((region->alloc_limit - region->alloc) < (ptrdiff_t)size) {
        size_t real_limit = MVM_CALLSTACK_REGION_SIZE - sizeof(MVMCallStackContinuationTag);
        if (size > real_limit)
            MVM_oops(tc, "Oversize callstack record requested (wanted %d, maximum %d)",
                    size, real_limit);
        next_region(tc);
        tc->stack_top = allocate_record_unchecked(tc, MVM_CALLSTACK_RECORD_START_REGION,
                sizeof(MVMCallStackRegionStart));
    }
    return allocate_record_unchecked(tc, kind, size);
}

/* Gets the actual size of a record (including any dynamically sized parts). */
size_t record_size(MVMCallStackRecord *record) {
    switch (MVM_callstack_kind_ignoring_deopt(record)) {
        case MVM_CALLSTACK_RECORD_START:
            return sizeof(MVMCallStackStart);
        case MVM_CALLSTACK_RECORD_START_REGION:
            return sizeof(MVMCallStackRegionStart);
        case MVM_CALLSTACK_RECORD_FRAME:
            return sizeof(MVMCallStackFrame);
        case MVM_CALLSTACK_RECORD_HEAP_FRAME:
            return sizeof(MVMCallStackHeapFrame);
        case MVM_CALLSTACK_RECORD_PROMOTED_FRAME:
            return sizeof(MVMCallStackPromotedFrame);
        case MVM_CALLSTACK_RECORD_CONTINUATION_TAG:
            return sizeof(MVMCallStackContinuationTag);
        case MVM_CALLSTACK_RECORD_DISPATCH_RECORD:
            return sizeof(MVMCallStackDispatchRecord);
        default:
            MVM_panic(1, "Unknown callstack record type in record_size");
    }
}

/* Called upon thread creation to set up an initial callstack region for the
 * thread. */
void MVM_callstack_init(MVMThreadContext *tc) {
    /* Allocate an initial region, and put a start of stack record in it. */
    tc->stack_first_region = tc->stack_current_region = allocate_region();
    tc->stack_top = allocate_record_unchecked(tc, MVM_CALLSTACK_RECORD_START,
            sizeof(MVMCallStackStart));
}

/* Allocates a bytecode frame record on the callstack. */
MVMCallStackFrame * MVM_callstack_allocate_frame(MVMThreadContext *tc) {
    tc->stack_top = allocate_record(tc, MVM_CALLSTACK_RECORD_FRAME,
            sizeof(MVMCallStackFrame));
    return (MVMCallStackFrame *)tc->stack_top;
}

/* Allocates a bytecode frame record on the callstack. */
MVMCallStackHeapFrame * MVM_callstack_allocate_heap_frame(MVMThreadContext *tc) {
    tc->stack_top = allocate_record(tc, MVM_CALLSTACK_RECORD_HEAP_FRAME,
            sizeof(MVMCallStackHeapFrame));
    return (MVMCallStackHeapFrame *)tc->stack_top;
}

/* Allocates a dispatch recording record on the callstack. */
MVMCallStackDispatchRecord * MVM_callstack_allocate_dispatch_record(MVMThreadContext *tc) {
    tc->stack_top = allocate_record(tc, MVM_CALLSTACK_RECORD_DISPATCH_RECORD,
            sizeof(MVMCallStackDispatchRecord));
    return (MVMCallStackDispatchRecord *)tc->stack_top;
}

/* Allocates a dispatch run record on the callstack. */
MVMCallStackDispatchRun * MVM_callstack_allocate_dispatch_run(MVMThreadContext *tc,
        MVMuint32 num_temps) {
    tc->stack_top = allocate_record(tc, MVM_CALLSTACK_RECORD_DISPATCH_RUN,
            sizeof(MVMCallStackDispatchRecord) + num_temps * sizeof(MVMRegister));
    MVMCallStackDispatchRun *record = (MVMCallStackDispatchRun *)tc->stack_top;
    record->temps = (MVMRegister *)((char *)record + sizeof(MVMCallStackDispatchRun));
    record->num_temps = num_temps;
    return record;
}

/* Allocates a flattening record on the callstack, with space for the specified
 * number of arguments (both their flags and the args buffer itself) as well as
 * the list of argument name strings. Setup is performed as follows:
 * 1. produced_cs has its arg_flags pointing into a buffer of the right size to
 *    write the produced flags into, arg_names also pointing to an appropriate
 *    buffer, flag_count set to num_args, num_pos set as passed, and the rest
 *    zeroed.
 * 2. arg_info has its callsite pointer set to point at produced_cs, its map
 *    to point to an identity map of at least num_args length, and its source
 *    pointing at a buffer of num_args MVMRegisters.
 * Neither the arg_flags nor the arg names list nor the source are zeroed, as
 * it is expected they will all be written during the flattening process. */
static MVMuint32 to_8_bytes(MVMuint32 num) {
    return (num + 8 - 1) & -8;
}
MVMCallStackFlattening * MVM_callstack_allocate_flattening(MVMThreadContext *tc,
        MVMuint16 num_args, MVMuint16 num_pos) {
    /* Allocate. */
    size_t record_size = to_8_bytes(sizeof(MVMCallStackFlattening));
    size_t flags_size = to_8_bytes(num_args);
    size_t nameds_size = (num_args - num_pos) * sizeof(MVMString *);
    size_t args_size = num_args * sizeof(MVMRegister);
    tc->stack_top = allocate_record(tc, MVM_CALLSTACK_RECORD_FLATTENING,
            record_size + flags_size + nameds_size + args_size);
    MVMCallStackFlattening *record = (MVMCallStackFlattening *)tc->stack_top;

    /* Setup callsite area to produce. */
    record->produced_cs.arg_flags = (MVMCallsiteEntry *)((char *)record + record_size);
    record->produced_cs.flag_count = num_args;
    record->produced_cs.num_pos = num_pos;
    record->produced_cs.is_interned = 0;
    record->produced_cs.has_flattening = 0;
    record->produced_cs.arg_names = (MVMString **)((char *)record + record_size + flags_size);

    /* Set up arg info. */
    record->arg_info.callsite = &(record->produced_cs);
    record->arg_info.map = MVM_args_identity_map(tc, record->arg_info.callsite);
    record->arg_info.source = (MVMRegister *)((char *)record + record_size +
            flags_size + nameds_size);

    return record;
}

/* Creates a new region for a continuation. By a continuation boundary starting
 * a new region, we are able to take the continuation by slicing off the entire
 * region from the regions linked list. The continuation tags always go at the
 * start of such a region (and are a region start mark). */
void MVM_callstack_new_continuation_region(MVMThreadContext *tc, MVMObject *tag) {
    next_region(tc);
    tc->stack_top = allocate_record(tc, MVM_CALLSTACK_RECORD_CONTINUATION_TAG,
            sizeof(MVMCallStackContinuationTag));
    MVMCallStackContinuationTag *tag_record = (MVMCallStackContinuationTag *)tc->stack_top;
    tag_record->tag = tag;
    tag_record->active_handlers = tc->active_handlers;
}

/* Locates the callstack region for the specified continuation tag, and slices
 * it off the callstack, updating the stack top to point at the top frame in
 * the previous region. The first region in the slice is retunred. The prev
 * pointer of both the region and of the region start record are NULL'd out. */
MVMCallStackRegion * MVM_callstack_continuation_slice(MVMThreadContext *tc, MVMObject *tag,
        MVMActiveHandler **active_handlers) {
    MVMCallStackRegion *cur_region = tc->stack_current_region;
    while (cur_region != NULL) {
        MVMCallStackRecord *record = (MVMCallStackRecord *)cur_region->start;
        if (record->kind == MVM_CALLSTACK_RECORD_CONTINUATION_TAG) {
            MVMCallStackContinuationTag *tag_record = (MVMCallStackContinuationTag *)record;
            if (tag_record->tag == tag || tag == tc->instance->VMNull) {
                /* Found the tag we were looking for. Detach this region from
                 * the linked list. */
                tc->stack_current_region = cur_region->prev;
                tc->stack_current_region->next = NULL;

                /* Set the stack top to the prev of the tag record, and then
                 * clear that too. */
                tc->stack_top = tag_record->common.prev;
                tag_record->common.prev = NULL;

                /* Hand back the active handlers at the reset point through the
                 * out argument, and the region pointer as the return value. */
                *active_handlers = tag_record->active_handlers;
                return cur_region;
            }
        }
        cur_region = cur_region->prev;
    }
    return NULL;
}

/* Take the continuation regions and append them to the callstack, updating
 * the current region and the stack top appropriately. */
static void free_regions_from(MVMCallStackRegion *cur) {
    while (cur) {
        MVMCallStackRegion *next = cur->next;
        MVM_free(cur);
        cur = next;
    }
}
void MVM_callstack_continuation_append(MVMThreadContext *tc, MVMCallStackRegion *first_region,
        MVMCallStackRecord *stack_top, MVMObject *update_tag) {
    /* Ensure the first record in the region to append is a continuation tag. */
    MVMCallStackRecord *record = (MVMCallStackRecord *)first_region->start;
    if (record->kind != MVM_CALLSTACK_RECORD_CONTINUATION_TAG)
        MVM_panic(1, "Malformed continuation record");

    /* Update the continuation tag. */
    MVMCallStackContinuationTag *tag_record = (MVMCallStackContinuationTag *)record;
    tag_record->tag = update_tag;
    tag_record->active_handlers = tc->active_handlers;

    /* If we have next regions, free them (this prevents us ending up with
     * runaway memory use then continuations move between threads in producer
     * consumer style patterns). */
    free_regions_from(tc->stack_current_region->next);

    /* Insert continuation regions into the region list. */
    tc->stack_current_region->next = first_region;
    first_region->prev = tc->stack_current_region;

    /* Make sure the current stack region is the one containing the stack top. */
    while ((char *)stack_top < tc->stack_current_region->start ||
            (char *)stack_top > tc->stack_current_region->alloc)
        tc->stack_current_region = tc->stack_current_region->next;

    /* Make the first record we splice in point back to the current stack top. */
    record->prev = tc->stack_top;

    /* Update the stack top to the new top record. */
    tc->stack_top = stack_top;
}

/* Walk the frames in the region, looking for the first bytecode one. */
MVMFrame * MVM_callstack_first_frame_in_region(MVMThreadContext *tc, MVMCallStackRegion *region) {
    char *cur_pos = region->start;
    while (cur_pos < region->alloc) {
        MVMCallStackRecord *record = (MVMCallStackRecord *)cur_pos;
        switch (MVM_callstack_kind_ignoring_deopt(record)) {
            case MVM_CALLSTACK_RECORD_FRAME:
                return &(((MVMCallStackFrame *)record)->frame);
            case MVM_CALLSTACK_RECORD_HEAP_FRAME:
                return ((MVMCallStackHeapFrame *)record)->frame;
            case MVM_CALLSTACK_RECORD_PROMOTED_FRAME:
                return ((MVMCallStackPromotedFrame *)record)->frame;
            default:
                cur_pos += record_size(record);
        }
    }
    MVM_panic(1, "No frame found in callstack region");
}

/* Finds the first frame that is a dispatch recording. */
MVMCallStackDispatchRecord * MVM_callstack_find_topmost_dispatch_recording(MVMThreadContext *tc) {
    MVMCallStackIterator iter;
    MVM_callstack_iter_one_kind_init(tc, &iter, tc->stack_top, MVM_CALLSTACK_RECORD_DISPATCH_RECORD);
    if (!MVM_callstack_iter_move_next(tc, &iter))
        MVM_exception_throw_adhoc(tc, "Not currently recording a dispatch program");
    return (MVMCallStackDispatchRecord *)MVM_callstack_iter_current(tc, &iter);
}

/* Unwind the calls stack until we reach a prior bytecode frame. */
static int is_bytecode_frame(MVMuint8 kind) {
    switch (kind) {
        case MVM_CALLSTACK_RECORD_FRAME:
        case MVM_CALLSTACK_RECORD_HEAP_FRAME:
        case MVM_CALLSTACK_RECORD_PROMOTED_FRAME:
            return 1;
        default:
            return 0;
    }
}
static void unwind_region_start(MVMThreadContext *tc) {
    if (tc->stack_top->kind == MVM_CALLSTACK_RECORD_START_REGION) {
        tc->stack_current_region->alloc = (char *)tc->stack_top;
        tc->stack_current_region = tc->stack_current_region->prev;
        tc->stack_top = tc->stack_top->prev;
    }
}
static void handle_end_of_dispatch_record(MVMThreadContext *tc, MVMuint32 *thunked) {
    /* End of a dispatch recording; make callback to update the
     * inline cache, put the result in place, and take any further
     * actions. If the dispatch invokes bytecode, then the dispatch
     * record stays around, but we tweak its kind so we don't enter
     * the end of recording logic again. */
    MVMCallStackDispatchRecord *disp_record = (MVMCallStackDispatchRecord *)tc->stack_top;
    MVMuint32 remove_dispatch_frame = MVM_disp_program_record_end(tc, disp_record, thunked);
    if (remove_dispatch_frame) {
        assert((char *)disp_record == (char *)tc->stack_top);
        MVM_disp_program_recording_destroy(tc, &(disp_record->rec));
        tc->stack_current_region->alloc = (char *)tc->stack_top;
        tc->stack_top = tc->stack_top->prev;
        unwind_region_start(tc);
    }
}
static void exit_frame(MVMThreadContext *tc, MVMFrame *returner) {
    MVMFrame *caller = returner->caller;
    if (caller && (returner != tc->thread_entry_frame || tc->nested_interpreter)) {
       if (tc->jit_return_address != NULL) {
            /* on a JIT frame, exit to interpreter afterwards */
            MVMJitCode *jitcode = returner->spesh_cand->body.jitcode;
            MVM_jit_code_set_current_position(tc, jitcode, returner, jitcode->exit_label);
            /* given that we might throw in the special-return, act as if we've
             * left the current frame (which is true) */
            tc->jit_return_address = NULL;
        }

        tc->cur_frame = caller;
        tc->current_frame_nr = caller->sequence_nr;
    }
    else {
        tc->cur_frame = NULL;
    }
}
MVMFrame * MVM_callstack_unwind_frame(MVMThreadContext *tc, MVMuint8 exceptional, MVMuint32 *thunked) {
    do {
        /* Ensure region and stack top are in a consistent state. */
        assert(tc->stack_current_region->start <= (char *)tc->stack_top);
        assert((char *)tc->stack_top < tc->stack_current_region->alloc);

        /* Do any cleanup actions needed. */
        switch (tc->stack_top->kind) {
            case MVM_CALLSTACK_RECORD_START_REGION:
            case MVM_CALLSTACK_RECORD_CONTINUATION_TAG:
                /* Sync region and move to previous record. */
                tc->stack_current_region->alloc = (char *)tc->stack_top;
                tc->stack_current_region = tc->stack_current_region->prev;
                tc->stack_top = tc->stack_top->prev;
                break;
            case MVM_CALLSTACK_RECORD_FRAME:
                exit_frame(tc, &(((MVMCallStackFrame *)tc->stack_top)->frame));
                tc->stack_current_region->alloc = (char *)tc->stack_top;
                tc->stack_top = tc->stack_top->prev;
                break;
            case MVM_CALLSTACK_RECORD_HEAP_FRAME:
                exit_frame(tc, ((MVMCallStackHeapFrame *)tc->stack_top)->frame);
                tc->stack_current_region->alloc = (char *)tc->stack_top;
                tc->stack_top = tc->stack_top->prev;
                break;
            case MVM_CALLSTACK_RECORD_PROMOTED_FRAME:
                exit_frame(tc, ((MVMCallStackPromotedFrame *)tc->stack_top)->frame);
                tc->stack_current_region->alloc = (char *)tc->stack_top;
                tc->stack_top = tc->stack_top->prev;
                break;
            case MVM_CALLSTACK_RECORD_DEOPT_FRAME:
                /* Deopt it, but don't move stack top back, since we're either
                 * turning the current frame into a deoptimized one or will put
                 * new uninlined frames on the top of the stack, which we shall
                 * then want to return in to. */
                MVM_spesh_deopt_during_unwind(tc);
                break;
            case MVM_CALLSTACK_RECORD_START:
            case MVM_CALLSTACK_RECORD_FLATTENING:
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORDED:
            case MVM_CALLSTACK_RECORD_DISPATCH_RUN:
                /* No cleanup to do, just move to next record. */
                tc->stack_current_region->alloc = (char *)tc->stack_top;
                tc->stack_top = tc->stack_top->prev;
                break;
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORD:
                if (!exceptional) {
                    handle_end_of_dispatch_record(tc, thunked);
                }
                else {
                    /* There was an exception; just leave the frame behind. */
                    MVMCallStackDispatchRecord *disp_record =
                        (MVMCallStackDispatchRecord *)tc->stack_top;
                    MVM_disp_program_recording_destroy(tc, &(disp_record->rec));
                    tc->stack_current_region->alloc = (char *)tc->stack_top;
                    tc->stack_top = tc->stack_top->prev;
                }
                break;
            default:
                MVM_panic(1, "Unknown call stack record type in unwind");
        }
    } while (tc->stack_top && !is_bytecode_frame(tc->stack_top->kind));
    return tc->stack_top ? MVM_callstack_record_to_frame(tc->stack_top) : NULL;
}

/* Unwind a dispatch record frame, which should be on the top of the stack.
 * This is for the purpose of dispatchers that do not invoke. */
void MVM_callstack_unwind_dispatch_record(MVMThreadContext *tc, MVMuint32 *thunked) {
    assert(tc->stack_top->kind == MVM_CALLSTACK_RECORD_DISPATCH_RECORD);
    handle_end_of_dispatch_record(tc, thunked);
}

/* Unwind a dispatch run frame, which should be on the top of the stack.
 * This is for the purpose of dispatchers that do not invoke. */
void MVM_callstack_unwind_dispatch_run(MVMThreadContext *tc) {
    assert(tc->stack_top->kind == MVM_CALLSTACK_RECORD_DISPATCH_RUN);
    tc->stack_current_region->alloc = (char *)tc->stack_top;
    tc->stack_top = tc->stack_top->prev;
    unwind_region_start(tc);
}

/* Walk the linked list of records and mark each of them. */
#define add_collectable(tc, worklist, snapshot, col, desc) \
    do { \
        if (worklist) { \
            MVM_gc_worklist_add(tc, worklist, &(col)); \
        } \
        else { \
            MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot, \
                (MVMCollectable *)col, desc); \
        } \
    } while (0)
static void mark(MVMThreadContext *tc, MVMCallStackRecord *from_record, MVMGCWorklist *worklist,
        MVMHeapSnapshotState *snapshot) {
    MVMCallStackRecord *record = from_record;
    while (record) {
        switch (MVM_callstack_kind_ignoring_deopt(record)) {
            case MVM_CALLSTACK_RECORD_FRAME:
                /* TODO make sure the heap snapshot profiler doesn't miss this */
                if (worklist)
                    MVM_gc_root_add_frame_roots_to_worklist(tc, worklist,
                            &(((MVMCallStackFrame *)record)->frame));
                break;
            case MVM_CALLSTACK_RECORD_HEAP_FRAME:
                add_collectable(tc, worklist, snapshot,
                        ((MVMCallStackHeapFrame *)record)->frame,
                        "Callstack reference to heap-allocated frame");
                break;
            case MVM_CALLSTACK_RECORD_PROMOTED_FRAME:
                add_collectable(tc, worklist, snapshot,
                        ((MVMCallStackPromotedFrame *)record)->frame,
                        "Callstack reference to heap-promoted frame");
                break;
            case MVM_CALLSTACK_RECORD_CONTINUATION_TAG:
                add_collectable(tc, worklist, snapshot,
                        ((MVMCallStackContinuationTag *)record)->tag,
                        "Continuation tag");
                break;
            case MVM_CALLSTACK_RECORD_START:
            case MVM_CALLSTACK_RECORD_START_REGION:
                /* Nothing to mark. */
                break;
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORD:
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORDED: {
                MVMCallStackDispatchRecord *disp_record = (MVMCallStackDispatchRecord *)record;
                MVM_disp_program_mark_recording(tc, &(disp_record->rec), worklist);
                MVM_disp_program_mark_outcome(tc, &(disp_record->outcome), worklist);
                add_collectable(tc, worklist, snapshot, disp_record->current_capture.o,
                        "Dispatch recording current capture");
                add_collectable(tc, worklist, snapshot, disp_record->update_sf,
                        "Dispatch recording static frame root");
                break;
            }
            case MVM_CALLSTACK_RECORD_DISPATCH_RUN: {
                MVMCallStackDispatchRun *disp_run = (MVMCallStackDispatchRun *)record;
                MVMDispProgram *dp = disp_run->chosen_dp;
                if (dp && dp->num_temporaries != dp->first_args_temporary)
                    MVM_disp_program_mark_run_temps(tc, dp,
                            disp_run->temp_mark_callsite, disp_run->temps,
                            worklist);
                break;
            }
            case MVM_CALLSTACK_RECORD_FLATTENING: {
                MVMCallStackFlattening *f_record = (MVMCallStackFlattening *)record;
                MVMuint16 flagi;
                MVMCallsite *cs = &f_record->produced_cs;
                MVM_callsite_mark(tc, cs, worklist);
                for (flagi = 0; flagi < f_record->produced_cs.flag_count; flagi++) {
                    MVMuint8 flagtype = cs->arg_flags[flagi] & MVM_CALLSITE_ARG_TYPE_MASK;
                    if (flagtype == MVM_CALLSITE_ARG_OBJ || flagtype == MVM_CALLSITE_ARG_STR) {
                        add_collectable(tc, worklist, snapshot, f_record->arg_info.source[f_record->arg_info.map[flagi]].o,
                                "Flattened callstack entry register value");
                    }
                }
                break;
            }
            default:
                MVM_panic(1, "Unknown call stack record type in GC marking");
        }
        record = record->prev;
    }
}

/* Walk the current thread's callstack and GC-mark its content. */
void MVM_callstack_mark_current_thread(MVMThreadContext *tc, MVMGCWorklist *worklist,
        MVMHeapSnapshotState *snapshot) {
    mark(tc, tc->stack_top, worklist, snapshot);
}

/* Walk the records chain from the specified stack top. This is used when we
 * have a chunk of records detached from the callstack. */
void MVM_callstack_mark_detached(MVMThreadContext *tc, MVMCallStackRecord *stack_top,
        MVMGCWorklist *worklist) {
    mark(tc, stack_top, worklist, NULL);
}

/* Frees detached regions of the callstack, for example if a continuation is
 * taken, but never invoked, and then gets collected. */
void MVM_callstack_free_detached_regions(MVMThreadContext *tc, MVMCallStackRegion *first_region,
        MVMCallStackRecord *stack_top) {
    if (first_region && stack_top) {
        /* For now, we don't have any unwind cleanup work that causes leaks if
         * we don't do it; in the future, we may, in which case it needs to
         * be done here. For now, it's sufficient just to free the regions. */
        free_regions_from(first_region);
    }
}

/* Called at thread exit to destroy all callstack regions the thread has. */
void MVM_callstack_destroy(MVMThreadContext *tc) {
    free_regions_from(tc->stack_first_region);
    tc->stack_first_region = NULL;
}
