#include "moar.h"
#include "nodes.h"
#include "compiler.h"

/* Takes a hash of types and produces a MASTNodeTypes structure. */
#define grab_type(name) do { \
    MVMString *key = MVM_string_utf8_decode(tc, tc->instance->VMString, #name, strlen(#name)); \
    result->name   = MVM_repr_at_key_o(tc, types, key); \
} while (0)

static MASTNodeTypes * node_types_struct(MVMThreadContext *tc, MVMObject *types) {
    MASTNodeTypes *result = MVM_malloc(sizeof(MASTNodeTypes));
    MVMROOT(tc, types, {
        grab_type(CompUnit);
        grab_type(Frame);
        grab_type(Op);
        grab_type(ExtOp);
        grab_type(SVal);
        grab_type(IVal);
        grab_type(NVal);
        grab_type(Label);
        grab_type(Local);
        grab_type(Lexical);
        grab_type(Call);
        grab_type(Annotated);
        grab_type(HandlerScope);
    });
    return result;
}

/* Compiles MAST down to bytecode, then loads it as a compilation unit,
 * running deserialize and load frames as appropriate. */
void MVM_mast_to_cu(MVMThreadContext *tc, MVMObject *mast, MVMObject *types,
        MVMRegister *res) {
    MVMCompUnit *loaded;

    MVMROOT(tc, mast, {
        /* Get node types into struct. */
        MASTNodeTypes *mnt = node_types_struct(tc, types);

        /* Turn the MAST tree into bytecode. Switch to gen2 GC allocation to be
         * sure nothing moves, though we'd really rather not have compiler
         * temporaries live longer. */
        unsigned int size;
        char *bytecode;
        MVM_gc_allocate_gen2_default_set(tc);
        bytecode = MVM_mast_compile(tc, mast, mnt, &size);
        MVM_free(mnt);
        MVM_gc_allocate_gen2_default_clear(tc);

        /* Load it as a compilation unit; it is a kind of MVMObject, so cast
         * it to that. */
        loaded = MVM_cu_from_bytes(tc, (MVMuint8 *)bytecode, (MVMuint32)size);
        loaded->body.deallocate = MVM_DEALLOCATE_FREE;
    });

    /* Stash loaded comp unit in result register. */
    res->o = (MVMObject *)loaded;

    /* If there's a deserialization frame, need to run that. */
    if (loaded->body.deserialize_frame) {
        /* Set up special return to delegate to running the load frame,
         * if any. */
        tc->cur_frame->return_value        = NULL;
        tc->cur_frame->return_type         = MVM_RETURN_VOID;

        /* Invoke the deserialization frame and return to the runloop. */
        MVM_frame_invoke(tc, loaded->body.deserialize_frame, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS),
            NULL, NULL, NULL, -1);
    }
}

/* Compiles MAST down to bytecode, then writes it to disk. */
void MVM_mast_to_file(MVMThreadContext *tc, MVMObject *mast, MVMObject *types, MVMString *filename) {
    MVMROOT(tc, mast, {
        FILE *fh;
        char *c_filename;

        /* Get node types into struct. */
        MASTNodeTypes *mnt = node_types_struct(tc, types);

        /* Turn the MAST tree into bytecode. */
        unsigned int size;
        char *bytecode;
        MVM_gc_allocate_gen2_default_set(tc);
        bytecode = MVM_mast_compile(tc, mast, mnt, &size);
        MVM_free(mnt);
        MVM_gc_allocate_gen2_default_clear(tc);

        /* Write it out to a file. (Not using VM-level IO for this right now;
         * may want to do that, but really we just want to shove the bytes out
         * to disk, without having to go via string subsystem, etc. */
        c_filename = MVM_string_utf8_encode_C_string(tc, filename);
        if ((fh = fopen(c_filename, "wb+"))) {
            fwrite(bytecode, 1, size, fh);
            fclose(fh);
            MVM_free(c_filename);
        }
        else {
            /* we have to build waste by hand, otherwise MVMROOT gets mad */
            char *waste[2];
            waste[0] = c_filename;
            waste[1] = NULL;
            MVM_exception_throw_adhoc_free(tc, waste, "Unable to write bytecode to '%s'", c_filename);
        }
    });
}
