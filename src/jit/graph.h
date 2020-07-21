/* The MVMJitGraph is - for now - really a linked list of instructions.
 * It's likely I'll add complexity when it's needed */
struct MVMJitGraph {
    MVMSpeshGraph *sg;
    MVMJitNode    *first_node;
    MVMJitNode    *last_node;

    /* Number of instruction+bb+graph labels, but excluding the expression labels */
    MVMint32       num_labels;
    /* Offset for instruction labels */
    MVMint32       obj_label_ofs;

    /* Sequence number for expr trees */
    MVMuint16      expr_seq_nr;

    /* resultant JIT code is supports 'invokish' etc? */
    MVMuint8       no_trampoline;

    /* All labeled things */
    MVM_VECTOR_DECL(void*, obj_labels);
    MVM_VECTOR_DECL(MVMJitDeopt, deopts);
    MVM_VECTOR_DECL(MVMJitHandler, handlers);
    MVM_VECTOR_DECL(MVMJitInline, inlines);
    MVM_VECTOR_DECL(MVMJitNode*, label_nodes);
};

struct MVMJitDeopt {
    MVMint32 idx;
    MVMint32 label;
};

struct MVMJitHandler {
    MVMint32 start_label;
    MVMint32 end_label;
    MVMint32 goto_label;
};

struct MVMJitInline {
    MVMint32 start_label;
    MVMint32 end_label;
};

/* A label (no more than a number) */
struct MVMJitLabel {
    MVMint32    name;
};

struct MVMJitPrimitive {
    MVMSpeshIns * ins;
};

struct MVMJitGuard {
    MVMSpeshIns * ins;
    MVMuint32 deopt_idx;
};


#define MVM_JIT_INFO_INVOKISH 1
#define MVM_JIT_INFO_THROWISH 2

typedef enum {
    MVM_JIT_CONTROL_BREAKPOINT,
} MVMJitControlType;

struct MVMJitControl {
    MVMSpeshIns       *ins;
    MVMJitControlType type;
};

/* Special branch target for the exit */
#define MVM_JIT_BRANCH_EXIT -1


/* What does a branch need? a label to go to, an instruction to read */
struct MVMJitBranch {
    MVMint32     dest;
    MVMSpeshIns *ins;
};

typedef enum {
    MVM_JIT_INTERP_TC,
    MVM_JIT_INTERP_CU,
    MVM_JIT_INTERP_FRAME,
    MVM_JIT_INTERP_PARAMS,
    MVM_JIT_INTERP_CALLER,
} MVMJitInterpVar;

typedef enum {
    MVM_JIT_INTERP_VAR,
    MVM_JIT_REG_VAL,
    MVM_JIT_REG_VAL_F,
    MVM_JIT_REG_ADDR,
    MVM_JIT_STR_IDX,
    MVM_JIT_LITERAL,
    MVM_JIT_LITERAL_F,
    MVM_JIT_LITERAL_64,
    MVM_JIT_LITERAL_PTR,
    MVM_JIT_REG_STABLE,
    MVM_JIT_REG_OBJBODY,
    /* Take from register relative to cur_op. Usually code is JIT compiled by
       spesh which already known the indexes of the registers an op uses.
       Compilation of native calls however happens ahead of time when the code
       that will call the ncinvoke op may not even exist yet. In that case
       we need to do the same as interp.c and address registers relative to
       cur_op. */
    MVM_JIT_REG_DYNIDX,
    MVM_JIT_DATA_LABEL,
    /* The MVM_JIT_ARG_* types are used when the offset into the WORK array is
       not known yet, i.e. for ahead of time compiled native calls. */
    MVM_JIT_ARG_I64,
    MVM_JIT_ARG_I64_RW,
    MVM_JIT_ARG_DOUBLE,
    /* Pointers are passed as objects with CPointer representation, i.e. the
       actual pointer is part of the object's data. The MVM_JIT_ARG_PTR type
       unboxes the CPointer object and passes on the contained pointer */
    MVM_JIT_ARG_PTR,
    MVM_JIT_ARG_VMARRAY,
    /* The MVM_JIT_PARAM_* types are usd when actual JIT compilation is
       happening as part of spesh, i.e. the offset of the args buffer in WORK
       is already known. */
    MVM_JIT_PARAM_I64,
    MVM_JIT_PARAM_I64_RW,
    MVM_JIT_PARAM_DOUBLE,
    MVM_JIT_PARAM_PTR,
    MVM_JIT_PARAM_VMARRAY,
    /* spesh slot value */
    MVM_JIT_SPESH_SLOT_VALUE,
    /* stack relative address */
    MVM_JIT_STACK_VALUE,
} MVMJitArgType;

