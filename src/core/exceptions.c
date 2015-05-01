#include "moar.h"
#include <stdarg.h>

#if _MSC_VER
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif

static int crash_on_error = 0;

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
 * for it, but we can use the same thing the GC uses to know if to scan the
 * work area. */
static MVMuint8 in_caller_chain(MVMThreadContext *tc, MVMFrame *f_maybe) {
    return f_maybe->tc ? 1 : 0;
}


/* Information about a located handler. */
typedef struct {
    MVMFrame        *frame;
    MVMFrameHandler *handler;
    MVMJitHandler   *jit_handler;
} LocatedHandler;

static MVMint32 handler_can_handle(MVMFrame *f, MVMFrameHandler *fh, MVMint32 cat, MVMObject *payload) {
    MVMuint32         category_mask = fh->category_mask;
    MVMuint64       block_has_label = category_mask & MVM_EX_CAT_LABELED;
    MVMuint64           block_label = block_has_label ? (MVMuint64)(f->work[fh->label_reg].o) : 0;
    MVMuint64          thrown_label = payload ? (MVMuint64)payload : 0;
    MVMuint64 identical_label_found = thrown_label == block_label;
    return ((cat & category_mask) == cat && (!(cat & MVM_EX_CAT_LABELED) || identical_label_found))
        || ((category_mask & MVM_EX_CAT_CONTROL) && cat != MVM_EX_CAT_CATCH);
}

/* Looks through the handlers of a particular scope, and sees if one will
 * match what we're looking for. Returns 1 to it if so; if not,
 * returns 0. */
