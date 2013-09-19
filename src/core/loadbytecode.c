#include "moarvm.h"

/* Dummy, 0-arg callsite. */
static MVMCallsite no_arg_callsite = { NULL, 0, 0, 0 };

/* Handles loading of bytecode, including triggering the deserialize and load
 * special frames. Takes place in two steps, with a callback between them which
 * is triggered by the special_return mechanism. */
static void run_load(MVMThreadContext *tc, void *sr_data);
void MVM_load_bytecode(MVMThreadContext *tc, MVMString *filename) {
    MVMCompUnit *cu, *try_cu;
    MVMLoadedCompUnitName *loaded_name;

    /* See if we already loaded this. */
    uv_mutex_lock(&tc->instance->mutex_loaded_compunits);
    MVM_string_flatten(tc, filename);
    MVM_HASH_GET(tc, tc->instance->loaded_compunits, filename, loaded_name);
    if (loaded_name) {
        /* already loaded */
        uv_mutex_unlock(&tc->instance->mutex_loaded_compunits);
        return;
    }

    /* Otherwise, load from disk. */
    MVMROOT(tc, filename, {
        cu = MVM_cu_map_from_file(tc, MVM_string_utf8_encode_C_string(tc, filename));
        cu->body.filename = filename;

        /* If there's a deserialization frame, need to run that. */
        if (cu->body.deserialize_frame) {
            /* Set up special return to delegate to running the load frame,
             * if any. */
            tc->cur_frame->return_value        = NULL;
            tc->cur_frame->return_type         = MVM_RETURN_VOID;
            tc->cur_frame->special_return      = run_load;
            tc->cur_frame->special_return_data = cu;

            /* Invoke the deserialization frame and return to the runloop. */
            MVM_frame_invoke(tc, cu->body.deserialize_frame, &no_arg_callsite,
                NULL, NULL, NULL);
        }
        else {
            /* No deserialize frame, so do load frame instead. */
            run_load(tc, cu);
        }
        loaded_name = calloc(1, sizeof(MVMLoadedCompUnitName));
        loaded_name->filename = filename;
        MVM_HASH_BIND(tc, tc->instance->loaded_compunits, filename, loaded_name);
    });

    uv_mutex_unlock(&tc->instance->mutex_loaded_compunits);
}

/* Callback after running deserialize code to run the load code. */
static void run_load(MVMThreadContext *tc, void *sr_data) {
    MVMCompUnit *cu = (MVMCompUnit *)sr_data;

    /* If there's a load frame, need to run that. If not, we're done. */
    if (cu->body.load_frame) {
        /* Make sure the call happens in void context. No special return
         * handler here; we want to go back to the place that used the
         * loadbytecode op in the first place. */
        tc->cur_frame->return_value = NULL;
        tc->cur_frame->return_type  = MVM_RETURN_VOID;

        /* Invoke the load frame and return to the runloop. */
        MVM_frame_invoke(tc, cu->body.load_frame, &no_arg_callsite,
            NULL, NULL, NULL);
    }
}
