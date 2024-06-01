#include "moar.h"

#ifdef _MSC_VER
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif

static int crash_on_error = 0;

/* Function for getting effective (specialized or not) frame handlers. */
MVM_STATIC_INLINE MVMFrameHandler * MVM_frame_effective_handlers(MVMFrame *f) {
    MVMSpeshCandidate *spesh_cand = f->spesh_cand;
    return spesh_cand ? spesh_cand->body.handlers : f->static_info->body.handlers;
}

/* Maps ID of exception category to its name. */
static const char * cat_name(MVMThreadContext *tc, MVMint32 cat) {
    switch (cat) {
        case MVM_EX_CAT_CATCH:
            return "catch";
        case MVM_EX_CAT_CONTROL:
            return "control";
        case MVM_EX_CAT_NEXT:
            return "next";
        case MVM_EX_CAT_REDO:
            return "redo";
        case MVM_EX_CAT_LAST:
            return "last";
        case MVM_EX_CAT_RETURN:
            return "return";
        case MVM_EX_CAT_TAKE:
            return "take";
        case MVM_EX_CAT_WARN:
            return "warn";
        case MVM_EX_CAT_SUCCEED:
            return "succeed";
        case MVM_EX_CAT_PROCEED:
            return "proceed";
        case MVM_EX_CAT_NEXT | MVM_EX_CAT_LABELED:
            return "next_label";
        case MVM_EX_CAT_REDO | MVM_EX_CAT_LABELED:
            return "redo_label";
        case MVM_EX_CAT_LAST | MVM_EX_CAT_LABELED:
            return "last_label";
        default:
            return "unknown";
    }
}

/* Checks if an exception handler is already on the active handler stack,
 * so we don't re-trigger the same exception handler. Note: We have static
 * handlers that get reused, so also check for the same handler being in
 * the same frame, otherwise we consider the handler as being another one. */
static MVMuint8 in_handler_stack(MVMThreadContext *tc, MVMFrameHandler *fh, MVMFrame *f) {
    if (tc->active_handlers) {
        MVMActiveHandler *ah = tc->active_handlers;
        while (ah) {
            if (ah->handler == fh && ah->frame == f)
                return 1;
            ah = ah->next_handler;
        }
    }
    return 0;
}

/* Checks if a frame is still active. Naively, we could scan the call stack
 * for it, but since we always clean up ->work when a frame is removed from
 * the call stack we can do it in O(1) that way. */
static MVMuint8 in_caller_chain(MVMThreadContext *tc, MVMFrame *f_maybe) {
    return f_maybe->work ? 1 : 0;
}


/* Information about a located handler. */
typedef struct {
    MVMFrame        *frame;
    MVMFrameHandler *handler;
    MVMJitHandler   *jit_handler;
    MVMint32         handler_out_of_dynamic_scope;
} LocatedHandler;

static MVMint32 handler_can_handle(MVMFrame *f, MVMFrameHandler *fh, MVMuint32 cat, MVMObject *payload) {
    MVMuint32         category_mask = fh->category_mask;
    MVMuint64       block_has_label = category_mask & MVM_EX_CAT_LABELED;
    MVMuint64           block_label = block_has_label ? (uintptr_t)(f->work[fh->label_reg].o) : 0;
    MVMuint64          thrown_label = payload ? (uintptr_t)payload : 0;
    MVMuint64 identical_label_found = thrown_label == block_label;
    return ((cat & category_mask) == cat && (!(cat & MVM_EX_CAT_LABELED) || identical_label_found))
        || ((category_mask & MVM_EX_CAT_CONTROL) && cat != MVM_EX_CAT_CATCH);
}

/* Looks through the handlers of a particular frame, including inlines in
 * dynamic scope, and sees if one will match what we're looking for. Returns
 * 1 to it if so, and 0 if not; in the case 1 is returned the *lh will be
 * populated with details of the located handler. Since upon inlining, the
 * dynamic scope becomes lexical so far as the optimized bytecode is
 * concerned, then this just needs a scan of the table without any further
 * checks being needed. */
static MVMint32 search_frame_handlers_dyn(MVMThreadContext *tc, MVMFrame *f,
                                          MVMuint32 cat, MVMObject *payload,
                                          LocatedHandler *lh) {
    MVMuint32  i;
    if (f->spesh_cand && f->spesh_cand->body.jitcode && f->jit_entry_label) {
        MVMJitCode *jitcode = f->spesh_cand->body.jitcode;
        void *current_position = MVM_jit_code_get_current_position(tc, jitcode, f);
        MVMJitHandler    *jhs = f->spesh_cand->body.jitcode->handlers;
        MVMFrameHandler  *fhs = MVM_frame_effective_handlers(f);
        for (i = MVM_jit_code_get_active_handlers(tc, jitcode, current_position, 0);
             i < jitcode->num_handlers;
             i = MVM_jit_code_get_active_handlers(tc, jitcode, current_position, i+1)) {
            if (handler_can_handle(f, &fhs[i], cat, payload) &&
                !in_handler_stack(tc, &fhs[i], f)) {
                lh->handler     = &fhs[i];
                lh->jit_handler = &jhs[i];
                return 1;
            }
        }
    } else {
        MVMuint32 num_handlers = f->spesh_cand
            ? f->spesh_cand->body.num_handlers
            : f->static_info->body.num_handlers;
        MVMuint32 pc;
        if (f == tc->cur_frame)
            pc = (MVMuint32)(*tc->interp_cur_op - *tc->interp_bytecode_start);
        else
            pc = (MVMuint32)(f->return_address - MVM_frame_effective_bytecode(f));
        for (i = 0; i < num_handlers; i++) {
            MVMFrameHandler  *fh = &(MVM_frame_effective_handlers(f)[i]);
            if (!handler_can_handle(f, fh, cat, payload))
                continue;
            if (pc >= fh->start_offset && pc <= fh->end_offset && !in_handler_stack(tc, fh, f)) {
                lh->handler = fh;
                return 1;
            }
        }
    }
    return 0;
}

