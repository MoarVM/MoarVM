#include "moarvm.h"

/* Dummy, 0-arg callsite. */
static MVMCallsite no_arg_callsite = { NULL, 0, 0 };

/* Handles loading of bytecode, including triggering the deserialize and load
 * special frames. Takes place in two steps, with a callback between them which
 * is triggered by the special_return mechanism. */
static void run_load(MVMThreadContext *tc, void *sr_data);
void MVM_load_bytecode(MVMThreadContext *tc, MVMString *filename) {
    MVMCompUnit *cu, *try_cu;

    /* See if we already loaded this. */
    try_cu = tc->instance->head_compunit;
    while (try_cu) {
        if (try_cu->filename) {
            if (MVM_string_equal(tc, try_cu->filename, filename)) {
                /* Already loaded, so we're done. */
                return;
            }
        }
        try_cu = try_cu->next_compunit;
    }

    /* Otherwise, load from disk. */
    cu = MVM_cu_map_from_file(tc, MVM_string_utf8_encode_C_string(tc, filename));
    cu->filename = filename;

    /* If there's a deserialization frame, need to run that. */
    if (cu->deserialize_frame) {
        /* Set up special return to delegate to running the load frame,
         * if any. */
        tc->cur_frame->return_value        = NULL;
        tc->cur_frame->return_type         = MVM_RETURN_VOID;
        tc->cur_frame->special_return      = run_load;
        tc->cur_frame->special_return_data = cu;

        /* Inovke the deserialization frame and return to the runloop. */
        MVM_frame_invoke(tc, cu->deserialize_frame, &no_arg_callsite,
            NULL, NULL, NULL);
    }
    else {
        /* No deserialize frame, so do load frame instead. */
        run_load(tc, cu);
    }
}

/* Callback after running deserialize code to run the load code. */
static void run_load(MVMThreadContext *tc, void *sr_data) {
    MVMCompUnit *cu = (MVMCompUnit *)sr_data;

    /* If there's a load frame, need to run that. If not, we're done. */
    if (cu->load_frame) {
        /* Make sure the call happens in void context. No special return
         * handler here; we want to go back to the place that used the
         * loadbytecode op in the first place. */
        tc->cur_frame->return_value = NULL;
        tc->cur_frame->return_type  = MVM_RETURN_VOID;

        /* Inovke the deserialization frame and return to the runloop. */
        MVM_frame_invoke(tc, cu->load_frame, &no_arg_callsite,
            NULL, NULL, NULL);
    }
}