static MVMint32 search_frame_handlers(MVMThreadContext *tc, MVMFrame *f,
                                      MVMuint32 cat, MVMObject *payload,
                                      LocatedHandler *lh) {
    MVMuint32  i;
    if (f->spesh_cand && f->spesh_cand->jitcode && f->jit_entry_label) {
        MVMJitHandler    *jhs = f->spesh_cand->jitcode->handlers;
        MVMFrameHandler  *fhs = f->effective_handlers;
        MVMint32 num_handlers = f->spesh_cand->jitcode->num_handlers;
        void         **labels = f->spesh_cand->jitcode->labels;
        void       *cur_label = f->jit_entry_label;
        for (i = 0; i < num_handlers; i++) {
            if (!handler_can_handle(f, &fhs[i], cat, payload))
                continue;
            if (cur_label >= labels[jhs[i].start_label] &&
                cur_label <= labels[jhs[i].end_label] &&
                !in_handler_stack(tc, &fhs[i], f)) {
                lh->handler     = &fhs[i];
                lh->jit_handler = &jhs[i];
                return 1;
            }
        }
    } else {
        MVMint32 pc;
        if (f == tc->cur_frame)
            pc = (MVMuint32)(*tc->interp_cur_op - *tc->interp_bytecode_start);
        else
            pc = (MVMuint32)(f->return_address - f->effective_bytecode);
        for (i = 0; i < f->static_info->body.num_handlers; i++) {
            MVMFrameHandler  *fh = &f->effective_handlers[i];
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

/* Searches for a handler of the specified category, relative to the given
 * starting frame, searching according to the chosen mode. */
static LocatedHandler search_for_handler_from(MVMThreadContext *tc, MVMFrame *f,
        MVMuint8 mode, MVMuint32 cat, MVMObject *payload) {
    LocatedHandler lh;
    lh.frame = NULL;
    lh.handler = NULL;
    lh.jit_handler = NULL;
    if (mode == MVM_EX_THROW_LEXOTIC) {
        while (f != NULL) {
            lh = search_for_handler_from(tc, f, MVM_EX_THROW_LEX, cat, payload);
            if (lh.frame != NULL)
                return lh;
            f = f->caller;
        }
    }
    else {
        while (f != NULL) {
            if (search_frame_handlers(tc, f, cat, payload, &lh)) {
                lh.frame = f;
                return lh;
            }
            if (mode == MVM_EX_THROW_DYN) {
                f = f->caller;
            }
            else {
                MVMFrame *f_maybe = f->outer;
                while (f_maybe != NULL && !in_caller_chain(tc, f_maybe))
                    f_maybe = f_maybe->outer;
                f = f_maybe;
            }
        }
    }
    return lh;
}

/* Runs an exception handler (which really means updating interpreter state
 * so that when we return to the runloop, we're in the handler). If there is
 * an exception object already, it will be used; NULL can be passed if there
 * is not one, meaning it will be created if needed. */
static void unwind_after_handler(MVMThreadContext *tc, void *sr_data);
static void cleanup_active_handler(MVMThreadContext *tc, void *sr_data);
static void run_handler(MVMThreadContext *tc, LocatedHandler lh, MVMObject *ex_obj) {
    switch (lh.handler->action) {
    case MVM_EX_ACTION_GOTO:
        if (lh.jit_handler) {
            void **labels = lh.frame->spesh_cand->jitcode->labels;
            MVMuint8  *pc = lh.frame->spesh_cand->jitcode->bytecode;
            lh.frame->jit_entry_label = labels[lh.jit_handler->goto_label];
            MVM_frame_unwind_to(tc, lh.frame, pc, 0, NULL);
        } else {
            MVM_frame_unwind_to(tc, lh.frame, NULL, lh.handler->goto_offset, NULL);
        }
        break;

    case MVM_EX_ACTION_INVOKE: {
        /* Create active handler record. */
        MVMActiveHandler *ah = MVM_malloc(sizeof(MVMActiveHandler));
        MVMFrame *cur_frame = tc->cur_frame;

        /* Find frame to invoke. */
        MVMObject *handler_code = MVM_frame_find_invokee(tc, lh.frame->work[lh.handler->block_reg].o, NULL);

        /* Ensure we have an exception object. */
        /* TODO: Can make one up. */
        if (ex_obj == NULL)
            MVM_panic(1, "Exception object creation NYI");

        /* Install active handler record. */
        ah->frame           = MVM_frame_inc_ref(tc, lh.frame);
        ah->handler         = lh.handler;
        ah->jit_handler     = lh.jit_handler;
        ah->ex_obj          = ex_obj;
        ah->next_handler    = tc->active_handlers;
        tc->active_handlers = ah;

        /* Set up special return to unwinding after running the
         * handler. */
        cur_frame->return_value        = (MVMRegister *)&tc->last_handler_result;
        cur_frame->return_type         = MVM_RETURN_OBJ;
        cur_frame->special_return      = unwind_after_handler;
        cur_frame->special_unwind      = cleanup_active_handler;
        cur_frame->special_return_data = ah;

        /* Invoke the handler frame and return to runloop. */
        STABLE(handler_code)->invoke(tc, handler_code, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS),
                                     cur_frame->args);
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


    /* Get active handler; sanity check (though it's possible other cases
     * should be supported). */
    MVMActiveHandler *ah = (MVMActiveHandler *)sr_data;
    if (tc->active_handlers != ah)
        MVM_panic(1, "Trying to unwind from wrong handler");

    /* Grab info we'll need to unwind. */
    frame       = ah->frame;
    exception   = (MVMException *)ah->ex_obj;
    if (ah->jit_handler) {
        void **labels = frame->spesh_cand->jitcode->labels;
        frame->jit_entry_label = labels[ah->jit_handler->goto_label];
        abs_address = frame->spesh_cand->jitcode->bytecode;
        goto_offset = 0;
    }
    else {
        goto_offset = ah->handler->goto_offset;
        abs_address = NULL;
    }
    /* Clean up. */
    tc->active_handlers = ah->next_handler;
    MVM_frame_dec_ref(tc, ah->frame);
    MVM_free(ah);

    /* Do the unwinding as needed. */
    if (exception && exception->body.return_after_unwind) {
        MVM_frame_unwind_to(tc, frame->caller, NULL, 0, tc->last_handler_result);
    }
    else {
        MVM_frame_unwind_to(tc, frame, abs_address, goto_offset, NULL);
    }
}

/* Cleans up an active handler record if we unwind over it. */
static void cleanup_active_handler(MVMThreadContext *tc, void *sr_data) {
    /* Get active handler; sanity check (though it's possible other cases
     * should be supported). */
    MVMActiveHandler *ah = (MVMActiveHandler *)sr_data;
    if (tc->active_handlers != ah)
        MVM_panic(1, "Trying to unwind over wrong handler");

    /* Clean up. */
    tc->active_handlers = ah->next_handler;
    MVM_frame_dec_ref(tc, ah->frame);
    MVM_free(ah);
}

char * MVM_exception_backtrace_line(MVMThreadContext *tc, MVMFrame *cur_frame, MVMuint16 not_top) {
    MVMString *filename = cur_frame->static_info->body.cu->body.filename;
    MVMString *name = cur_frame->static_info->body.name;
    /* XXX TODO: make the caller pass in a char ** and a length pointer so
     * we can update it if necessary, and the caller can cache it. */
    char *o = MVM_malloc(1024);
    MVMuint8 *cur_op = not_top ? cur_frame->return_address : cur_frame->throw_address;
    MVMuint32 offset = cur_op - cur_frame->effective_bytecode;
    MVMuint32 instr = MVM_bytecode_offset_to_instr_idx(tc, cur_frame->static_info, offset);
    MVMBytecodeAnnotation *annot = MVM_bytecode_resolve_annotation(tc, &cur_frame->static_info->body,
                                        offset > 0 ? offset - 1 : 0);

    MVMuint32 line_number = annot ? annot->line_number : 1;
    MVMuint16 string_heap_index = annot ? annot->filename_string_heap_index : 0;
    char *tmp1 = annot && string_heap_index < cur_frame->static_info->body.cu->body.num_strings
        ? MVM_string_utf8_encode_C_string(tc,
            cur_frame->static_info->body.cu->body.strings[string_heap_index])
        : NULL;

    /* We may be mid-instruction if exception was thrown at an unfortunate
     * point; try to cope with that. */
    if (instr == MVM_BC_ILLEGAL_OFFSET && offset >= 2)
        instr = MVM_bytecode_offset_to_instr_idx(tc, cur_frame->static_info, offset - 2);

    snprintf(o, 1024, " %s %s:%u  (%s:%s:%u)",
        not_top ? "from" : "  at",
        tmp1 ? tmp1 : "<unknown>",
        line_number,
        filename ? MVM_string_utf8_encode_C_string(tc, filename) : "<ephemeral file>",
        name ? MVM_string_utf8_encode_C_string(tc, name) : "<anonymous frame>",
        instr
    );

    if (tmp1)
        MVM_free(tmp1);
    if (annot)
        MVM_free(annot);

    return o;
}

/* Returns a list of hashes containing file, line, sub and annotations. */
MVMObject * MVM_exception_backtrace(MVMThreadContext *tc, MVMObject *ex_obj) {
    MVMFrame *cur_frame;
    MVMObject *arr = NULL, *annotations = NULL, *row = NULL, *value = NULL;
    MVMuint32 count = 0;
    MVMString *k_file = NULL, *k_line = NULL, *k_sub = NULL, *k_anno = NULL;

    if (IS_CONCRETE(ex_obj) && REPR(ex_obj)->ID == MVM_REPR_ID_MVMException)
        cur_frame = ((MVMException *)ex_obj)->body.origin;
    else
        MVM_exception_throw_adhoc(tc, "Op 'backtrace' needs an exception object");

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&arr);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&annotations);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&row);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&value);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&k_file);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&k_line);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&k_sub);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&k_anno);

    k_file = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "file");
    k_line = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "line");
    k_sub  = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "sub");
    k_anno = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "annotations");

    arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);

    while (cur_frame != NULL) {
        MVMuint8             *cur_op = count ? cur_frame->return_address : cur_frame->throw_address;
        MVMuint32             offset = cur_op - cur_frame->effective_bytecode;
        MVMBytecodeAnnotation *annot = MVM_bytecode_resolve_annotation(tc, &cur_frame->static_info->body,
                                            offset > 0 ? offset - 1 : 0);
        MVMint32              fshi   = annot ? (MVMint32)annot->filename_string_heap_index : -1;
        char            *line_number = MVM_malloc(16);
        snprintf(line_number, 16, "%d", annot ? annot->line_number : 1);

        /* annotations hash will contain "file" and "line" */
        annotations = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);

        /* file */
        if (fshi >= 0 && fshi < cur_frame->static_info->body.cu->body.num_strings)
            value = MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type,
                        cur_frame->static_info->body.cu->body.strings[fshi]);
        else
            value = MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type,
                        cur_frame->static_info->body.cu->body.filename);
        MVM_repr_bind_key_o(tc, annotations, k_file, value);

        /* line */
        value = (MVMObject *)MVM_string_ascii_decode_nt(tc, tc->instance->VMString, line_number);
        value = MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type, (MVMString *)value);
        MVM_repr_bind_key_o(tc, annotations, k_line, value);
        MVM_free(line_number);

        /* row will contain "sub" and "annotations" */
        row = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
        MVM_repr_bind_key_o(tc, row, k_sub, cur_frame->code_ref);
        MVM_repr_bind_key_o(tc, row, k_anno, annotations);

        MVM_repr_push_o(tc, arr, row);
        MVM_free(annot);

        cur_frame = cur_frame->caller;
        while (cur_frame && cur_frame->static_info->body.is_thunk)
            cur_frame = cur_frame->caller;
        count++;
    }

    MVM_gc_root_temp_pop_n(tc, 8);

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

    cur_frame = ex->body.origin;
    arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);

    MVMROOT(tc, arr, {
        MVMuint32 count = 0;
        while (cur_frame != NULL) {
            char      *line     = MVM_exception_backtrace_line(tc, cur_frame, count++);
            MVMString *line_str = MVM_string_utf8_decode(tc, tc->instance->VMString, line, strlen(line));
            MVMObject *line_obj = MVM_repr_box_str(tc, tc->instance->boot_types.BOOTStr, line_str);
            MVM_repr_push_o(tc, arr, line_obj);
            cur_frame = cur_frame->caller;
            MVM_free(line);
        }
    });

    return arr;
}

