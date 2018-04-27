#include "moar.h"
#include "internal.h"

/* Walk stack to find the current return address in a code frame */
void * stack_find_return_address_in_frame_posix(void *base, size_t size, void *top);
void * stack_find_return_address_in_frame_win64(void *base, size_t size, void *top);


MVM_STATIC_INLINE MVMint32 address_within_region(MVMThreadContext *tc, MVMJitCode *code, void *address) {
    MVMint32 ofs = (char*)address - (char*)code->func_ptr;
    return (0 <= ofs) && (ofs < code->size);
}


/* Enter the JIT code segment. The label is a continuation point where control
 * is resumed after the frame is properly setup. */
void MVM_jit_code_enter(MVMThreadContext *tc, MVMJitCode *code, MVMCompUnit *cu) {
    void *label = tc->cur_frame->jit_entry_label;
    if (!address_within_region(tc, code, label)) {
        MVM_panic(1, "JIT entry label out of range for code!\n"
                  "(label %p, func_ptr %p, code size %lui, offset %li, frame_nr %i, seq nr %i)",
                  label, code->func_ptr, code->size,
                  (char*) label - (char *) code->func_ptr
                  ,
                  tc->cur_frame->sequence_nr, code->seq_nr);
    }
    code->func_ptr(tc, cu, label);
}

static void * get_current_position(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame) {
    if (tc->cur_frame == frame) {
        /* currently on C stack */
        void *return_address = stack_find_return_address_in_frame_posix(code->func_ptr, code->size, tc->interp_cur_op);
        if (address_within_region(tc, code, return_address)) {
            return return_address;
        } else {
            /* this can only happen if we need to look up the current position
             * (e.g. for throwing an exception) while changing the current
             * frame, e.g. during special unwinding, in which case the
             * jit_entry_label must be correct */
            return frame->jit_entry_label;
        }
    } else {
        /* trampolined-out of this frame, so jit_entry_label is correct */
        return frame->jit_entry_label;
    }
}

MVMint32 MVM_jit_code_get_active_deopt_idx(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame) {
    MVMint32 i;
    void *current_position = get_current_position(tc, code, frame);
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
    void *current_position = get_current_position(tc, code, frame);
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
    void *current_position = get_current_position(tc, code, frame);
    for (i = 0; i < code->num_inlines; i++) {
        void *inline_start = code->labels[code->inlines[i].start_label];
        void *inline_end   = code->labels[code->inlines[i].end_label];
        if (inline_start <= current_position && current_position <= inline_end) {
            inlines_out[j++] = i;
        }
    }
    return j;
}
