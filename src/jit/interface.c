#include "moar.h"
#include "internal.h"


/* Enter the JIT code segment. The label is a continuation point where control
 * is resumed after the frame is properly setup. */
void MVM_jit_code_enter(MVMThreadContext *tc, MVMJitCode *code, MVMCompUnit *cu) {
    void *label = tc->cur_frame->jit_entry_label;

    MVM_jit_code_assert_within_region(tc, code, label);

    code->func_ptr(tc, cu, label);
}

void * MVM_jit_code_get_current_position(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame) {
    if (tc->cur_frame == frame && tc->jit_return_address != NULL) {
        /* currently on C stack */
        void *return_address = *tc->jit_return_address;
        MVM_jit_code_assert_within_region(tc, code, return_address);
        return return_address;
    } else {
        /* trampolined-out of this frame, so jit_entry_label is correct */
        return frame->jit_entry_label;
    }
}

void MVM_jit_code_set_current_position(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame, void *position) {
    MVM_jit_code_assert_within_region(tc, code, position);
    if (tc->cur_frame == frame && tc->jit_return_address != NULL) {
        /* this overwrites the address on the stack that MVM_frame_invoke_code will ret to! */
        *tc->jit_return_address = position;
    } else {
        frame->jit_entry_label = position;
    }
}

void MVM_jit_code_trampoline(MVMThreadContext *tc) {
    if (tc->jit_return_address != NULL) {
        MVMJitCode *code  = tc->cur_frame->spesh_cand->body.jitcode;
        void *reentry_label = *tc->jit_return_address;
        MVM_jit_code_assert_within_region(tc, code, reentry_label);
        /* Store our current position */
        tc->cur_frame->jit_entry_label = *tc->jit_return_address;
        /* Tell currently-active JIT code that we're leaving this frame */
        MVM_jit_code_assert_within_region(tc, code, code->exit_label);
        *tc->jit_return_address = code->exit_label;
        /* And tell further frame handlers that as far as they are concerned,
           we're not on the stack anymore */
        tc->jit_return_address = NULL;
    }
}


MVMuint32 MVM_jit_code_get_active_deopt_idx(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame) {
    MVMuint32 i;
    void *current_position = MVM_jit_code_get_current_position(tc, code, frame);
    for (i = 0; i < code->num_deopts; i++) {
        if (code->labels[code->deopts[i].label] == current_position) {
            break;
        }
    }
    return i;
}

MVMuint32 MVM_jit_code_get_active_handlers(MVMThreadContext *tc, MVMJitCode *code, void *current_position, MVMuint32 i) {
    for (; i < code->num_handlers; i++) {
        void *start_label = code->labels[code->handlers[i].start_label];
        void *end_label   = code->labels[code->handlers[i].end_label];
        if (start_label <= current_position && current_position <= end_label) {
            break;
        }
    }
    return i;
}

MVMint32 MVM_jit_code_get_active_inlines(MVMThreadContext *tc, MVMJitCode *code, void *current_position, MVMuint32 i) {
    for (;i < code->num_inlines; i++) {
        void *inline_start = code->labels[code->inlines[i].start_label];
        void *inline_end   = code->labels[code->inlines[i].end_label];
        if (inline_start <= current_position && current_position <= inline_end)
            break;
    }
    return i;
}
