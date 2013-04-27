#include "moarvm.h"
#include <stdarg.h>

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

/* Runs an exception handler (which really means updating interpreter state
 * so that when we return to the runloop, we're in the handler). */
static void run_handler(MVMThreadContext *tc, LocatedHandler lh) {
    switch (lh.handler->action) {
        case MVM_EX_ACTION_GOTO:
            unwind_to_frame(tc, lh.frame);
            *tc->interp_cur_op = *tc->interp_bytecode_start + lh.handler->goto_offset;
            break;
        default:
            MVM_panic(1, "Unimplemented handler action");
    }
}

/* Panic over an unhandled exception throw by category. */
static void panic_unhandled_cat(MVMThreadContext *tc, MVMuint32 cat) {
    /* XXX TODO: Backtrace, turn cat into something meaningful. */
    MVM_panic(1, "No exception handler located (category %d)", cat);
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
    run_handler(tc, lh);
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
        run_handler(tc, lh);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Too late to invoke lexotic return");
    }
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
