#include "moar.h"
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

/* NB - valid only for POSIX call convention */
static const MVMint32 CALL_REGISTERS[] = {
    /* rdi, rsi, rdx, rcx, r8, r9 */
    4, 3, 2, 1, 5, 6
};

#define NUM_REGISTERS (sizeof(FREE_REGISTERS)/sizeof(X64_REGISTER))
#define NUM_CALL_REGS (sizeof(FREE_REGISTERS)/sizeof(X64_REGISTER))

typedef struct {
    MVMint32 reg_used[NUM_REGISTERS]; /* last use of a register */
    MVMint32 *nodes_reg;      /* register number of node */
    MVMint32 *nodes_spill;    /* spill location of a given node, if spilled */
    MVMint32 last_register;   /* last register number allocated; used
                                 to implement the 'register ring' */
    MVMint32 last_spill;      /* last spill threshold, used to ensure
                               * that every value receives it's own
                               * node */
    MVMint32 cond_depth;      /* last used local label depth by
                                 conditional statement */
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
        state->nodes_reg[node]        = -1;
        state->nodes_spill[node]      = spill;
    }
    /* mark register as free */
    state->reg_used[regnum] = -1;
}


static void emit_full_spill(MVMThreadContext *tc, CompilerRegisterState *state) {
    /* Equivalent to simply taking all registers */
    MVMint32 i;
    for (i = 0; i < NUM_REGISTERS; i++) {
        take_register(tc, state, i);
    }
}

/* invalidate registers */
static void invalidate_registers(MVMThreadContext *tc, CompilerRegisterState *state) {
    MVMint32 i;
    for (i = 0; i < NUM_REGISTERS; i++) {
        MVMint32 node = state->reg_used[i];
        if (node >= 0) {
            state->nodes_reg[node] = -1;
            state->reg_used[i]     = -1;
        }
    }
}

static void load_node_to(MVMThreadContext *tc, CompilerRegisterState *state, MVMint32 node, MVMint32 regnum) {
    MVMint32 cur_reg = state->nodes_reg[node];
    if (state->reg_used[regnum] == node)
        return;
    if (cur_reg >= 0) {
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[FREE_REGISTERS[regnum]],
                    X64_REGISTER_NAMES[FREE_REGISTERS[cur_reg]]);
        state->reg_used[cur_reg] = -1; /* is now free */
    } else if (state->nodes_spill[node] >= 0) {
        MVM_jit_log(tc, "mov %s, [rsp+0x%x]\n", X64_REGISTER_NAMES[FREE_REGISTERS[regnum]],
                    state->nodes_spill[node]*sizeof(MVMRegister));
    } else {
        MVM_oops(tc, "Requested load of node %d but not in register or memory\n");
    }
    state->nodes_reg[node]  = regnum;
    state->reg_used[regnum] = node;
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



static void emit_expr_op(MVMThreadContext *tc, CompilerRegisterState *state, MVMJitExprNode op,
                         X64_REGISTER *regs, MVMJitExprNode *args) {
    switch(op) {
    case MVM_JIT_LOAD:
        MVM_jit_log(tc, "mov %s, [%s]\n", X64_REGISTER_NAMES[regs[1]], X64_REGISTER_NAMES[regs[0]]);
        break;
    case MVM_JIT_STORE:
        MVM_jit_log(tc, "mov [%s], %s\n", X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[regs[1]]);
        break;
    case MVM_JIT_CONST:
        MVM_jit_log(tc, "mov %s, 0x%"PRIx64"\n", X64_REGISTER_NAMES[regs[0]], args[0]);
        break;
    case MVM_JIT_COPY:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[regs[1]], X64_REGISTER_NAMES[regs[0]]);
        break;
    case MVM_JIT_ADDR:
        MVM_jit_log(tc, "lea %s, [%s+0x%"PRIx64"]\n", X64_REGISTER_NAMES[regs[1]],
                    X64_REGISTER_NAMES[regs[0]], args[0]);
        break;
    case MVM_JIT_IDX:
        MVM_jit_log(tc, "lea %s, [%s+%s*%d]\n", X64_REGISTER_NAMES[regs[2]],
                    X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[regs[1]], args[0]);
        break;
    case MVM_JIT_ADD:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[regs[2]], X64_REGISTER_NAMES[regs[0]]);
        MVM_jit_log(tc, "add %s, %s\n", X64_REGISTER_NAMES[regs[2]], X64_REGISTER_NAMES[regs[1]]);
        break;
    case MVM_JIT_AND:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[regs[2]], X64_REGISTER_NAMES[regs[0]]);
        MVM_jit_log(tc, "and %s, %s\n", X64_REGISTER_NAMES[regs[2]], X64_REGISTER_NAMES[regs[1]]);
        break;
    case MVM_JIT_NZ:
        take_register(tc, state, 0);
        MVM_jit_log(tc, "test %s, %s\n", X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[regs[0]]);
        MVM_jit_log(tc, "setnz al\n");
        MVM_jit_log(tc, "movzx %s, al\n", X64_REGISTER_NAMES[regs[1]]);
        break;
    case MVM_JIT_ZR:
        take_register(tc, state, 0);
        MVM_jit_log(tc, "test %s, %s\n", X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[regs[0]]);
        MVM_jit_log(tc, "setnz al\n");
        MVM_jit_log(tc, "movzx %s, al\n", X64_REGISTER_NAMES[regs[1]]);
        break;
    case MVM_JIT_LOCAL:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[rbx]);
        break;
    case MVM_JIT_FRAME:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[r12]);
        break;
    case MVM_JIT_CU:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[r13]);
        break;
    case MVM_JIT_TC:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[regs[0]], X64_REGISTER_NAMES[r14]);
        break;
    case MVM_JIT_CALL:
        MVM_jit_log(tc, "call %s\n", X64_REGISTER_NAMES[regs[0]]);
        break;
    default: {
        const MVMJitExprOpInfo *info = MVM_jit_expr_op_info(tc, op);
        MVM_jit_log(tc, "not yet sure how to compile %s\n", info->name);
    }
    }
}