/* Looks for lexically applicable handlers in the current frame, accounting
 * for any inlines. The skip_first_inlinee flag indicates that we should skip
 * looking until we have encountered an inline boundary indicator saying that
 * we have crossed from an inlinee to its inliner's handlers; this is used to
 * handle the THROW_LEX_CALLER mode. If we never encounter an inline boundary
 * when skip_first_inlinee is true then we'll always return 0.
 *
 * If skip_first_inlinee is false or we already saw an inline boundary, then
 * we start looking for a matching handler. If one is found before seeing
 * another inline boundary, then it is applicable; the data pointed to by lh
 * will be updated with the frame handler details and 1 will be returned.
 *
 * Upon reaching an (or, in the case of skip_first_inlinee, a second) inline
 * boundary indicator, there are two cases that apply. We take the inline
 * that we are leaving, and look up the code ref using the code_ref_reg in
 * the inline. We know that we can never inline a frame that was closed over
 * (due to capturelex or takeclosure being marked :noinline). Thus, either:
 * 1. The outer of the inlinee is actually the current frame f. In this case,
 *    we skip all inlined handlers and just consider those of f itself.
 * 2. The next frame we should search in is the ->outer of the inlinee, and
 *    thus all the rest of the handlers in this frame should be ignored. In
 *    this case, the MVMFrame **next_outer will be populated with a pointer
 *    to that frame.
 *
 * The skip_all_inlinees flag is set once we are below the frame on the stack
 * to where the search started. Again, this is because a frame that did a
 * lexical capture may not be inlined, so we only need to consider the topmost
 * frame's handlers, not anything it might have inlined into it.
 */
static MVMint32 search_frame_handlers_lex(MVMThreadContext *tc, MVMFrame *f,
                                          MVMuint32 cat, MVMObject *payload,
                                          LocatedHandler *lh,
                                          MVMuint32 *skip_first_inlinee,
                                          MVMuint32 skip_all_inlinees,
                                          MVMFrame **next_outer) {
    MVMuint32 i;
    MVMuint32 skipping = *skip_first_inlinee;
    MVMFrameHandler *fhs = MVM_frame_effective_handlers(f);
    if (f->spesh_cand && f->spesh_cand->body.jitcode && f->jit_entry_label) {
        MVMJitCode *jitcode = f->spesh_cand->body.jitcode;
        void *current_position = MVM_jit_code_get_current_position(tc, jitcode, f);
        MVMJitHandler    *jhs = jitcode->handlers;
        for (i = MVM_jit_code_get_active_handlers(tc, jitcode, current_position, 0);
             i < jitcode->num_handlers;
             i = MVM_jit_code_get_active_handlers(tc, jitcode, current_position, i+1)) {
            MVMFrameHandler *fh = &(fhs[i]);
            if (skip_all_inlinees && fh->inlinee >= 0)
                continue;
            if (fh->category_mask == MVM_EX_INLINE_BOUNDARY) {
                if (skipping) {
                    skipping = 0;
                    *skip_first_inlinee = 0;
                }
                else {
                    MVMuint16 cr_reg = f->spesh_cand->body.inlines[fh->inlinee].code_ref_reg;
                    MVMFrame *inline_outer = ((MVMCode *)f->work[cr_reg].o)->body.outer;
                    if (inline_outer == f) {
                        skip_all_inlinees = 1;
                    }
                    else {
                        *next_outer = inline_outer;
                        return 0;
                    }
                }
            }
            if (skipping || !handler_can_handle(f, fh, cat, payload))
                continue;
            if (!in_handler_stack(tc, fh, f)) {
                if (skipping && f->static_info->body.is_thunk)
                    return 0;
                lh->handler     = fh;
                lh->jit_handler = &jhs[i];
                return 1;
            }
        }
    }
    else {
        MVMuint32 num_handlers = f->spesh_cand
            ? f->spesh_cand->body.num_handlers
            : f->static_info->body.num_handlers;
        MVMuint32 pc;
        if (f == tc->cur_frame)
            pc = (MVMuint32)(*tc->interp_cur_op - *tc->interp_bytecode_start);
        else
            pc = (MVMuint32)(f->return_address - MVM_frame_effective_bytecode(f));
        for (i = 0; i < num_handlers; i++) {
            MVMFrameHandler *fh = &(fhs[i]);
            if (skip_all_inlinees && fh->inlinee >= 0)
                continue;
            if (fh->category_mask == MVM_EX_INLINE_BOUNDARY) {
                if (pc >= fh->start_offset && pc <= fh->end_offset) {
                    if (skipping) {
                        skipping = 0;
                        *skip_first_inlinee = 0;
                    }
                    else {
                        MVMuint16 cr_reg = f->spesh_cand->body.inlines[fh->inlinee].code_ref_reg;
                        MVMFrame *inline_outer = ((MVMCode *)f->work[cr_reg].o)->body.outer;
                        if (inline_outer == f) {
                            skip_all_inlinees = 1;
                        }
                        else {
                            *next_outer = inline_outer;
                            return 0;
                        }
                    }
                }
                continue;
            }
            if (skipping || !handler_can_handle(f, fh, cat, payload))
                continue;
            if (pc >= fh->start_offset &&
                    pc <= fh->end_offset &&
                    !in_handler_stack(tc, fh, f)) {
                if (skipping && f->static_info->body.is_thunk)
                    return 0;
                lh->handler = fh;
                return 1;
            }
        }
    }
    return 0;
}

