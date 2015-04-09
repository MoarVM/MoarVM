#include "moar.h"

/* Handles loading of bytecode, including triggering the deserialize and load
 * special frames. Takes place in two steps, with a callback between them which
 * is triggered by the special_return mechanism. */
static void run_load(MVMThreadContext *tc, void *sr_data);
static void mark_sr_data(MVMThreadContext *tc, MVMFrame *frame, MVMGCWorklist *worklist) {
    MVM_gc_worklist_add(tc, worklist, &frame->special_return_data);
}
void MVM_load_bytecode(MVMThreadContext *tc, MVMString *filename) {
    MVMCompUnit *cu;
    MVMLoadedCompUnitName *loaded_name;

    /* Work out actual filename to use, taking --libpath into account. */
    filename = MVM_file_in_libpath(tc, filename);

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
        char *c_filename = MVM_string_utf8_encode_C_string(tc, filename);
        cu = MVM_cu_map_from_file(tc, c_filename);
        MVM_free(c_filename);
        cu->body.filename = filename;

        /* If there's a deserialization frame, need to run that. */
        if (cu->body.deserialize_frame) {
            /* Set up special return to delegate to running the load frame,
             * if any. */
            tc->cur_frame->return_value             = NULL;
            tc->cur_frame->return_type              = MVM_RETURN_VOID;
            tc->cur_frame->special_return           = run_load;
            tc->cur_frame->special_return_data      = cu;
            tc->cur_frame->mark_special_return_data = mark_sr_data;

            /* Invoke the deserialization frame and return to the runloop. */
            MVM_frame_invoke(tc, cu->body.deserialize_frame, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS),
                NULL, NULL, NULL, -1);
        }
        else {
            /* No deserialize frame, so do load frame instead. */
            run_load(tc, cu);
        }
        loaded_name = MVM_calloc(1, sizeof(MVMLoadedCompUnitName));
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
        MVM_frame_invoke(tc, cu->body.load_frame, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS),
            NULL, NULL, NULL, -1);
    }
}
