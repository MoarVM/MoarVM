#include "moar.h"

/* Dummy, 0-arg callsite. */
static MVMCallsite no_arg_callsite = { NULL, 0, 0, 0 };

/* Takes a filename and prepends any --libpath value we have, if it's not an
 * absolute path. */
static MVMString * figure_filename(MVMThreadContext *tc, MVMString *orig) {
    const char *lib_path = tc->instance->lib_path;
    if (lib_path) {
        /* We actually have a lib_path to consider. See if the filename is
         * absolute (XXX wants a platform abstraction, and doing better). */
        char *orig_cstr = MVM_string_utf8_encode_C_string(tc, orig);
        int  absolute   = orig_cstr[0] == '/' || orig_cstr[0] == '\\' ||
                          orig_cstr[1] == ':' && orig_cstr[2] == '\\';
        if (absolute) {
            /* Nothing more to do; we have an absolute path. */
            free(orig_cstr);
            return orig;
        }
        else {
            /* Concatenate libpath with filename. */
            MVMString *result;
            size_t lib_path_len = strlen(lib_path);
            size_t orig_len     = strlen(orig_cstr);
            int    need_sep     = lib_path[lib_path_len - 1] != '/' &&
                                  lib_path[lib_path_len - 1] != '\\';
            int    new_len      = lib_path_len + (need_sep ? 1 : 0) + orig_len;
            char * new_path     = malloc(new_len);
            memcpy(new_path, lib_path, lib_path_len);
            if (need_sep) {
                new_path[lib_path_len] = '/';
                memcpy(new_path + lib_path_len + 1, orig_cstr, orig_len);
            }
            else {
                memcpy(new_path + lib_path_len, orig_cstr, orig_len);
            }
            result = MVM_string_utf8_decode(tc, tc->instance->VMString, new_path, new_len);
            free(new_path);
            return result;
        }
    }
    else {
        /* No libpath, so just hand back the original name. */
        return orig;
    }
}

/* Handles loading of bytecode, including triggering the deserialize and load
 * special frames. Takes place in two steps, with a callback between them which
 * is triggered by the special_return mechanism. */
static void run_load(MVMThreadContext *tc, void *sr_data);
void MVM_load_bytecode(MVMThreadContext *tc, MVMString *filename) {
    MVMCompUnit *cu, *try_cu;
    MVMLoadedCompUnitName *loaded_name;

    /* Work out actual filename to use, taking --libpath into account. */
    filename = figure_filename(tc, filename);

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