/* Searches for a handler of the specified category, relative to the given
 * starting frame, searching according to the chosen mode. */
static LocatedHandler search_for_handler_from(MVMThreadContext *tc, MVMFrame *f,
        MVMuint8 mode, MVMuint32 cat, MVMObject *payload) {
    MVMuint32 skip_first_inlinee = 0;
    LocatedHandler lh;
    lh.frame = NULL;
    lh.handler = NULL;
    lh.jit_handler = NULL;
    lh.handler_out_of_dynamic_scope = 0;
    switch (mode) {
        case MVM_EX_THROW_LEX_CALLER:
            skip_first_inlinee = 1;
            /* fallthrough */
        case MVM_EX_THROW_LEX: {
            MVMint32 skip_all_inlinees = 0;
            while (f != NULL) {
                MVMFrame *outer_from_inlinee = NULL;
                if (search_frame_handlers_lex(tc, f, cat, payload, &lh, &skip_first_inlinee,
                            skip_all_inlinees, &outer_from_inlinee)) {
                    if (in_caller_chain(tc, f))
                        lh.frame = f;
                    else
                        lh.handler_out_of_dynamic_scope = 1;
                    return lh;
                }
                if (skip_first_inlinee) {
                    /* If this is still set, it means that the topmost frame
                     * had no inlines, so we didn't already reach a chain of
                     * outers to traverse. In this case, skip over any thunks
                     * and continue the search. */
                    skip_first_inlinee = 0;
                    f = f->caller;
                    while (f && f->static_info->body.is_thunk)
                        f = f->caller;
                }
                else {
                    f = outer_from_inlinee ? outer_from_inlinee : f->outer;
                    skip_all_inlinees = 1;
                }
            }
            return lh;
        }
        case MVM_EX_THROW_DYN:
            while (f != NULL) {
                if (search_frame_handlers_dyn(tc, f, cat, payload, &lh)) {
                    lh.frame = f;
                    return lh;
                }
                f = MVM_LIKELY(f != tc->thread_entry_frame) ? f->caller : NULL;
            }
            return lh;
        case MVM_EX_THROW_LEXOTIC:
            while (f != NULL) {
                lh = search_for_handler_from(tc, f, MVM_EX_THROW_LEX, cat, payload);
                if (lh.frame != NULL)
                    return lh;
                f = MVM_LIKELY(f != tc->thread_entry_frame) ? f->caller : NULL;
            }
            return lh;
        default:
            MVM_panic(1, "Unhandled exception throw mode %d", (int)mode);
    }
}

/* Runs an exception handler (which really means updating interpreter state
 * so that when we return to the runloop, we're in the handler). If there is
 * an exception object already, it will be used; NULL can be passed if there
 * is not one, meaning it will be created if needed (based on the category
 * parameter; if ex_obj is passed, the category is not used). */
static void unwind_after_handler(MVMThreadContext *tc, void *sr_data);
static void cleanup_active_handler(MVMThreadContext *tc, void *sr_data);
static void run_handler(MVMThreadContext *tc, LocatedHandler lh, MVMObject *ex_obj,
                        MVMuint32 category, MVMObject *payload) {
    switch (lh.handler->action) {
    case MVM_EX_ACTION_GOTO_WITH_PAYLOAD:
        if (payload)
            tc->last_payload = payload;
        else if (ex_obj && ((MVMException *)ex_obj)->body.payload)
            tc->last_payload = ((MVMException *)ex_obj)->body.payload;
        else
            tc->last_payload = tc->instance->VMNull;
        /* Deliberate fallthrough to unwind below. */
        MVM_FALLTHROUGH

    case MVM_EX_ACTION_GOTO: ;
        if (category == MVM_EX_CAT_RETURN &&
                MVM_frame_continue_conflicting_unwind(tc, lh.frame, 0)) {
            /* There is a conflicting unwind that took precedence. */
        }
        else {
            if (lh.jit_handler) {
                void **labels = lh.frame->spesh_cand->body.jitcode->labels;
                MVMuint8  *pc = lh.frame->spesh_cand->body.jitcode->bytecode;
                MVM_frame_unwind_to(tc, lh.frame, pc, 0, NULL,
                        labels[lh.jit_handler->goto_label], 0);
            } else {
                MVM_frame_unwind_to(tc, lh.frame, NULL,
                        lh.handler->goto_offset, NULL, NULL, 0);
            }
        }

        break;

    case MVM_EX_ACTION_INVOKE: {
        /* Create active handler record. */
        MVMActiveHandler *ah = MVM_malloc(sizeof(MVMActiveHandler));

        /* Ensure we have an exception object. */
        MVMFrame *cur_frame = tc->cur_frame;
        if (ex_obj == NULL) {
            MVMROOT3(tc, cur_frame, lh.frame, payload, {
                ex_obj = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTException);
            });
            ((MVMException *)ex_obj)->body.category = category;
            MVM_ASSIGN_REF(tc, &(ex_obj->header), ((MVMException *)ex_obj)->body.payload, payload);
        }

        /* Preserve the frame caller chain, so we can do backtraces. */
        MVMFrame *pres_frame = ((MVMException *)ex_obj)->body.origin;
        while (pres_frame) {
            MVMFrameExtra *extra = MVM_frame_extra(tc, pres_frame);
            extra->caller_info_needed = 1;
            pres_frame = pres_frame->caller;
        }

        /* Find frame to invoke. */
        MVMObject *handler_code = lh.frame->work[lh.handler->block_reg].o;
        if (!MVM_code_iscode(tc, handler_code))
            MVM_oops(tc, "Exception handler must be a VM code handle");

        /* Install active handler record. */
        ah->frame           = lh.frame;
        ah->handler         = lh.handler;
        ah->jit_handler     = lh.jit_handler;
        ah->ex_obj          = ex_obj;
        ah->next_handler    = tc->active_handlers;
        tc->active_handlers = ah;

        /* Set up special return to unwinding after running the
         * handler. */
        cur_frame->return_value = (MVMRegister *)&tc->last_handler_result;
        cur_frame->return_type = MVM_RETURN_OBJ;
        cur_frame->return_address = *(tc->interp_cur_op);
        MVMActiveHandler **sr = MVM_callstack_allocate_special_return(tc,
                unwind_after_handler, cleanup_active_handler,
                NULL, sizeof(MVMActiveHandler *));
        *sr = ah;

        /* Invoke the handler frame and return to runloop. */
        MVM_frame_dispatch_zero_args(tc, (MVMCode *)handler_code);
        break;
    }
    default:
        MVM_panic(1, "Unimplemented handler action");
    }
}

