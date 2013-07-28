/* NFA constants. */
#define MVM_NFA_EDGE_FATE              0
#define MVM_NFA_EDGE_EPSILON           1
#define MVM_NFA_EDGE_CODEPOINT         2
#define MVM_NFA_EDGE_CODEPOINT_NEG     3
#define MVM_NFA_EDGE_CHARCLASS         4
#define MVM_NFA_EDGE_CHARCLASS_NEG     5
#define MVM_NFA_EDGE_CHARLIST          6
#define MVM_NFA_EDGE_CHARLIST_NEG      7
#define MVM_NFA_EDGE_CODEPOINT_I       9
#define MVM_NFA_EDGE_CODEPOINT_I_NEG   10

/* State entry. */
typedef struct {
    MVMint64 act;
    MVMint64 to;
    union {
        MVMint64   i;
        MVMString *s;
        struct {
            MVMCodepoint32 uc;
            MVMCodepoint32 lc;
        } uclc;
    } arg;
} MVMNFAStateInfo;

/* Body of an NFA. */
typedef struct _MVMNFABody {
    MVMObject        *fates;
    MVMint64          num_states;
    MVMint64         *num_state_edges;
    MVMNFAStateInfo **states;
} MVMNFABody;

typedef struct _MVMNFA {
    MVMObject common;
    MVMNFABody body;
} MVMNFA;

/* Function for REPR setup. */
MVMREPROps * MVMNFA_initialize(MVMThreadContext *tc);

/* Other NFA related functions. */
MVMObject * MVM_nfa_from_statelist(MVMThreadContext *tc, MVMObject *states, MVMObject *nfa_type);
MVMObject * MVM_nfa_run_proto(MVMThreadContext *tc, MVMObject *nfa, MVMString *target, MVMint64 offset);
void MVM_nfa_run_alt(MVMThreadContext *tc, MVMObject *nfa, MVMString *target,
    MVMint64 offset, MVMObject *bstack, MVMObject *cstack, MVMObject *labels);
