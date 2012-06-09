#include "moarvm.h"

/* Obtains a call frame ready to be filled out. Makes no promises that the
 * frame data structure will be zeroed out. */
static MVMFrame * obtain_frame(MVMThreadContext *tc) {
    /* XXX Don't need to malloc every time; maintain a free list. */
    return malloc(sizeof(MVMFrame));
}

/* Takes a static frame and does various one-off calculations about what
 * space it shall need. Also triggers bytecode verification of the frame's
 * bytecode. */
void prepare_and_verify_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    /* Calculate lexicals storage needed. */
    static_frame->env_size = static_frame->num_lexicals * sizeof(MVMRegister);
    
    /* Work size is number of locals/registers plus size of the maximum
     * call site argument list. */
    static_frame->work_size = static_frame->num_locals * sizeof(MVMRegister) +
        0; /* XXX Callsite argument list not yet factored in... */
        
    /* XXX Trigger bytecode verficiation. */
    
    /* Mark frame as invoked, so we need not do these calulations again. */
    static_frame->invoked = 1;
}

/* Takes a static frame and a thread context. Invokes the static frame. */
void MVM_frame_invoke(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    /* Get a fresh frame data structure. */
    MVMFrame *frame = obtain_frame(tc);
    
    /* If the frame was never invoked before, need initial calculations
     * and verification. */
    if (!static_frame->invoked)
        prepare_and_verify_static_frame(tc, static_frame);
    
    /* Copy thread context into the frame. */
    frame->tc = tc;
    
    /* Allocate space for lexicals and work area. */
    /* XXX Do something better than malloc here some day. */
    frame->env  = static_frame->env_size ?
        malloc(static_frame->env_size) :
        NULL;
    frame->work = static_frame->work_size ?
        malloc(static_frame->work_size) :
        NULL;
    
    /* Calculate args buffer position. */
    frame->args = static_frame->work_size ?
        frame->work + static_frame->num_locals :
        NULL;

    /* XXX Outer. */
    frame->outer = NULL;
    
    /* Caller is current frame in the thread context. */
    /* XXX Reference count. */
    frame->caller = tc->cur_frame;

    /* Set static frame. */
    frame->static_info = static_frame;
    
    /* Initial reference count is 1 by virtue of it being the currently
     * executing frame. */
    frame->ref_count = 1;
    
    /* Update interpreter and thread context, so next execution will use this
     * frame. */
    tc->cur_frame = frame;
    *(tc->interp_cur_op) = static_frame->bytecode;
    *(tc->interp_bytecode_start) = static_frame->bytecode;
    *(tc->interp_reg_base) = frame->work;
    *(tc->interp_cu) = static_frame->cu;
}