/* Unwinds after a handler. */
static void unwind_after_handler(MVMThreadContext *tc, void *sr_data) {
    MVMFrame     *frame;
    MVMException *exception;
    MVMuint32     goto_offset;
    MVMuint8     *abs_address;
    void         *jit_return_label;

    /* Get active handler; sanity check (though it's possible other cases
     * should be supported). */
    MVMActiveHandler *ah = *((MVMActiveHandler **)sr_data);
    if (tc->active_handlers != ah)
        MVM_panic(1, "Trying to unwind from wrong handler");

    /* Grab info we'll need to unwind. */
    frame       = ah->frame;
    exception   = (MVMException *)ah->ex_obj;
    if (ah->jit_handler) {
        void **labels = frame->spesh_cand->body.jitcode->labels;
        jit_return_label = labels[ah->jit_handler->goto_label];
        abs_address = frame->spesh_cand->body.jitcode->bytecode;
        goto_offset = 0;
    }
    else {
        goto_offset = ah->handler->goto_offset;
        abs_address = NULL;
        jit_return_label = NULL;
    }
    /* Clean up. */
    tc->active_handlers = ah->next_handler;
    MVM_free(ah);

    /* Do the unwinding as needed. */
    if (exception && exception->body.return_after_unwind) {
        /* we can't very well return to our the unwod JIT address */
        MVM_frame_unwind_to(tc, frame->caller, NULL, 0, tc->last_handler_result, NULL, 1);
    }
    else {
        MVM_frame_unwind_to(tc, frame, abs_address, goto_offset, NULL, jit_return_label, 1);
    }
}

/* Cleans up an active handler record if we unwind over it. */
static void cleanup_active_handler(MVMThreadContext *tc, void *sr_data) {
    /* Get active handler; sanity check (though it's possible other cases
     * should be supported). */
    MVMActiveHandler *ah = *((MVMActiveHandler **)sr_data);
    if (tc->active_handlers != ah)
        MVM_panic(1, "Trying to unwind over wrong handler");

    /* Clean up. */
    tc->active_handlers = ah->next_handler;
    MVM_free(ah);
}

char * MVM_exception_backtrace_line(MVMThreadContext *tc, MVMFrame *cur_frame,
                                    MVMuint16 not_top, MVMuint8 *throw_address) {
    MVMString *filename = cur_frame->static_info->body.cu->body.filename;
    MVMString *name = cur_frame->static_info->body.name;
    /* XXX TODO: make the caller pass in a char ** and a length pointer so
     * we can update it if necessary, and the caller can cache it. */
    char *o = MVM_malloc(1024);
    MVMuint8 *cur_op = not_top ? cur_frame->return_address : throw_address;
    MVMuint32 offset = cur_op - MVM_frame_effective_bytecode(cur_frame);
    MVMBytecodeAnnotation *annot = MVM_bytecode_resolve_annotation(tc, &cur_frame->static_info->body,
                                        offset > 0 ? offset - 1 : 0);

    MVMuint32 line_number = annot ? annot->line_number : 1;
    MVMuint16 string_heap_index = annot ? annot->filename_string_heap_index : 0;
    char *tmp1 = annot && string_heap_index < cur_frame->static_info->body.cu->body.num_strings
        ? MVM_string_utf8_encode_C_string(tc, MVM_cu_string(tc,
                cur_frame->static_info->body.cu, string_heap_index))
        : NULL;

    char *filename_c = filename
        ? MVM_string_utf8_encode_C_string(tc, filename)
        : "<ephemeral file>";
    char *name_c = name
        ? MVM_string_utf8_encode_C_string(tc, name)
        : "<anonymous frame>";

    snprintf(o, 1024, " %s %s:%u  (%s:%s)",
        not_top ? "from" : "  at",
        tmp1 ? tmp1 : "<unknown>",
        line_number,
        filename_c,
        name_c
    );
    if (filename)
        MVM_free(filename_c);
    if (name)
        MVM_free(name_c);

    if (tmp1)
        MVM_free(tmp1);
    if (annot)
        MVM_free(annot);

    return o;
}

