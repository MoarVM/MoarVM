#include "moarvm.h"
#include <stdarg.h>

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
        default:
            return "unknown";
    }
}

/* Checks if an exception handler is already on the active handler stack,
 * so we don't re-trigger the same exception handler. */
static MVMuint8 in_handler_stack(MVMThreadContext *tc, MVMFrameHandler *fh) {
    /* XXX TODO: Implement this check. */
    return 0;
}

/* Checks if a frame is still active. Naively, we could scan the call stack
 * for it, but we can use the same thing the GC uses to know if to scan the
 * work area. */
static MVMuint8 in_caller_chain(MVMThreadContext *tc, MVMFrame *f_maybe) {
    return f_maybe->tc ? 1 : 0;
}

/* Looks through the handlers of a particular scope, and sees if one will
 * match what we're looking for. Returns a pointer to it if so; if not,
 * returns NULL. */
static MVMFrameHandler * search_frame_handlers(MVMThreadContext *tc, MVMFrame *f, MVMuint32 cat) {
    MVMStaticFrame *sf = f->static_info;
    MVMuint32 pc, i;
    if (f == tc->cur_frame)
        pc = (MVMuint32)(*tc->interp_cur_op - *tc->interp_bytecode_start);
    else
        pc = (MVMuint32)(f->return_address - sf->bytecode);
    for (i = 0; i < sf->num_handlers; i++) {
        if (sf->handlers[i].category_mask & cat)
            if (pc >= sf->handlers[i].start_offset && pc < sf->handlers[i].end_offset)
                if (!in_handler_stack(tc, &sf->handlers[i]))
                    return &sf->handlers[i];
    }
    return NULL;
}

/* Information about a located handler. */
typedef struct {
    MVMFrame        *frame;
    MVMFrameHandler *handler;
} LocatedHandler;

/* Searches for a handler of the specified category, relative to the given
 * starting frame, searching according to the chosen mode. */