/* Dumps a backtrace relative to the current frame to stderr. */
void MVM_dump_backtrace(MVMThreadContext *tc) {
    MVMFrame *cur_frame = tc->cur_frame;
    MVMuint32 count = 0;
    while (cur_frame != NULL) {
        char *line = MVM_exception_backtrace_line(tc, cur_frame, count++);
        fprintf(stderr, "%s\n", line);
        MVM_free(line);
        cur_frame = cur_frame->caller;
    }
}

/* Panic over an unhandled exception throw by category. */
static void panic_unhandled_cat(MVMThreadContext *tc, MVMuint32 cat) {
    /* If it's a control exception, try promoting it to a catch one. */
    if (cat != MVM_EX_CAT_CATCH) {
        MVM_exception_throw_adhoc(tc, "No exception handler located for %s",
            cat_name(tc, cat));
    }
    else {
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
    /* If it's a control exception, try promoting it to a catch one; use
     * the category name. */
    if (ex->body.category != MVM_EX_CAT_CATCH)
        panic_unhandled_cat(tc, ex->body.category);

    /* If there's no message, fall back to category also. */
    if (!ex->body.message)
        panic_unhandled_cat(tc, ex->body.category);

    /* Otherwise, dump message and a backtrace. */
    fprintf(stderr, "Unhandled exception: %s\n",
        MVM_string_utf8_encode_C_string(tc, ex->body.message));
    MVM_dump_backtrace(tc);
    if (crash_on_error)
        abort();
    else
        exit(1);
}

/* Throws an exception by category, searching for a handler according to
 * the specified mode. If the handler resumes, the resumption result will
 * be put into resume_result. Leaves the interpreter in a state where it
 * will next run the instruction of the handler. If there is no handler,
 * it will panic and exit with a backtrace. */
void MVM_exception_throwcat(MVMThreadContext *tc, MVMuint8 mode, MVMuint32 cat, MVMRegister *resume_result) {
    LocatedHandler lh = search_for_handler_from(tc, tc->cur_frame, mode, cat, NULL);
    if (lh.frame == NULL)
        panic_unhandled_cat(tc, cat);
    run_handler(tc, lh, NULL);
}

void MVM_exception_die(MVMThreadContext *tc, MVMString *str, MVMRegister *rr) {
    MVMException *ex = (MVMException *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTException);
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

    if (IS_CONCRETE(ex_obj) && REPR(ex_obj)->ID == MVM_REPR_ID_MVMException)
        ex = (MVMException *)ex_obj;
    else
        MVM_exception_throw_adhoc(tc, "Can only throw an exception object");

    if (!ex->body.category)
        ex->body.category = MVM_EX_CAT_CATCH;
    if (resume_result)
        ex->body.resume_addr = *tc->interp_cur_op;
    lh = search_for_handler_from(tc, tc->cur_frame, mode, ex->body.category, ex->body.payload);
    if (lh.frame == NULL)
        panic_unhandled_ex(tc, ex);

    if (!ex->body.origin) {
        ex->body.origin = MVM_frame_inc_ref(tc, tc->cur_frame);
        tc->cur_frame->throw_address = *(tc->interp_cur_op);
        tc->cur_frame->keep_caller   = 1;
    }

    run_handler(tc, lh, ex_obj);
}

void MVM_exception_resume(MVMThreadContext *tc, MVMObject *ex_obj) {
    MVMException     *ex;
    MVMFrame         *target;
    MVMActiveHandler *ah;

    if (IS_CONCRETE(ex_obj) && REPR(ex_obj)->ID == MVM_REPR_ID_MVMException)
        ex = (MVMException *)ex_obj;
    else
        MVM_exception_throw_adhoc(tc, "Can only resume an exception object");
    
    /* Check that everything is in place to do the resumption. */
    if (!ex->body.resume_addr)
        MVM_exception_throw_adhoc(tc, "This exception is not resumable");
    target = ex->body.origin;
    if (!target)
        MVM_exception_throw_adhoc(tc, "This exception is not resumable");
    if (target->special_return != unwind_after_handler)
        MVM_exception_throw_adhoc(tc, "This exception is not resumable");
    if (!target->tc)
        MVM_exception_throw_adhoc(tc, "Too late to resume this exception");

    /* Check that this is the exception we're currently handling. */
    if (!tc->active_handlers)
        MVM_exception_throw_adhoc(tc, "Can only resume an exception in its handler");
    if (tc->active_handlers->ex_obj != ex_obj)
        MVM_exception_throw_adhoc(tc, "Can only resume the current exception");

    /* Clear special return handler; we'll do its work here. */
    target->special_return = NULL;
    target->special_unwind = NULL;

    /* Clear the current active handler. */
    ah = tc->active_handlers;
    tc->active_handlers = ah->next_handler;
    MVM_frame_dec_ref(tc, ah->frame);
    MVM_free(ah);

    /* Unwind to the thrower of the exception; set PC. */
    MVM_frame_unwind_to(tc, target, ex->body.resume_addr, 0, NULL);
}

static MVMObject* get_lexotic_for_handler_idx(MVMThreadContext *tc, MVMint32 handler_idx) {
    MVMLexotic *lexotic;
    MVMStaticFrame  *sf = tc->cur_frame->static_info;
    /* See if we've got this lexotic cached; return it if so. */
    if (sf->body.pool_index < tc->lexotic_cache_size) {
        lexotic = tc->lexotic_cache[sf->body.pool_index];
        if (lexotic && lexotic->body.handler_idx == handler_idx)
            return (MVMObject *)lexotic;
    }

    /* Allocate lexotic object, set it up, and cache it. */
    MVMROOT(tc, sf, {
        lexotic = (MVMLexotic *)MVM_repr_alloc_init(tc, tc->instance->Lexotic);
    });
    lexotic->body.handler_idx = handler_idx;
    MVM_ASSIGN_REF(tc, &(lexotic->common.header), lexotic->body.sf, sf);
    if (sf->body.pool_index >= tc->lexotic_cache_size) {
        MVMuint32 orig_size = tc->lexotic_cache_size;
        tc->lexotic_cache_size = sf->body.pool_index + 1;
        tc->lexotic_cache = orig_size
            ? MVM_realloc(tc->lexotic_cache, tc->lexotic_cache_size * sizeof(MVMLexotic *))
            : MVM_malloc(tc->lexotic_cache_size * sizeof(MVMLexotic *));
        memset(tc->lexotic_cache + orig_size, 0,
            (tc->lexotic_cache_size - orig_size) * sizeof(MVMLexotic *));
    }
    if (!tc->lexotic_cache[sf->body.pool_index])
        tc->lexotic_cache[sf->body.pool_index] = lexotic;

    return (MVMObject *)lexotic;
}

/* Creates a new lexotic. */
MVMObject * MVM_exception_newlexotic(MVMThreadContext *tc, MVMuint32 offset) {
    /* Locate handler associated with the specified label. */
    MVMFrame       *f     = tc->cur_frame;
    MVMStaticFrame *sf    = f->static_info;
    MVMint32 handler_idx  = -1;
    MVMint32 num_handlers = f->spesh_cand
        ? f->spesh_cand->num_handlers
        : sf->body.num_handlers;
    MVMuint32 i;
    for (i = 0; i < num_handlers; i++) {
        if (f->effective_handlers[i].action == MVM_EX_ACTION_GOTO &&
                f->effective_handlers[i].goto_offset == offset) {
            handler_idx = i;
            break;
        }
    }
    if (handler_idx < 0)
        MVM_exception_throw_adhoc(tc, "Label with no handler passed to newlexotic");
    return get_lexotic_for_handler_idx(tc, handler_idx);
}

/* Creates a new lexotic from the JIT. The JIT doesn't have access to
 * the offset, so we can't find it from within there. */
MVMObject * MVM_exception_newlexotic_from_jit(MVMThreadContext *tc, MVMint32 label) {
    /* Locate handler associated with the specified label. */
    MVMFrame       *f       = tc->cur_frame;
    MVMint32 handler_idx    = -1;
    MVMint32 num_handlers   = f->spesh_cand->jitcode->num_handlers;
    MVMJitHandler *handlers = f->spesh_cand->jitcode->handlers;
    MVMuint32 i;
    for (i = 0; i < num_handlers; i++) {
        if (f->effective_handlers[i].action == MVM_EX_ACTION_GOTO &&
            handlers[i].goto_label == label) {
            handler_idx = i;
            break;
        }
    }
    if (handler_idx < 0)
        MVM_exception_throw_adhoc(tc, "Label with no handler passed to newlexotic");
    return get_lexotic_for_handler_idx(tc, handler_idx);
}


/* Unwinds to a lexotic captured handler. */
void MVM_exception_gotolexotic(MVMThreadContext *tc, MVMint32 handler_idx, MVMStaticFrame *sf) {
    MVMFrame *f, *search;
    search = tc->cur_frame;
    while (search) {
        f = search;
        while (f) {
            if (f->static_info == sf)
                break;
            f = f->outer;
        }
        if (f)
            break;
        search = search->caller;
    }
    if (f && in_caller_chain(tc, f)) {
        LocatedHandler lh;
        lh.frame = f;
        lh.handler = &(f->effective_handlers[handler_idx]);
        if (f->spesh_cand && f->spesh_cand->jitcode)
            lh.jit_handler = &(f->spesh_cand->jitcode->handlers[handler_idx]);
        else
            lh.jit_handler = NULL;
        run_handler(tc, lh, NULL);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Too late to invoke lexotic return");
    }
}

/* Panics and shuts down the VM. Don't do this unless it's something quite
 * unrecoverable.
 * TODO: Some hook for embedders.
 */
MVM_NO_RETURN
void MVM_panic(MVMint32 exitCode, const char *messageFormat, ...) {
    va_list args;
    va_start(args, messageFormat);
    vfprintf(stderr, messageFormat, args);
    va_end(args);
    fwrite("\n", 1, 1, stderr);
    if (crash_on_error)
        abort();
    else
        exit(exitCode);
}

MVM_NO_RETURN
void MVM_panic_allocation_failed(size_t len) {
    MVM_panic(1, "Memory allocation failed; could not allocate %"MVM_PRSz" bytes", len);
}

/* Throws an ad-hoc (untyped) exception. */
MVM_NO_RETURN
void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) {
    va_list args;
    va_start(args, messageFormat);
    MVM_exception_throw_adhoc_va(tc, messageFormat, args);
    va_end(args);
}

/* Throws an ad-hoc (untyped) exception. */
MVM_NO_RETURN
void MVM_exception_throw_adhoc_va(MVMThreadContext *tc, const char *messageFormat, va_list args) {
    LocatedHandler lh;

    /* Create and set up an exception object. */
    MVMException *ex = (MVMException *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTException);
    MVMROOT(tc, ex, {
        char      *c_message = MVM_malloc(1024);
        int        bytes     = vsnprintf(c_message, 1024, messageFormat, args);
        MVMString *message   = MVM_string_utf8_decode(tc, tc->instance->VMString, c_message, bytes);
        MVM_free(c_message);
        MVM_ASSIGN_REF(tc, &(ex->common.header), ex->body.message, message);
        if (tc->cur_frame) {
            ex->body.origin = MVM_frame_inc_ref(tc, tc->cur_frame);
            tc->cur_frame->throw_address = *(tc->interp_cur_op);
            tc->cur_frame->keep_caller   = 1;
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
            fwrite("\n", 1, 1, stderr);
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
    run_handler(tc, lh, (MVMObject *)ex);

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
