#include "moar.h"
#include "dasm_proto.h"
#include "platform/mmap.h"
#include "emit.h"

#define COPY_ARRAY(a, n, t) memcpy(MVM_malloc(n * sizeof(t)), a, n * sizeof(t))

static const MVMuint16 MAGIC_BYTECODE[] = { MVM_OP_sp_jit_enter, 0 };

MVMJitCode * MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *jg) {
    dasm_State *state;
    char * memory;
    size_t codesize;
    /* Space for globals */
    MVMint32  num_globals = MVM_jit_num_globals();
    void ** dasm_globals = MVM_malloc(num_globals * sizeof(void*));
    MVMJitNode * node = jg->first_node;
    MVMJitCode * code;
    MVMint32 i;

    MVM_jit_log(tc, "Starting compilation\n");

    /* setup dasm */
    dasm_init(&state, 1);
    dasm_setupglobal(&state, dasm_globals, num_globals);
    dasm_setup(&state, MVM_jit_actions());
    dasm_growpc(&state, jg->labels_num);

    /* generate code */
    MVM_jit_emit_prologue(tc, jg,  &state);
    while (node) {
        switch(node->type) {
        case MVM_JIT_NODE_LABEL:
            MVM_jit_emit_label(tc, jg, &node->u.label, &state);
            break;
        case MVM_JIT_NODE_PRIMITIVE:
            MVM_jit_emit_primitive(tc, jg, &node->u.prim, &state);
            break;
        case MVM_JIT_NODE_BRANCH:
            MVM_jit_emit_branch(tc, jg, &node->u.branch, &state);
            break;
        case MVM_JIT_NODE_CALL_C:
            MVM_jit_emit_call_c(tc, jg, &node->u.call, &state);
            break;
        case MVM_JIT_NODE_GUARD:
            MVM_jit_emit_guard(tc, jg, &node->u.guard, &state);
            break;
        case MVM_JIT_NODE_INVOKE:
            MVM_jit_emit_invoke(tc, jg, &node->u.invoke, &state);
            break;
        case MVM_JIT_NODE_JUMPLIST:
            MVM_jit_emit_jumplist(tc, jg, &node->u.jumplist, &state);
            break;
        case MVM_JIT_NODE_CONTROL:
            MVM_jit_emit_control(tc, jg, &node->u.control, &state);
            break;
        }
        node = node->next;
    }
    MVM_jit_emit_epilogue(tc, jg, &state);

    /* compile the function */
    dasm_link(&state, &codesize);
    memory = MVM_platform_alloc_pages(codesize, MVM_PAGE_READ|MVM_PAGE_WRITE);
    dasm_encode(&state, memory);
    /* set memory readable + executable */
    MVM_platform_set_page_mode(memory, codesize, MVM_PAGE_READ|MVM_PAGE_EXEC);


    MVM_jit_log(tc, "Bytecode size: %"MVM_PRSz"\n", codesize);
    /* Create code segment */
    code = MVM_malloc(sizeof(MVMJitCode));
    code->func_ptr   = (MVMJitFunc)memory;
    code->size       = codesize;
    code->bytecode   = (MVMuint8*)MAGIC_BYTECODE;
    code->sf         = jg->sg->sf;

    /* Get the basic block labels */
    code->num_labels = jg->labels_num;
    code->labels = MVM_malloc(sizeof(void*) * code->num_labels);
    for (i = 0; i < code->num_labels; i++) {
        MVMint32 offset = dasm_getpclabel(&state, i);
        if (offset < 0)
            MVM_jit_log(tc, "Got negative offset for dynamic label %d\n", i);
        code->labels[i] = memory + offset;
    }

    /* Copy the deopts, inlines, and handlers. Because these use the label index
     * rather than the direct pointer, no fixup is necessary */
    code->num_bbs      = jg->bbs_num;
    code->bb_labels    = COPY_ARRAY(jg->bbs, jg->bbs_num, MVMint32);

    code->num_deopts   = jg->deopts_num;
    code->deopts       = code->num_deopts ? COPY_ARRAY(jg->deopts, jg->deopts_num, MVMJitDeopt) : NULL;
    code->num_handlers = jg->handlers_num;
    code->handlers     = code->num_handlers ? COPY_ARRAY(jg->handlers, jg->handlers_alloc, MVMJitHandler) : NULL;
    code->num_inlines  = jg->inlines_num;
    code->inlines      = code->num_inlines ? COPY_ARRAY(jg->inlines, jg->inlines_alloc, MVMJitInline) : NULL;

    /* clear up the assembler */
    dasm_free(&state);
    MVM_free(dasm_globals);

    if (tc->instance->jit_bytecode_dir) {
        MVM_jit_log_bytecode(tc, code);
    }
    if (tc->instance->jit_log_fh)
        fflush(tc->instance->jit_log_fh);
    return code;
}