/* Returns a list of hashes containing file, line, sub and annotations. */
MVMObject * MVM_exception_backtrace(MVMThreadContext *tc, MVMObject *ex_obj) {
    MVMFrame *cur_frame;
    MVMSpeshFrameWalker fw;
    MVMObject *arr = NULL, *annotations = NULL, *row = NULL, *value = NULL;
    MVMuint32 count = 0;
    MVMString *k_file = NULL, *k_line = NULL, *k_sub = NULL, *k_anno = NULL;
    MVMuint8 *throw_address;

    if (IS_CONCRETE(ex_obj) && REPR(ex_obj)->ID == MVM_REPR_ID_MVMException) {
        cur_frame = ((MVMException *)ex_obj)->body.origin;
        throw_address = ((MVMException *)ex_obj)->body.throw_address;
    }
    else if (ex_obj == tc->instance->VMNull) {
        cur_frame = tc->cur_frame;
        throw_address = *tc->interp_cur_op;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Op 'backtrace' needs an exception object");
    }

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&arr);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&annotations);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&row);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&value);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&k_file);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&k_line);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&k_sub);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&k_anno);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&cur_frame);

    k_file = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "file");
    k_line = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "line");
    k_sub  = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "sub");
    k_anno = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "annotations");

    arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);

    if (cur_frame) {
        MVM_spesh_frame_walker_init(tc, &fw, cur_frame, 0);
        MVM_spesh_frame_walker_next(tc, &fw); // Go to first inline

        do {
            cur_frame = MVM_spesh_frame_walker_current_frame(tc, &fw);
            MVMuint8             *cur_op = count ? cur_frame->return_address : throw_address;
            MVMuint32             offset = cur_op - MVM_frame_effective_bytecode(cur_frame);
            MVMBytecodeAnnotation *annot = MVM_bytecode_resolve_annotation(tc, &cur_frame->static_info->body,
                                                offset > 0 ? offset - 1 : 0);
            MVMuint32             fshi   = annot ? (MVMint32)annot->filename_string_heap_index : -1;
            MVMString      *filename_str;

            /* annotations hash will contain "file" and "line" */
            annotations = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);

            /* file */
            filename_str = annot && fshi < cur_frame->static_info->body.cu->body.num_strings
                 ? MVM_cu_string(tc, cur_frame->static_info->body.cu, fshi)
                 : cur_frame->static_info->body.cu->body.filename;
            value = MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type,
                filename_str ? filename_str : tc->instance->str_consts.empty);
            MVM_repr_bind_key_o(tc, annotations, k_file, value);

            /* line */
            value = (MVMObject *)MVM_coerce_u_s(tc, annot ? annot->line_number : 1);
            value = MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type, (MVMString *)value);
            MVM_repr_bind_key_o(tc, annotations, k_line, value);

            /* row will contain "sub" and "annotations" */
            row = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
            MVM_repr_bind_key_o(tc, row, k_sub,
                cur_frame->code_ref ? cur_frame->code_ref : tc->instance->VMNull);
            MVM_repr_bind_key_o(tc, row, k_anno, annotations);

            MVM_repr_push_o(tc, arr, row);
            MVM_free(annot);

            count++;
        } while (MVM_spesh_frame_walker_move_caller_skip_thunks(tc, &fw));

        MVM_spesh_frame_walker_cleanup(tc, &fw);
    }

    MVM_gc_root_temp_pop_n(tc, 9);

    return arr;
}

/* Returns the lines (backtrace) of an exception-object as an array. */
MVMObject * MVM_exception_backtrace_strings(MVMThreadContext *tc, MVMObject *ex_obj) {
    MVMException *ex;
    MVMFrame *cur_frame;
    MVMObject *arr;

    if (IS_CONCRETE(ex_obj) && REPR(ex_obj)->ID == MVM_REPR_ID_MVMException)
        ex = (MVMException *)ex_obj;
    else
        MVM_exception_throw_adhoc(tc, "Op 'backtracestrings' needs an exception object");

    MVMROOT(tc, ex, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);

        cur_frame = ex->body.origin;

        MVMROOT2(tc, arr, cur_frame, {
            MVMuint32 count = 0;
            while (cur_frame != NULL) {
                char *line = MVM_exception_backtrace_line(tc, cur_frame, count++,
                    ex->body.throw_address);
                MVMString *line_str = MVM_string_utf8_decode(tc, tc->instance->VMString, line, strlen(line));
                MVMObject *line_obj = MVM_repr_box_str(tc, tc->instance->boot_types.BOOTStr, line_str);
                MVM_repr_push_o(tc, arr, line_obj);
                cur_frame = cur_frame->caller;
                MVM_free(line);
            }
        });
    });

    return arr;
}

/* Dumps a backtrace relative to the current frame to stderr. */
void MVM_dump_backtrace(MVMThreadContext *tc) {
    MVMFrame *cur_frame = tc->cur_frame;
    MVMuint32 count = 0;
    MVMROOT(tc, cur_frame, {
        while (cur_frame != NULL) {
            char *line = MVM_exception_backtrace_line(tc, cur_frame, count++,
                *(tc->interp_cur_op));
            fprintf(stderr, "%s\n", line);
            MVM_free(line);
            cur_frame = cur_frame->caller;
        }
    });
}

/* Panic over an unhandled exception throw by category. */
static void panic_unhandled_cat(MVMThreadContext *tc, MVMuint32 cat) {
    /* If it's a control exception, try promoting it to a catch one. */
    if (cat != MVM_EX_CAT_CATCH) {
        MVM_exception_throw_adhoc(tc, "No exception handler located for %s",
            cat_name(tc, cat));
    }
    else {
        if (tc->nested_interpreter)
            fputs("An unhandled exception occurred in a native callback.\n"
                  "This situation is not recoverable, and the program will terminate.\n"
                  "The stack trace below helps indicate which library needs fixing.\n"
                  "The exception was thrown at:\n", stderr);
        else
            fprintf(stderr, "No exception handler located for %s\n", cat_name(tc, cat));
        MVM_dump_backtrace(tc);
        if (crash_on_error)
            abort();
        else
            exit(1);
    }
}

