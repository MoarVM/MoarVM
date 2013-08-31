#include "moarvm.h"
#include "nodes_moarvm.h"
#include "mast/compiler.h"

/* Dummy, 0-arg callsite. */
static MVMCallsite no_arg_callsite = { NULL, 0, 0 };

/* Takes a hash of types and produces a MASTNodeTypes structure. */
#define grab_type(name) do { \
    MVMString *key = MVM_string_utf8_decode(tc, tc->instance->VMString, #name, strlen(#name)); \
    result->name   = MVM_repr_at_key_boxed(tc, types, key); \
} while (0);
MASTNodeTypes * node_types_struct(MVMThreadContext *tc, MVMObject *types) {
    MASTNodeTypes *result = malloc(sizeof(MASTNodeTypes));
    MVMROOT(tc, types, {
        grab_type(CompUnit);
        grab_type(Frame);
        grab_type(Op);
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
        
        /* Turn the MAST tree into bytecode. */
        unsigned int size;
        char *bytecode = MVM_mast_compile(tc, mast, mnt, &size);
        free(mnt);
        
        /* Load it as a compilation unit; it is a kind of MVMObject, so cast
         * it to that. */
        loaded = MVM_cu_from_bytes(tc, (MVMuint8 *)bytecode, (MVMuint32)size);
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
        MVM_frame_invoke(tc, loaded->body.deserialize_frame, &no_arg_callsite,
            NULL, NULL, NULL);
    }
}

/* Compiles MAST down to bytecode, then loads it as a compilation unit. */
void MVM_mast_to_file(MVMThreadContext *tc, MVMObject *mast, MVMObject *types, MVMString *filename) {
    MVMROOT(tc, mast, {
        /* Get node types into struct. */
        MASTNodeTypes *mnt = node_types_struct(tc, types);
        
        /* Turn the MAST tree into bytecode. */
        unsigned int size;
        char *bytecode = MVM_mast_compile(tc, mast, mnt, &size);
        free(mnt);
        
        MVM_exception_throw_adhoc(tc, "MAST to file NYI");
    });
}
