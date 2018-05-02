/* NFA constants that are part of the NFA REPR API. */
#define MVM_NFA_EDGE_FATE              0
#define MVM_NFA_EDGE_EPSILON           1
#define MVM_NFA_EDGE_CODEPOINT         2
#define MVM_NFA_EDGE_CODEPOINT_NEG     3
#define MVM_NFA_EDGE_CHARCLASS         4
#define MVM_NFA_EDGE_CHARCLASS_NEG     5
#define MVM_NFA_EDGE_CHARLIST          6
#define MVM_NFA_EDGE_CHARLIST_NEG      7
#define MVM_NFA_EDGE_SUBRULE           8
#define MVM_NFA_EDGE_CODEPOINT_I       9
#define MVM_NFA_EDGE_CODEPOINT_I_NEG   10
#define MVM_NFA_EDGE_GENERIC_VAR       11
#define MVM_NFA_EDGE_CHARRANGE         12
#define MVM_NFA_EDGE_CHARRANGE_NEG     13
#define MVM_NFA_EDGE_CODEPOINT_LL      14
#define MVM_NFA_EDGE_CODEPOINT_I_LL    15
#define MVM_NFA_EDGE_CODEPOINT_M       16
#define MVM_NFA_EDGE_CODEPOINT_M_NEG   17
#define MVM_NFA_EDGE_CODEPOINT_M_LL    18
#define MVM_NFA_EDGE_CODEPOINT_IM      19
#define MVM_NFA_EDGE_CODEPOINT_IM_NEG  20
#define MVM_NFA_EDGE_CODEPOINT_IM_LL   21
#define MVM_NFA_EDGE_CHARRANGE_M       22
#define MVM_NFA_EDGE_CHARRANGE_M_NEG   23

/* A synthetic edge we use to let us more optimally handle nodes with a fanout
 * of many codepoints. We sort the edges of type CODEPOINT and CODEPOINT_LL to
 * the start of the state out edges list, and insert this node before, which
 * indicates how many CODEPOINT and CODEPOINT_LL edges there are. We can then
 * binary search them for the current codepoint, and skip over the rest. This
 * is especially useful in huge categories, such as infix, prefix, etc. */
#define MVM_NFA_EDGE_SYNTH_CP_COUNT    64

/* State entry. */
struct MVMNFAStateInfo {
    MVMint64 act;
    MVMint64 to;
    union {
        MVMGrapheme32  g;
        MVMint64       i;
        MVMString     *s;
        struct {
            MVMGrapheme32 uc;
            MVMGrapheme32 lc;
        } uclc;
    } arg;
};

/* Body of an NFA. */
struct MVMNFABody {
    MVMObject        *fates;
    MVMint64          num_states;
    MVMint64         *num_state_edges;
    MVMNFAStateInfo **states;
};

struct MVMNFA {
    MVMObject common;
    MVMNFABody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMNFA_initialize(MVMThreadContext *tc);

/* Other NFA related functions. */
MVMObject * MVM_nfa_from_statelist(MVMThreadContext *tc, MVMObject *states, MVMObject *nfa_type);
MVMObject * MVM_nfa_run_proto(MVMThreadContext *tc, MVMObject *nfa, MVMString *target, MVMint64 offset);
void MVM_nfa_run_alt(MVMThreadContext *tc, MVMObject *nfa, MVMString *target,
    MVMint64 offset, MVMObject *bstack, MVMObject *cstack, MVMObject *labels);