/* Panic over an unhandled exception object. */
static void panic_unhandled_ex(MVMThreadContext *tc, MVMException *ex) {
    char *backtrace;

    /* If a debug session is running, notify the client. */
    MVMROOT(tc, ex, {
        MVM_debugserver_notify_unhandled_exception(tc, ex);
    });

    /* If it's a control exception, try promoting it to a catch one; use
     * the category name. */
    if (ex->body.category != MVM_EX_CAT_CATCH)
        panic_unhandled_cat(tc, ex->body.category);

    /* If there's no message, fall back to category also. */
    if (!ex->body.message)
        panic_unhandled_cat(tc, ex->body.category);

    /* Otherwise, dump message and a backtrace. */
    backtrace = MVM_string_utf8_encode_C_string(tc, ex->body.message);
    fprintf(stderr, "Unhandled exception: %s\n", backtrace);
    MVM_free(backtrace);
    MVM_dump_backtrace(tc);
    if (crash_on_error)
        abort();
    else
        exit(1);
}

/* Checks if we're throwing lexically, and - if yes - if the current HLL has
 * a handler for unlocated lexical handlers. */
static MVMint32 use_lexical_handler_hll_error(MVMThreadContext *tc, MVMuint8 mode) {
    return (mode == MVM_EX_THROW_LEX || mode == MVM_EX_THROW_LEX_CALLER) &&
        !MVM_is_null(tc, (MVMObject *)MVM_hll_current(tc)->lexical_handler_not_found_error);
}

/* Invokes the HLL's handler for unresolved lexical throws. */
static void invoke_lexical_handler_hll_error(MVMThreadContext *tc, MVMint64 cat, LocatedHandler lh) {
    MVMCode *handler = MVM_hll_current(tc)->lexical_handler_not_found_error;
    MVMCallStackArgsFromC *args_record = MVM_callstack_allocate_args_from_c(tc,
            MVM_callsite_get_common(tc, MVM_CALLSITE_ID_INT_INT));
    args_record->args.source[0].i64 = cat;
    args_record->args.source[1].i64 = lh.handler_out_of_dynamic_scope;
    MVM_frame_dispatch_from_c(tc, handler, args_record, NULL, MVM_RETURN_VOID);
}

/* Throws an exception by category, searching for a handler according to
 * the specified mode. If the handler resumes, the resumption result will
 * be put into resume_result. Leaves the interpreter in a state where it
 * will next run the instruction of the handler. If there is no handler,
 * it will panic and exit with a backtrace. */
void MVM_exception_throwcat(MVMThreadContext *tc, MVMuint8 mode, MVMuint32 cat, MVMRegister *resume_result) {
    LocatedHandler lh = search_for_handler_from(tc, tc->cur_frame, mode, cat, NULL);
    if (lh.frame == NULL) {
        if (use_lexical_handler_hll_error(tc, mode)) {
            invoke_lexical_handler_hll_error(tc, cat, lh);
            return;
        }
        panic_unhandled_cat(tc, cat);
    }
    run_handler(tc, lh, NULL, cat, NULL);
}

void MVM_exception_die(MVMThreadContext *tc, MVMString *str, MVMRegister *rr) {
    MVMException *ex;
    MVMROOT(tc, str, {
        ex = (MVMException *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTException);
    });
    ex->body.category = MVM_EX_CAT_CATCH;
    MVM_ASSIGN_REF(tc, &(ex->common.header), ex->body.message, str);
    MVM_exception_throwobj(tc, MVM_EX_THROW_DYN, (MVMObject *)ex, rr);
}

/* Throws the specified exception object, taking the category from it. If
 * the handler resumes, the resumption result will be put into resume_result.
 * Leaves the interpreter in a state where it will next run the instruction of
 * the handler. If there is no handler, it will panic and exit with a backtrace. */
void MVM_exception_throwobj(MVMThreadContext *tc, MVMuint8 mode, MVMObject *ex_obj, MVMRegister *resume_result) {
    LocatedHandler  lh;
    MVMException   *ex;

    /* The current frame will be assigned as the thrower of the exception, so
     * force it onto the heap before we begin (promoting it later would mean
     * outer handler search result would be outdated). */
    MVMROOT(tc, ex_obj, {
        MVM_frame_force_to_heap(tc, tc->cur_frame);
    });

    if (IS_CONCRETE(ex_obj) && REPR(ex_obj)->ID == MVM_REPR_ID_MVMException)
        ex = (MVMException *)ex_obj;
    else
        MVM_exception_throw_adhoc(tc, "Can only throw an exception object");

    if (!ex->body.category)
        ex->body.category = MVM_EX_CAT_CATCH;
    if (resume_result) {
        ex->body.resume_addr = *tc->interp_cur_op;
        /* Ensure that we store label where the JIT should return, if any */
        if (tc->jit_return_address != NULL) {
            ex->body.jit_resume_label = MVM_jit_code_get_current_position(tc, tc->cur_frame->spesh_cand->body.jitcode, tc->cur_frame);
        }
    }
    lh = search_for_handler_from(tc, tc->cur_frame, mode, ex->body.category, ex->body.payload);
    if (lh.frame == NULL) {
        if (use_lexical_handler_hll_error(tc, mode)) {
            invoke_lexical_handler_hll_error(tc, ex->body.category, lh);
            return;
        }
        panic_unhandled_ex(tc, ex);
    }

    if (!ex->body.origin) {
        MVM_ASSIGN_REF(tc, &(ex->common.header), ex->body.origin, tc->cur_frame);
        ex->body.throw_address = *(tc->interp_cur_op);
    }

    run_handler(tc, lh, ex_obj, 0, NULL);
}