static void load_op_regs(MVMThreadContext *tc, CompilerRegisterState *state,
                         MVMJitExprTree *tree, MVMint32 node, X64_REGISTER *regs) {
    MVMJitExprNode op            = tree->nodes[node];
    const MVMJitExprOpInfo *info = MVM_jit_expr_op_info(tc, op);
    MVMint32 i, first_child, nchild;
    first_child = node + 1;
    nchild      = (info->nchild < 0 ? tree->nodes[first_child++] : info->nchild);
    /* ensure child nodes have been computed into memory */
    for (i = 0; i < nchild; i++) {
        MVMint32 child  = tree->nodes[first_child+i];
        MVMint32 regnum = state->nodes_reg[child];
        if (MVM_jit_expr_op_info(tc, tree->nodes[child])->vtype == MVM_JIT_VOID)
            continue;
        if (regnum < 0) {
            regnum = get_next_register(tc, state, regs, i);
            load_node_to(tc, state, child, regnum);
        }
        regs[i] = FREE_REGISTERS[regnum];
    }
    if (info->vtype != MVM_JIT_VOID) {
        /* assign an output register */
        MVMint32 regnum = get_next_register(tc, state, regs, i);
        regs[i]         = FREE_REGISTERS[regnum];
        state->nodes_reg[node]  = regnum;
        state->reg_used[regnum] = node;
    }
}

static void load_call_regs(MVMThreadContext *tc, CompilerRegisterState *state,
                           MVMJitExprTree *tree, MVMint32 node, X64_REGISTER *regs) {
    MVMint32 arglist = tree->nodes[node+2];
    /* whatever, this doesn't really work anyway :-( */
    MVMint32 nargs   = MIN(tree->nodes[arglist+1], sizeof(CALL_REGISTERS)/sizeof(CALL_REGISTERS[0]));
    MVMint32 i, func;
    for (i = 0; i < nargs; i++) {
        MVMint32 carg   = tree->nodes[arglist+2+i];
        MVMint32 argval = tree->nodes[carg+1];
        MVMint32 argtyp = tree->nodes[carg+2];
        MVMint32     regnum  = CALL_REGISTERS[i];
        X64_REGISTER reg     = FREE_REGISTERS[regnum];
        /* whatever, ignore argtyp */
        load_node_to(tc, state, argval, regnum);
        regs[i] = reg;
    }
    /* load function register */
    func = get_next_register(tc, state, regs, nargs);
    load_node_to(tc, state, tree->nodes[node+1], func);
    regs[0] = FREE_REGISTERS[func];
}