static LocatedHandler search_for_handler_from(MVMThreadContext *tc, MVMFrame *f,
        MVMuint8 mode, MVMuint32 cat) {
    LocatedHandler lh;
    lh.frame = NULL;
    lh.handler = NULL;

    if (mode == MVM_EX_THROW_LEXOTIC) {
        while (f != NULL) {
            lh = search_for_handler_from(tc, f, MVM_EX_THROW_LEX, cat);
            if (lh.frame != NULL)
                return lh;
            f = f->caller;
        }
    }
    else {
        while (f != NULL) {
            MVMFrameHandler *h = search_frame_handlers(tc, f, cat);
            if (h != NULL) {
                lh.frame = f;
                lh.handler = h;
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

/* Unwinds execution state to the specified frame. */
static void unwind_to_frame(MVMThreadContext *tc, MVMFrame *target) {
    while (tc->cur_frame != target)
        if (!MVM_frame_try_unwind(tc))
            MVM_panic(1, "Internal error: Unwound entire stack and missed handler");
}

/* Dummy, 0-arg callsite for invoking handlers. */
static MVMCallsite no_arg_callsite = { NULL, 0, 0 };

/* Runs an exception handler (which really means updating interpreter state
 * so that when we return to the runloop, we're in the handler). If there is
 * an exception object already, it will be used; NULL can be passed if there
 * is not one, meaning it will be created if needed. */
static void unwind_after_handler(MVMThreadContext *tc, void *sr_data);
static void run_handler(MVMThreadContext *tc, LocatedHandler lh, MVMObject *ex_obj) {
    switch (lh.handler->action) {
        case MVM_EX_ACTION_GOTO:
            unwind_to_frame(tc, lh.frame);
            *tc->interp_cur_op = *tc->interp_bytecode_start + lh.handler->goto_offset;
            break;
        case MVM_EX_ACTION_INVOKE: {
            /* Create active handler record. */
            MVMActiveHandler *ah = malloc(sizeof(MVMActiveHandler));

            /* Find frame to invoke. */
            MVMObject *handler_code = MVM_frame_find_invokee(tc,
                lh.frame->work[lh.handler->block_reg].o);

            /* Ensure we have an exception object. */
            /* TODO: Can make one up. */
            if (ex_obj == NULL)
                MVM_panic(1, "Exception object creation NYI");

            /* Install active handler record. */
            ah->frame = lh.frame;
            ah->handler = lh.handler;
            ah->ex_obj = ex_obj;
            ah->next_handler = tc->active_handlers;
            tc->active_handlers = ah;

            /* Set up special return to unwinding after running the
             * handler. */
            tc->cur_frame->return_value        = NULL;
            tc->cur_frame->return_type         = MVM_RETURN_VOID;
            tc->cur_frame->special_return      = unwind_after_handler;
            tc->cur_frame->special_return_data = ah;

            /* Inovke the handler frame and return to runloop. */
            STABLE(handler_code)->invoke(tc, handler_code, &no_arg_callsite,
                tc->cur_frame->args);
            break;
        }
        default:
            MVM_panic(1, "Unimplemented handler action");
    }
}

/* Unwinds after a handler. */
static void unwind_after_handler(MVMThreadContext *tc, void *sr_data) {
    /* Get active handler; sanity check (though it's possible other cases
     * should be supported). */
    MVMActiveHandler *ah = (MVMActiveHandler *)sr_data;
    if (tc->active_handlers != ah)
        MVM_panic(1, "Trying to unwind from wrong handler");
    tc->active_handlers = ah->next_handler;

    /* Do the unwinding as needed. */
    unwind_to_frame(tc, ah->frame);
    *tc->interp_cur_op = *tc->interp_bytecode_start + ah->handler->goto_offset;

    /* Clean up. */
    free(ah);
}

/* Returns the lines (backtrace) of an exception-object as an array. */
MVMObject * MVM_exception_backtrace_strings(MVMThreadContext *tc, MVMObject *ex_obj) {
    MVMException *ex;
    MVMFrame *cur_frame;
    MVMObject *arr;

    if (IS_CONCRETE(ex_obj) && REPR(ex_obj)->ID == MVM_REPR_ID_MVMException)
        ex = (MVMException *)ex_obj;
    else
        MVM_exception_throw_adhoc(tc, "Can only throw an exception object");

    cur_frame = ex->body.origin;
    arr = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTArray);

    MVMROOT(tc, arr, {
        while (cur_frame != NULL) {
            MVMObject *pobj = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTStr);
            MVM_repr_set_str(tc, pobj, cur_frame->static_info->name);
            MVM_repr_push_o(tc, arr, pobj);
            cur_frame = cur_frame->caller;
        }
    });

    return arr;
}

/* Dumps a backtrace relative to the current frame to stderr. */
static void dump_backtrace(MVMThreadContext *tc) {
    MVMFrame *cur_frame = tc->cur_frame;
    while (cur_frame != NULL) {
        fprintf(stderr, "  in %s\n",
            MVM_string_utf8_encode(tc, cur_frame->static_info->name, NULL));
        cur_frame = cur_frame->caller;
    }
}

/* Panic over an unhandled exception throw by category. */
static void panic_unhandled_cat(MVMThreadContext *tc, MVMuint32 cat) {
    fprintf(stderr, "No exception handler located for %s\n", cat_name(tc, cat));
    dump_backtrace(tc);
    exit(1);
}

/* Panic over an unhandled exception object. */
static void panic_unhandled_ex(MVMThreadContext *tc, MVMException *ex) {
    /* If there's no message, fall back to category. */
    if (!ex->body.message)
        panic_unhandled_cat(tc, ex->body.category);

    /* Otherwise, dump message and a backtrace. */
    fprintf(stderr, "Unhandled exception: %s\n",
        MVM_string_utf8_encode_C_string(tc, ex->body.message));
    dump_backtrace(tc);
    exit(1);
}

/* Throws an exception by category, searching for a handler according to
 * the specified mode. If the handler resumes, the resumption result will
 * be put into resume_result. Leaves the interpreter in a state where it
 * will next run the instruction of the handler. If there is no handler,
 * it will panic and exit with a backtrace. */
void MVM_exception_throwcat(MVMThreadContext *tc, MVMuint8 mode, MVMuint32 cat, MVMRegister *resume_result) {
    LocatedHandler lh = search_for_handler_from(tc, tc->cur_frame, mode, cat);
    if (lh.frame == NULL)
        panic_unhandled_cat(tc, cat);
    run_handler(tc, lh, NULL);
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

    lh = search_for_handler_from(tc, tc->cur_frame, mode, ex->body.category);
    if (lh.frame == NULL)
        panic_unhandled_ex(tc, ex);

    ex->body.origin = MVM_frame_inc_ref(tc, tc->cur_frame);

    run_handler(tc, lh, ex_obj);
}

/* Creates a new lexotic. */
MVMObject * MVM_exception_newlexotic(MVMThreadContext *tc, MVMuint32 offset) {
    MVMLexotic *lexotic;

    /* Locate handler associated with the specified label. */
    MVMStaticFrame *sf = tc->cur_frame->static_info;
    MVMFrameHandler *h = NULL;
    MVMuint32 i;
    for (i = 0; i < sf->num_handlers; i++) {
        if (sf->handlers[i].action == MVM_EX_ACTION_GOTO &&
                sf->handlers[i].goto_offset == offset) {
            h = &sf->handlers[i];
            break;
        }
    }
    if (h == NULL)
        MVM_exception_throw_adhoc(tc, "Label with no handler passed to newlexotic");

    /* Allocate lexotic object and set it up. */
    lexotic = (MVMLexotic *)MVM_repr_alloc_init(tc, tc->instance->Lexotic);
    lexotic->body.handler = h;
    lexotic->body.frame = MVM_frame_inc_ref(tc, tc->cur_frame);

    return (MVMObject *)lexotic;
}

/* Unwinds to a lexotic captured handler. */
void MVM_exception_gotolexotic(MVMThreadContext *tc, MVMFrameHandler *h, MVMFrame *f) {
    if (in_caller_chain(tc, f)) {
        LocatedHandler lh;
        lh.frame = f;
        lh.handler = h;
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
    exit(exitCode);
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
    /* Needs plugging in to the exceptions mechanism. */
    vfprintf(stderr, messageFormat, args);
    fwrite("\n", 1, 1, stderr);
    dump_backtrace(tc);
    exit(1);
}

/* Throws an ad-hoc (untyped) formatted exception with an apr error appended. */
MVM_NO_RETURN
void MVM_exception_throw_apr_error(MVMThreadContext *tc, apr_status_t code, const char *messageFormat, ...) {
    /* Needs plugging in to the exceptions mechanism. */
    char *error_string = malloc(512);
    int offset;
    va_list args;
    va_start(args, messageFormat);

    /* inject the supplied formatted string */
    offset = vsprintf(error_string, messageFormat, args);
    va_end(args);

    /* append the apr error */
    apr_strerror(code, error_string + offset, 512 - offset);
    fwrite(error_string, 1, strlen(error_string), stderr);
    fwrite("\n", 1, 1, stderr);
    free(error_string);

    dump_backtrace(tc);
    exit(1);
}