/* Throws an exception of the specified category and with the specified payload.
 * If a goto or payload handler exists, then no exception object will be created. */
void MVM_exception_throwpayload(MVMThreadContext *tc, MVMuint8 mode, MVMuint32 cat, MVMObject *payload, MVMRegister *resume_result) {
    LocatedHandler lh = search_for_handler_from(tc, tc->cur_frame, mode, cat, NULL);
    if (lh.frame == NULL) {
        if (use_lexical_handler_hll_error(tc, mode)) {
            invoke_lexical_handler_hll_error(tc, cat, lh);
            return;
        }
        panic_unhandled_cat(tc, cat);
    }
    run_handler(tc, lh, NULL, cat, payload);
}

void MVM_exception_resume(MVMThreadContext *tc, MVMObject *ex_obj) {
    MVMException *ex;
    if (IS_CONCRETE(ex_obj) && REPR(ex_obj)->ID == MVM_REPR_ID_MVMException)
        ex = (MVMException *)ex_obj;
    else
        MVM_exception_throw_adhoc(tc, "Can only resume an exception object");

    /* Check that everything is in place to do the resumption. */
    if (!ex->body.resume_addr)
        MVM_exception_throw_adhoc(tc, "This exception is not resumable");
    MVMFrame *target = ex->body.origin;
    if (!target)
        MVM_exception_throw_adhoc(tc, "This exception is not resumable");
    if (!in_caller_chain(tc, target))
        MVM_exception_throw_adhoc(tc, "Too late to resume this exception");

    /* Check that this is the exception we're currently handling. */
    if (!tc->active_handlers)
        MVM_exception_throw_adhoc(tc, "Can only resume an exception in its handler");
    if (tc->active_handlers->ex_obj != ex_obj)
        MVM_exception_throw_adhoc(tc, "Can only resume the current exception");

    /* Unwind to the thrower of the exception; set PC and jit entry label. */
    MVM_frame_unwind_to(tc, target, ex->body.resume_addr, 0, NULL, ex->body.jit_resume_label, 1);
}

/* Panics and shuts down the VM. Don't do this unless it's something quite
 * unrecoverable, and a thread context is either not available or stands a
 * good chance of being too corrupt to print (or is not relevant information).
 * Use MVM_oops in the case a thread context is available.
 * TODO: Some hook for embedders.
 */
MVM_NO_RETURN void MVM_panic(MVMint32 exitCode, const char *messageFormat, ...) {
    va_list args;
    fputs("MoarVM panic: ", stderr);
    va_start(args, messageFormat);
    vfprintf(stderr, messageFormat, args);
    va_end(args);
    fputc('\n', stderr);
    if (crash_on_error)
        abort();
    else
        exit(exitCode);
}

MVM_NO_RETURN void MVM_panic_allocation_failed(size_t len) {
    MVM_panic(1, "Memory allocation failed; could not allocate %"MVM_PRSz" bytes", len);
}

/* A kinder MVM_panic() that doesn't assume our memory is corrupted (but does kill the
 * process to indicate that we've made an error */
MVM_NO_RETURN void MVM_oops(MVMThreadContext *tc, const char *messageFormat, ...) {
    va_list args;
    fprintf(stderr, "MoarVM oops%s: ",
            !tc ? " with NULL tc" :
            (MVMObject *) tc->thread_obj == tc->instance->spesh_thread
            ? " in spesh thread" :
            (MVMObject *) tc->thread_obj == tc->instance->event_loop_thread
            ? " in event loop thread" : "");
    va_start(args, messageFormat);
    vfprintf(stderr, messageFormat, args);
    va_end(args);
    fputc('\n', stderr);

    /* Our caller is seriously buggy if tc is NULL */
    if (!tc)
        abort();

    MVM_dump_backtrace(tc);
    fputc('\n', stderr);
    exit(1);
}

/* Throws an ad-hoc (untyped) exception. */
MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) {
    va_list args;
    va_start(args, messageFormat);
    MVM_exception_throw_adhoc_free_va(tc, NULL, messageFormat, args);
    va_end(args);
}

/* Throws an ad-hoc (untyped) exception. */
MVM_NO_RETURN void MVM_exception_throw_adhoc_va(MVMThreadContext *tc, const char *messageFormat, va_list args) {
    MVM_exception_throw_adhoc_free_va(tc, NULL, messageFormat, args);
}

/* Throws an ad-hoc (untyped) exception, taking a NULL-terminated array of
 * char pointers to deallocate after message construction. */
MVM_NO_RETURN void MVM_exception_throw_adhoc_free(MVMThreadContext *tc, char **waste, const char *messageFormat, ...) {
    va_list args;
    va_start(args, messageFormat);
    MVM_exception_throw_adhoc_free_va(tc, waste, messageFormat, args);
    va_end(args);
}

/* Throws an ad-hoc (untyped) exception, taking a NULL-terminated array of
 * char pointers to deallocate after message construction. */
