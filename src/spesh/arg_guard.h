/* Specializations are selected using argument guards. These are arranged in
 * a tree, which is walked by a small interpreter. In the future, it may also
 * be compiled into machine code. */
struct MVMSpeshArgGuard {
    /* The nodes making up the guard. */
    MVMSpeshArgGuardNode *nodes;

    /* How many nodes we have. */
    MVMuint32 num_nodes;

    /* How many nodes are actually used. */
    MVMuint32 used_nodes;
};

/* Operations we may perform when evaluating a guard. */
typedef enum {
    /* Check if the callsite matches. */
    MVM_SPESH_GUARD_OP_CALLSITE,

    /* Load an arg from the args buffer into the test register. Always takes
     * "yes" branch. */
    MVM_SPESH_GUARD_OP_LOAD_ARG,

    /* Tests if the test register value matches the specified STable and is
     * a concrete value. */
    MVM_SPESH_GUARD_OP_STABLE_CONC,

    /* Tests if the test register value matches the specified STable and is
     * a type object. */
    MVM_SPESH_GUARD_OP_STABLE_TYPE,

    /* Dereferences the value at the specified offset into the current contents
     * of the test register and puts that value into the test register. Takes
     * the no branch if it is NULL and the yes branch otherwise. (Used for
     * a decont of a container value.)  */
    MVM_SPESH_GUARD_OP_DEREF_VALUE,

    /* Dereferences the 64-bit integer value offset into the current contents
     * of the test register. Takes the no branch if it's zero and the yes
     * branch otherwise. Does *not* update the test register. (Used for a
     * rw-ness test of a container.) */
    MVM_SPESH_GUARD_OP_DEREF_RW,

    /* Selects a specialization, if this node is reached. Ignores yes/no;
     * terminates execution of the guard check. */
    MVM_SPESH_GUARD_OP_RESULT
} MVMSpeshArgGuardOp;

/* A node in the guard tree. */
struct MVMSpeshArgGuardNode {
    /* The operation. */
    MVMSpeshArgGuardOp op;

    /* Where to go on match of the guard or no match. These are indexes into
     * the guard tree node array. A "no" value of zero indicates that there
     * are no more options, and the match should be termianted without a
     * result. */
    MVMuint16 yes;
    MVMuint16 no;

    /* Data for the operation; union discriminated by op. */
    union {
        /* Used by CALLSITE */
        MVMCallsite *cs;
        /* Used by LOAD_ARG */
        MVMuint16 arg_index;
        /* Used by STABLE_CONC, STABLE_TYPE */
        MVMSTable *st;
        /* Used by DEREF_VALUE and DEREF_RW. */
        MVMuint32 offset;
        /* Used by RESULT. */
        MVMuint32 result;
    };
};

void MVM_spesh_arg_guard_regenerate(MVMThreadContext *tc, MVMSpeshArgGuard **guard_ptr,
        MVMSpeshCandidate **candidates, MVMuint32 num_spesh_candidates); 
MVMint32 MVM_spesh_arg_guard_run_types(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
    MVMCallsite *cs, MVMSpeshStatsType *types);
MVMint32 MVM_spesh_arg_guard_run(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
    MVMCallsite *cs, MVMRegister *args, MVMint32 *certain);
MVMint32 MVM_spesh_arg_guard_run_callinfo(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
    MVMSpeshCallInfo *info);
void MVM_spesh_arg_guard_gc_mark(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
    MVMGCWorklist *worklist);
void MVM_spesh_arg_guard_gc_describe(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                                     MVMSpeshArgGuard *ag);
void MVM_spesh_arg_guard_destroy(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMuint32 safe);
void MVM_spesh_arg_guard_discard(MVMThreadContext *tc, MVMStaticFrame *sf);