void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code) {
    MVM_platform_free_pages(code->func_ptr, code->size);
    MVM_free(code);
}

#define X64_REGISTERS(_) \
    _(rax), \
    _(rcx), \
    _(rdx), \
    _(rbx), \
    _(rsp), \
    _(rbp), \
    _(rsi), \
    _(rdi), \
    _(r8), \
    _(r9), \
    _(r10), \
    _(r11), \
    _(r12), \
    _(r13), \
    _(r14), \
    _(r15)

typedef enum {
#define RENUM(x) x
X64_REGISTERS(RENUM)
#undef RENUM
} X64_REGISTER;

static const char * X64_REGISTER_NAMES[] = {
#define RNAME(x) #x
X64_REGISTERS(RNAME)
#undef RNAME
};


/* List of free registers, used as a 'register ring' */
static const X64_REGISTER FREE_REGISTERS[] = {
    rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11
};

#define NUM_REGISTERS (sizeof(FREE_REGISTERS)/sizeof(X64_REGISTER))

typedef struct {
    MVMint32 reg_used[NUM_REGISTERS]; /* last use of a register */
    MVMint32 *nodes_reg;      /* register number of last computed operand */
    MVMint32 *nodes_spill;    /* spill location of a given node, if spilled */
    MVMint32 last_register;   /* last register number allocated; used
                                 to implement the 'register ring' */
    MVMint32 last_spill;      /* last spill threshold, used to ensure
                               * that every value receives it's own
                               * node */
} CompilerRegisterState;

static void take_register(MVMThreadContext *tc, CompilerRegisterState *state, MVMint32 regnum) {
    MVMint32 node, spill;
    if (state->reg_used[regnum] >= 0) {
        /* register is used. spill it's value */
        node  = state->reg_used[regnum];
        spill = state->nodes_spill[node];

        if (spill < 0) {
            /* node was never spilled before, allocate a location  */
            spill = state->last_spill++;
        }
        MVM_jit_log(tc, "mov [rsp+0x%x], %s\n", spill*sizeof(MVMRegister),
                    X64_REGISTER_NAMES[FREE_REGISTERS[regnum]]);
        /* mark node as spilled */
        state->nodes_reg[node]     = -1;
        state->nodes_spill[node]      = spill;
    }
    /* mark register as free */
    state->reg_used[regnum] = -1;
}

static MVMint32 get_next_register(MVMThreadContext *tc, CompilerRegisterState *state,
                                  X64_REGISTER *regs, MVMint32 nregs) {
    MVMint32 i, j;
    for (i = 0; i < NUM_REGISTERS; i++) {
        MVMint32 regnum = (state->last_register++) % NUM_REGISTERS;
        for (j = 0; j < nregs; j++) {
            if (regs[j] == FREE_REGISTERS[regnum])
                break;
        }
        if (j < nregs)
            continue;
        /* register was not found in input. if necessary, steal it */
        take_register(tc, state, regnum);
        return regnum;
    }
    MVM_oops(tc, "Could not allocate a register\n");
}

static void emit_expr_op(MVMThreadContext *tc, MVMJitExprNode op,
                         X64_REGISTER *regs, MVMJitExprNode *args) {
    switch(op) {
    case MVM_JIT_LOAD:
        MVM_jit_log(tc, "mov %s, [%s]\n", X64_REGISTER_NAMES[regs[1]], X64_REGISTER_NAMES[regs[0]]);
        break;
    case MVM_JIT_STORE:
        MVM_jit_log(tc, "mov [%s], %s\n", X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[regs[1]]);
        break;
    case MVM_JIT_COPY:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[regs[1]], X64_REGISTER_NAMES[regs[0]]);
        break;
    case MVM_JIT_ADDR:
        MVM_jit_log(tc, "lea %s, [%s+0x%"PRIx64"]\n", X64_REGISTER_NAMES[regs[0]],
                    X64_REGISTER_NAMES[regs[1]], args[0]);
        break;
    case MVM_JIT_LOCAL:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[rbx]);
        break;
    default: {
        const MVMJitExprOpInfo *info = MVM_jit_expr_op_info(tc, op);
        MVM_jit_log(tc, "not yet sure how to compile %s\n", info->name);
    }
    }
}