static void prepare_expr_op(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                            MVMJitExprTree *tree, MVMint32 node) {
    /* Spill before call or if or when */
    CompilerRegisterState *state = traverser->data;
    MVMJitExprNode op = tree->nodes[node];
    if (traverser->visits[node] > 1)
        return;
    /* Conditional blocks should spill before they are run */
    switch (op) {
    case MVM_JIT_WHEN:
        /* require a label for the statement end */
        state->cond_depth += 1;
        break;
    case MVM_JIT_IF:
        /* require two labels, one for the alternative block, and one for the statement end */
        state->cond_depth += 2;
        break;
    case MVM_JIT_ALL:
    case MVM_JIT_ANY:
        /* require one label for short-circuiting */
        state->cond_depth += 1;
        /* emit spill because we can't be sure which block will execute */
        emit_full_spill(tc, state);
        break;
    default:
        break;
    }
}

static void compile_expr_labels(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                                MVMJitExprTree *tree, MVMint32 node, MVMint32 i) {
    CompilerRegisterState *state = traverser->data;
    MVMJitExprNode op = tree->nodes[node];
    if (traverser->visits[node] > 1)
        return;
    switch (op) {
    case MVM_JIT_IF:
        /* 'ternary operator' or 'expression style' if */
        if (i == 0) {
            MVMint32 condition = tree->nodes[node+1];
            MVMint32 regnum    = state->nodes_reg[condition];
            const char * regname;
            if (regnum < 0) {
                /* this seems to be a common pattern */
                regnum = get_next_register(tc, state, NULL, 0);
                load_node_to(tc, state, condition, regnum);
            }
            regname = X64_REGISTER_NAMES[FREE_REGISTERS[regnum]];
            emit_full_spill(tc, state);
            MVM_jit_log(tc, "test %s, %s\n", regname, regname);
            /* condition statement; branch to second option (label 1) */
            MVM_jit_log(tc, "jz >%d\n", state->cond_depth - 1);
        } else if (i == 1) {
            /* move result value into place (i.e. rax) */
            MVMint32 child = tree->nodes[node+2];
            if (tree->info[child].value.size > 0)
                load_node_to(tc, state, child, 0);
            /* just after first option, branch to end */
            MVM_jit_log(tc, "jmp >%d\n", state->cond_depth);
            MVM_jit_log(tc, "%d:\n" , state->cond_depth - 1);
            /* registers used in the 'then' block are not usable in the 'else' block */
            invalidate_registers(tc, state);
        } else {
            /* move result value into place */
            MVMint32 child = tree->nodes[node+3];
            if (tree->info[child].value.size > 0)
                load_node_to(tc, state, child, 0);
            /* emit end label */
            MVM_jit_log(tc, "%d:\n", state->cond_depth);
            invalidate_registers(tc, state);
        }
        break;
    case MVM_JIT_WHEN:
        if (i == 0) {
            MVM_jit_log(tc, "jnz >%d\n", state->cond_depth);
        } else {
            MVM_jit_log(tc, "%d:\n", state->cond_depth);
            invalidate_registers(tc, state);
        }
        break;
    case MVM_JIT_ALL:
        {
            MVMint32 result = state->nodes_reg[node];
            MVMint32 value  = state->nodes_reg[node+i+2];
            const char *regname;
            if (result < 0) {
                result = get_next_register(tc, state, NULL, 0);
                if (i > 0) {
                    load_node_to(tc, state, node, result);
                } else {
                    /* assign register to node */
                    state->nodes_reg[node]  = result;
                    state->reg_used[result] = node;
                }
            }
            if (value < 0) {
                X64_REGISTER regs[] = { FREE_REGISTERS[result] };
                value = get_next_register(tc, state, regs, 1);
                load_node_to(tc, state, tree->nodes[node + i + 2], result);
            }
            regname = X64_REGISTER_NAMES[FREE_REGISTERS[result]];
            if (i > 0) {
                MVM_jit_log(tc, "and %s, %s\n", regname, X64_REGISTER_NAMES[FREE_REGISTERS[value]]);
            } else {
                MVM_jit_log(tc, "mov %s, %s\n", regname, X64_REGISTER_NAMES[FREE_REGISTERS[value]]);
            }
            MVM_jit_log(tc, "test %s, %s\n", regname, regname);
            MVM_jit_log(tc, "jz >%d\n", state->cond_depth);
            break;
        }
    default:
        break;
    }

}

