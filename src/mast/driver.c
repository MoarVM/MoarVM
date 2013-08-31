#include "moarvm.h"
#include "nodes_moarvm.h"
#include "mast/compiler.h"

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

/* Compiles MAST down to bytecode, then loads it as a compilation unit. */
MVMObject * MVM_mast_to_cu(MVMThreadContext *tc, MVMObject *mast, MVMObject *types) {
    MVMObject *loaded;
    
    MVMROOT(tc, mast, {
        /* Get node types into struct. */
        MASTNodeTypes *mnt = node_types_struct(tc, types);
        
        /* Turn the MAST tree into bytecode. */
        unsigned int size;
        char *bytecode = MVM_mast_compile(tc, mast, mnt, &size);
        free(mnt);
        
        /* Load it as a compilation unit; it is a kind of MVMObject, so cast
         * it to that. */
        loaded = (MVMObject *)MVM_cu_from_bytes(tc, (MVMuint8 *)bytecode, (MVMuint32)size);
    });
    
    return loaded;
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
