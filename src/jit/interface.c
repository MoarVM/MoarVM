#include "moar.h"
#include "internal.h"

static void assert_within_region(MVMThreadContext *tc, MVMJitCode *code, void *address) {
#if MVM_JIT_DEBUG
    MVMint32 ofs = (char*)address - (char*)code->func_ptr;
    if ((0 <= ofs) && (ofs < code->size))
        return;
    MVM_panic(1, "JIT: address out of range for code!\n"
              "(label %p, func_ptr %p, code size %lui, offset %li, frame_nr %i, seq nr %i)",
              address, code->func_ptr, code->size, ofs,
              tc->cur_frame->sequence_nr, code->seq_nr);
#endif
}


/* Enter the JIT code segment. The label is a continuation point where control
 * is resumed after the frame is properly setup. */
void MVM_jit_code_enter(MVMThreadContext *tc, MVMJitCode *code, MVMCompUnit *cu) {
    void *label = tc->cur_frame->jit_entry_label;

    assert_within_region(tc, code, label);

    code->func_ptr(tc, cu, label);
}

void * MVM_jit_code_get_current_position(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame) {
    if (tc->cur_frame == frame && tc->jit_return_address != NULL) {
        /* currently on C stack */
        void *return_address = *tc->jit_return_address;
        assert_within_region(tc, code, return_address);
        return return_address;
    } else {
        /* trampolined-out of this frame, so jit_entry_label is correct */
        return frame->jit_entry_label;
    }
}

MVMint32 MVM_jit_code_get_active_deopt_idx(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame) {
    MVMint32 i;
    void *current_position = MVM_jit_code_get_current_position(tc, code, frame);
    for (i = 0; i < code->num_deopts; i++) {
        if (code->labels[code->deopts[i].label] == current_position) {
            break;
        }
    }
    return i;
}

MVMint32 MVM_jit_code_get_active_handlers(MVMThreadContext *tc, MVMJitCode *code, void *current_position, MVMint32 i) {
    for (; i < code->num_handlers; i++) {
        void *start_label = code->labels[code->handlers[i].start_label];
        void *end_label   = code->labels[code->handlers[i].end_label];
        if (start_label <= current_position && current_position <= end_label) {
            break;
        }
    }
    return i;
}

MVMint32 MVM_jit_code_get_active_inlines(MVMThreadContext *tc, MVMJitCode *code, void *current_position, MVMint32 i) {
    for (;i < code->num_inlines; i++) {
        void *inline_start = code->labels[code->inlines[i].start_label];
        void *inline_end   = code->labels[code->inlines[i].end_label];
        if (inline_start <= current_position && current_position <= inline_end)
            break;
    }
    return i;
}