struct MVMJitCallArg {
    MVMJitArgType type;
    union {
        MVMint64      lit_i64;
        MVMnum64      lit_n64;
        MVMJitInterpVar  ivar;
        MVMint16          reg;
        void             *ptr;
    } v;
};


typedef enum {
    MVM_JIT_RV_VOID,
    /* ptr and int are mostly the same, but they might not be on all
       platforms */
    MVM_JIT_RV_INT,
    MVM_JIT_RV_PTR,
    /* floats aren't */
    MVM_JIT_RV_NUM,
    /* dereference and store */
    MVM_JIT_RV_DEREF,
    /* store local at address */
    MVM_JIT_RV_ADDR,
    /* Store in register relative to cur_op. Usually code is JIT compiled by
       spesh which already known the indexes of the registers an op uses.
       Compilation of native calls however happens ahead of time when the code
       that will call the ncinvoke op may not even exist yet. In that case
       we need to do the same as interp.c and address registers relative to
       cur_op. */
    MVM_JIT_RV_DYNIDX,
    /* store pointer or vmnull */
    MVM_JIT_RV_DEREF_OR_VMNULL,
    /* store on stack with offset */
    MVM_JIT_RV_TO_STACK,
} MVMJitRVMode;


struct MVMJitCallC {
    void       *func_ptr;
    MVMJitCallArg  *args;
    MVMuint16   num_args;
    MVMJitRVMode rv_mode;
    MVMint16      rv_type;
    MVMint16      rv_idx;
};

struct MVMJitInvoke {
    MVMint16      callsite_idx;
    MVMint16      arg_count;
    MVMSpeshIns **arg_ins;
    MVMReturnType return_type;
    MVMint16      return_register;
    MVMuint32     code_register_or_name;
    MVMint16      spesh_cand_or_sf_slot;
    MVMint8       is_fast;
    MVMuint32     resolve_offset;           /* Only for spesh resolve */
    MVMint32      reentry_label;
};

struct MVMJitJumpList {
    MVMint64 num_labels;
    MVMint16 reg;
    /* labels of the goto's / jump instructions themselves */
    MVMint32 *in_labels;
    /* labels the goto's jump to */
    MVMint32 *out_labels;
};

struct MVMJitData {
    MVMint32 label;
    void     *data;
    size_t    size;
};

struct MVMJitStackSlot {
    MVMint16 slot;
};

/* Node types */
typedef enum {
    MVM_JIT_NODE_PRIMITIVE,
    MVM_JIT_NODE_CALL_C,
    MVM_JIT_NODE_BRANCH,
    MVM_JIT_NODE_LABEL,
    MVM_JIT_NODE_GUARD,
    MVM_JIT_NODE_INVOKE,
    MVM_JIT_NODE_JUMPLIST,
    MVM_JIT_NODE_CONTROL,
    MVM_JIT_NODE_DATA,
    MVM_JIT_NODE_EXPR_TREE,
    MVM_JIT_NODE_DEOPT_CHECK,
} MVMJitNodeType;

struct MVMJitNode {
    MVMJitNode   * next; /* linked list */
    MVMJitNodeType type; /* tag */
    union {
        MVMJitPrimitive prim;
        MVMJitCallC     call;
        MVMJitBranch    branch;
        MVMJitLabel     label;
        MVMJitGuard     guard;
        MVMJitInvoke    invoke;
        MVMJitJumpList  jumplist;
        MVMJitControl   control;
        MVMJitData      data;
        MVMJitExprTree *tree;
        MVMJitStackSlot stack;
    } u;
};

MVMJitGraph* MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg);
void MVM_jit_graph_destroy(MVMThreadContext *tc, MVMJitGraph *graph);