MVM_NO_RETURN void MVM_exception_throw_adhoc_free_va(MVMThreadContext *tc, char **waste, const char *messageFormat, va_list args) {
    LocatedHandler lh;
    MVMException *ex;
    const char *special = !tc ? " with NULL tc"
        : (MVMObject *) tc->thread_obj == tc->instance->spesh_thread ? " in spesh thread"
        : (MVMObject *) tc->thread_obj == tc->instance->event_loop_thread ? " in event loop thread" : NULL;

    if (special) {
        fprintf(stderr, "MoarVM exception%s treated as oops: ", special);
        vfprintf(stderr, messageFormat, args);
        va_end(args);
        fputc('\n', stderr);
        if (!tc)
            abort();

        MVM_dump_backtrace(tc);
        fputc('\n', stderr);
        exit(1);
    }

    /* The current frame will be assigned as the thrower of the exception, so
     * force it onto the heap before we begin. */
    if (tc->cur_frame)
        MVM_frame_force_to_heap(tc, tc->cur_frame);

    /* Create and set up an exception object. */
    ex = (MVMException *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTException);
    MVMROOT(tc, ex, {
        char      *c_message = MVM_malloc(1024);
        int        bytes     = vsnprintf(c_message, 1024, messageFormat, args);
        int        to_encode = bytes > 1024 ? 1024 : bytes;
        MVMString *message   = MVM_string_utf8_decode(tc, tc->instance->VMString, c_message, to_encode);
        MVM_free(c_message);

        /* Clean up after ourselves to avoid leaking C strings. */
        if (waste) {
            while(*waste)
                MVM_free(*waste++);
        }

        MVM_ASSIGN_REF(tc, &(ex->common.header), ex->body.message, message);
        if (tc->cur_frame) {
            ex->body.origin = tc->cur_frame;
            ex->body.throw_address = *(tc->interp_cur_op);
        }
        else {
            ex->body.origin = NULL;
        }
        ex->body.category = MVM_EX_CAT_CATCH;
    });

    /* Try to locate a handler, so long as we're in the interpreter. */
    if (tc->interp_cur_op)
        lh = search_for_handler_from(tc, tc->cur_frame, MVM_EX_THROW_DYN, ex->body.category, NULL);
    else
        lh.frame = NULL;

    /* Do we have a handler to unwind to? */
    if (lh.frame == NULL) {
        /* No handler. Should we crash on these? */
        if (crash_on_error) {
            /* Yes, abort. */
            vfprintf(stderr, messageFormat, args);
            fputc('\n', stderr);
            MVM_dump_backtrace(tc);
            abort();
        }
        else {
            /* No, just the usual panic. */
            panic_unhandled_ex(tc, ex);
        }
    }

    /* Run the handler, which doesn't actually run it but rather sets up the
     * interpreter so that when we return to it, we'll be at the handler. */
    run_handler(tc, lh, (MVMObject *)ex, MVM_EX_CAT_CATCH, NULL);

    /* Clear any C stack temporaries that code may have pushed before throwing
     * the exception, and release any needed mutex. */
    MVM_gc_root_temp_pop_all(tc);
    MVM_tc_release_ex_release_mutex(tc);

    /* Jump back into the interpreter. */
    longjmp(tc->interp_jump, 1);
}

void MVM_crash_on_error(void) {
    crash_on_error = 1;
}

MVMint32 MVM_get_exception_category(MVMThreadContext *tc, MVMObject *ex) {
    if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
        return ((MVMException *)ex)->body.category;
    else
        MVM_exception_throw_adhoc(tc, "getexcategory needs a VMException, got %s (%s)", REPR(ex)->name, MVM_6model_get_debug_name(tc, ex));
}

MVMObject * MVM_get_exception_payload(MVMThreadContext *tc, MVMObject *ex) {
    MVMObject *result;
    if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
        result = ((MVMException *)ex)->body.payload;
    else
        MVM_exception_throw_adhoc(tc, "getexpayload needs a VMException, got %s (%s)", REPR(ex)->name, MVM_6model_get_debug_name(tc, ex));
    if (!result)
        result = tc->instance->VMNull;
    return result;
}

MVMString * MVM_get_exception_message(MVMThreadContext *tc, MVMObject *ex) {
    if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
        return ((MVMException *)ex)->body.message;
    else
        MVM_exception_throw_adhoc(tc, "getexmessage needs a VMException, got %s (%s)", REPR(ex)->name, MVM_6model_get_debug_name(tc, ex));
}

void MVM_bind_exception_message(MVMThreadContext *tc, MVMObject *ex, MVMString *message) {
    if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException) {
        MVM_ASSIGN_REF(tc, &(ex->header), ((MVMException *)ex)->body.message,
            message);
    }
    else {
        MVM_exception_throw_adhoc(tc, "bindexmessage needs a VMException, got %s (%s)", REPR(ex)->name, MVM_6model_get_debug_name(tc, ex));
    }
}

void MVM_bind_exception_payload(MVMThreadContext *tc, MVMObject *ex, MVMObject *payload) {
    if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException) {
        MVM_ASSIGN_REF(tc, &(ex->header), ((MVMException *)ex)->body.payload,
                payload);
    }
    else {
        MVM_exception_throw_adhoc(tc, "bindexpayload needs a VMException, got %s (%s)", REPR(ex)->name, MVM_6model_get_debug_name(tc, ex));
    }
}

void MVM_bind_exception_category(MVMThreadContext *tc, MVMObject *ex, MVMint32 category) {
    if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
        ((MVMException *)ex)->body.category = category;
    else
        MVM_exception_throw_adhoc(tc, "bindexcategory needs a VMException, got %s (%s)", REPR(ex)->name, MVM_6model_get_debug_name(tc, ex));
}
void MVM_exception_returnafterunwind(MVMThreadContext *tc, MVMObject *ex) {
    if (IS_CONCRETE(ex) && REPR(ex)->ID == MVM_REPR_ID_MVMException)
        ((MVMException *)ex)->body.return_after_unwind = 1;
    else
        MVM_exception_throw_adhoc(tc, "exreturnafterunwind needs a VMException, got %s (%s)", REPR(ex)->name, MVM_6model_get_debug_name(tc, ex));
}
