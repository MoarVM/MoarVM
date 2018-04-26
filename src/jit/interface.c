#include "moar.h"
#include "internal.h"

void * stack_find_return_address(void *base, void *end, MVMint64 depth);

/* Enter the JIT code segment. The label is a continuation point where control
 * is resumed after the frame is properly setup. */
void MVM_jit_code_enter(MVMThreadContext *tc, MVMCompUnit *cu,
                        MVMJitCode *code) {
    void *label = tc->cur_frame->jit_entry_label;
    MVMint32 ofs = (char*)label - (char*)code->func_ptr;
    if (ofs < 0 || ofs >= code->size)
        MVM_oops(tc, "JIT entry label out of range for code!\n"
                 "(label %p, func_ptr %p, code size %lui, offset %li, frame_nr %i, seq nr %i)",
                 label, code->func_ptr, code->size, ((char*)label) - ((char*)code->func_ptr),
                 tc->cur_frame->sequence_nr, code->seq_nr);
    code->func_ptr(tc, cu, label);
}

MVMint32 MVM_jit_code_get_active_deopt_idx(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame) {
    MVMint32 i;
    void *current_position = frame->jit_entry_label;
    for (i = 0; i < code->num_deopts; i++) {
        if (code->labels[code->deopts[i].label] == current_position) {
            break;
        }
    }
    return i;
}

MVMint32 MVM_jit_code_get_active_handlers(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame, MVMint32 *handlers_out) {
    MVMint32 i;
    MVMint32 j = 0;
    void *current_position = frame->jit_entry_label;
    for (i = 0; i < code->num_handlers; i++) {
        void *start_label = code->labels[code->handlers[i].start_label];
        void *end_label   = code->labels[code->handlers[i].end_label];
        if (start_label <= current_position && current_position <= end_label) {
            handlers_out[j++] = i;
        }
    }
    return j;
}

MVMint32 MVM_jit_code_get_active_inlines(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame, MVMint32 *inlines_out) {
    MVMint32 i;
    MVMint32 j = 0;
    void *current_position = frame->jit_entry_label;
    for (i = 0; i < code->num_inlines; i++) {
        void *inline_start = code->labels[code->inlines[i].start_label];
        void *inline_end   = code->labels[code->inlines[j].end_label];
        if (inline_start <= current_position && current_position <= inline_end) {
            inlines_out[j++] = i;
        }
    }
    return j;
}