static void compile_expr_op(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                            MVMJitExprTree *tree, MVMint32 node) {
    CompilerRegisterState *state = traverser->data;
    MVMJitExprNode op  = tree->nodes[node];
    const MVMJitExprOpInfo *info = MVM_jit_expr_op_info(tc, op);
    X64_REGISTER regs[8];
    MVMJitExprNode *args = tree->nodes + node + (info->nchild < 0 ? tree->nodes[node+1] + 1 : info->nchild) + 1;
    if (traverser->visits[node] > 1) /* no revisits */
        return;
    switch(op) {
    case MVM_JIT_CALL:
        /* spill before we go */
        emit_full_spill(tc, state);
        load_call_regs(tc, state, tree, node, regs);
        emit_expr_op(tc, state, op, regs, args);
        /* all loaded registers are now invalid */
        invalidate_registers(tc, state);
        if (args[0] != MVM_JIT_RV_VOID) {
            state->nodes_reg[node]  = 0;
            state->reg_used[0]      = node;
        }
        break;
    case MVM_JIT_WHEN:
        state->cond_depth     -= 1;
        break;
    case MVM_JIT_IF:
        /* result value lives in rax (as assured in compile_expr_labels) */
        {
            MVMint32 left = tree->nodes[node+2], right = tree->nodes[node+3];;
            if (tree->info[left].value.size > 0 && tree->info[right].value.size > 0) {
                state->reg_used[0]     = node;
                state->nodes_reg[node] = 0;
            } else if (tree->info[left].value.size > 0 || tree->info[right].value.size > 0) {
                MVM_oops(tc, "One child of IF yields a value, the other does not\n");
            }
            state->cond_depth     -= 2;
        }
        break;
    case MVM_JIT_DO:
        {
            /* take value from last node */
            MVMint32 nchild     = tree->nodes[node+1];
            MVMint32 last_child = tree->nodes[node+1+nchild];
            MVMint32 regnum     = get_next_register(tc, state, regs, 0);
            if (MVM_jit_expr_op_info(tc, tree->nodes[last_child])->vtype == MVM_JIT_VOID)
                break;
            if (state->nodes_reg[last_child] >= 0) {
                MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[FREE_REGISTERS[regnum]],
                            X64_REGISTER_NAMES[FREE_REGISTERS[state->nodes_reg[last_child]]]);
            } else if (state->nodes_spill[last_child] >= 0) {
                MVM_jit_log(tc, "mov %s, [rsp+0x%x]\n", X64_REGISTER_NAMES[FREE_REGISTERS[regnum]],
                            state->nodes_spill[last_child]*sizeof(MVMRegister));
            } else {
                MVM_oops(tc, "Can't load last node in DO\n");
            }
            state->reg_used[regnum] = node;
            state->nodes_reg[node]  = regnum;
            break;
        }
    case MVM_JIT_ALL:
        {
            MVMint32 result = state->nodes_reg[node];
            if (result < 0)
                MVM_oops(tc, "ALL result node is empty, wat\n");

            /* short-circuit label */
            MVM_jit_log(tc, "%d:\n", state->cond_depth);
            state->cond_depth -= 1;
            /* we don't know how we got here, but we spilled
               everything before entering AND. hence all we computed
               inbetween is an intermediary, and should be
               invalidated */
            invalidate_registers(tc, state);
            state->reg_used[result] = node;
            state->nodes_reg[node]  = result;
            break;
        }
    case MVM_JIT_CARG:
    case MVM_JIT_ARGLIST:
        break;
    default:
        load_op_regs(tc, state, tree, node, regs);
        emit_expr_op(tc, state, op, regs, args);
        break;
    }

}

void MVM_jit_pseudo_compile_expr_tree(MVMThreadContext *tc,  MVMJitGraph *jg, MVMJitExprTree *tree) {
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
    state.nodes_reg     = MVM_malloc(sizeof(MVMint32)*tree->nodes_num);
    state.nodes_spill   = MVM_malloc(sizeof(MVMint32)*tree->nodes_num);
    state.last_register = 0;
    state.last_spill    = 0;
    state.cond_depth    = 0;
    memset(state.nodes_reg, -1, sizeof(MVMint32)*tree->nodes_num);
    memset(state.nodes_spill, -1, sizeof(MVMint32)*tree->nodes_num);

    /* initialize compiler */
    memset(&compiler, 0, sizeof(MVMJitTreeTraverser));
    compiler.data      = &state;
    compiler.preorder  = &prepare_expr_op;
    compiler.inorder   = &compile_expr_labels;
    compiler.postorder = &compile_expr_op;

    MVM_jit_expr_tree_traverse(tc, tree, &compiler);

    MVM_free(state.nodes_reg);
    MVM_free(state.nodes_spill);
}
