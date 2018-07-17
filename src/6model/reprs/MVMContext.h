/* Representation for a context in the VM. Holds an MVMFrame and perhaps also
 * a path to follow to reach the represented content. The path following is
 * done lazily so as to cope with inlines but also to cope with deopt taking
 * place. Terminal operations that actually resolve data will resolve the
 * path and then do the operation. */
struct MVMContextBody {
    /* The base frame that a reference was originally taken to via the
     * ctx op. We never inline this op, so know our starting point will
     * always have a caller and an outer. */
    MVMFrame *context;

    /* An array of traversal operations to perform relative to context. */
    MVMuint8 *traversals;

    /* The number of traversal operations. */
    MVMuint32 num_traversals;
};
struct MVMContext {
    MVMObject common;
    MVMContextBody body;
};

/* The various context traversals that we might perform. */
#define MVM_CTX_TRAV_OUTER                  1
#define MVM_CTX_TRAV_CALLER                 2
#define MVM_CTX_TRAV_OUTER_SKIP_THUNKS      3
#define MVM_CTX_TRAV_CALLER_SKIP_THUNKS     4

/* Function for REPR setup. */
const MVMREPROps * MVMContext_initialize(MVMThreadContext *tc);