static void compile_expr_op(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                            MVMJitExprTree *tree, MVMint32 node) {
    CompilerRegisterState *state = traverser->data;
    MVMJitExprNode op  = tree->nodes[node];
    const MVMJitExprOpInfo *info = MVM_jit_expr_op_info(tc, op);
    X64_REGISTER regs[8];
    MVMint32 i, j, first_child, nchild;
    if (traverser->visits[node] > 1) /* no revisits */
        return;
    first_child = node + 1;
    nchild      = (info->nchild < 0 ? tree->nodes[first_child++] : info->nchild);
    /* ensure child nodes have been computed into memory */
    for (i = 0; i < nchild; i++) {
        MVMint32 child  = tree->nodes[first_child+i];
        MVMint32 regnum = state->nodes_reg[child];
        if (MVM_jit_expr_op_info(tc, tree->nodes[child])->vtype == MVM_JIT_VOID)
            continue;
        if (regnum < 0) {
            /* child does not reside in a register */
            MVMint32 spill = state->nodes_spill[child];
            if (spill < 0) {
                MVM_oops(tc, "Child %d of %s is needed, but not register or memory", i, info->name);
            }
            regnum = get_next_register(tc, state, regs, i);
            /* emit load */
            MVM_jit_log(tc, "mov %s, [rsp+0x%x]\n", X64_REGISTER_NAMES[regs[i]], spill);
            /* store child as existing in the register */
            state->reg_used[regnum] = child;
            state->nodes_reg[child] = regnum;
        }
        regs[i] = FREE_REGISTERS[regnum];
    }
    if (info->vtype != MVM_JIT_VOID) {
        /* assign an output register */
        MVMint32 regnum = get_next_register(tc, state, regs, i);
        regs[i]         = FREE_REGISTERS[regnum];
        state->nodes_reg[node] = regnum;
        state->reg_used[regnum] = node;
    }
    emit_expr_op(tc, op, regs, tree->nodes + first_child + nchild);
}


void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitGraph *jg, MVMJitExprTree *tree) {
    /* VERY SIMPLE AND PROVISIONAL COMPILER FOR THE EXPRESSION TREE
     *
     * We use the tree nodes as value numbers, and compile everything
     * to registers. Spill to stack (much simpler that way, but wrong
     * with regard to GC). This algorithm is a proof of concept, but
     * the concept is to do online register allocation without any use
     * knowledge, which is not really a good concept at all. */
    MVMJitTreeTraverser compiler;
    CompilerRegisterState state;
    /* Initialize state */
    memset(state.reg_used, -1, sizeof(state.reg_used));
    state.nodes_reg = MVM_malloc(sizeof(X64_REGISTER)*tree->nodes_num);
    state.nodes_spill = MVM_malloc(sizeof(MVMint32)*tree->nodes_num);

    memset(state.nodes_reg, -1, sizeof(X64_REGISTER)*tree->nodes_num);
    memset(state.nodes_spill, -1, sizeof(MVMint32)*tree->nodes_num);

    /* initialize compiler */
    memset(&compiler, 0, sizeof(MVMJitTreeTraverser));
    compiler.data      = &state;
    compiler.postorder = &compile_expr_op;

    MVM_jit_expr_tree_traverse(tc, tree, &compiler);

    MVM_free(state.nodes_reg);
    MVM_free(state.nodes_spill);
}


/* Returns 1 if we should return from the frame, the function, 0 otherwise */
MVMint32 MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                            MVMJitCode *code) {
    /* The actual JIT code returns 0 if it went through to the exit */
    void *label = tc->cur_frame->jit_entry_label;
    MVMint32 ctrl = code->func_ptr(tc, cu, label);
    return ctrl ? 0 : 1;
}
