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


/* NB - incorrect for win64 */
static const X64_REGISTER FREE_REGISTERS[] = {
    rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11
};
#define NUM_REGISTERS (sizeof(FREE_REGISTERS)/sizeof(X64_REGISTER))

typedef struct {
    MVMint32 used_registers[NUM_REGISTERS];
    X64_REGISTER *nodes_reg;  /* register of last computed operand */
    MVMint32 *nodes_spill;
} CompilerRegisterState;


static X64_REGISTER get_free_reg(MVMThreadContext *tc, CompilerRegisterState *state, MVMint32 node,
                                 MVMint32 num_input_registers, X64_REGISTER *input_registers) {
    MVMint32 i, j, prev;
    for (i = 0; i < NUM_REGISTERS; i++) {
        if (state->used_registers[i] < 0) {
            state->used_registers[i] = node;
            return FREE_REGISTERS[i];
        }
    }
    /* no free register found. spill it */
    for (i = 0; i < NUM_REGISTERS; i++) {
        for (j = 0; j < num_input_registers; j++) {
            if (input_registers[j] == i) {
                /* this register is used as input */
                break;
            }
        }
        if (j == num_input_registers) {
            /* register is not used by any input. spill it's value; nb
               this is a dumb spilling algorithm */
            break;
        }
    }
    if (i == NUM_REGISTERS) {
        MVM_oops(tc, "Want to allocate more registers than we really have");
    }
    /* This spill algorithm is wrong, by the way */
    MVM_jit_log(tc, "mov [rsp+0x%"PRIx32"], %s", i*8, X64_REGISTER_NAMES[i]);
    /* spill previous owner of this register */
    prev = state->used_registers[i];
    state->nodes_spill[prev] = i;
    state->nodes_reg[prev] = -1;
    /* assign it to the new owner */
    state->used_registers[i] = node;
    return FREE_REGISTERS[i];
}

static void emit_expr_op(MVMThreadContext *tc, MVMJitExprNode op, X64_REGISTER out,
                         X64_REGISTER *in, MVMJitExprNode *args) {

    switch(op) {
    case MVM_JIT_LOAD:
        MVM_jit_log(tc, "mov %s, [%s]\n", X64_REGISTER_NAMES[out], X64_REGISTER_NAMES[in[0]]);
        break;
    case MVM_JIT_STORE:
        MVM_jit_log(tc, "mov [%s], %s\n", X64_REGISTER_NAMES[in[0]], X64_REGISTER_NAMES[in[1]]);
        break;
    case MVM_JIT_COPY:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[out], X64_REGISTER_NAMES[in[0]]);
        break;
    case MVM_JIT_ADDR:
        MVM_jit_log(tc, "lea %s, [%s+0x%"PRIx64"]\n", X64_REGISTER_NAMES[out], X64_REGISTER_NAMES[in[0]], args[0]);
        break;
    case MVM_JIT_LOCAL:
        MVM_jit_log(tc, "mov %s, %s\n", X64_REGISTER_NAMES[out], X64_REGISTER_NAMES[rbx]);
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
    X64_REGISTER input_regs[8];
    if (traverser->visits[node] == 1) {
        /* first visit. compute the node */
        MVMint32 first_child = node+1;
        MVMint32 nchild      = (info->nchild < 0 ? tree->nodes[first_child++] : info->nchild);
        MVMJitExprNode *args = tree->nodes + first_child + nchild;
        MVMint32 i;
        for (i = 0; i < nchild; i++) {
            MVMint32 child = tree->nodes[first_child+i];
            if (state->nodes_reg[child] < 0 || state->nodes_reg[child] >= NUM_REGISTERS) {
                MVM_oops(tc, "JIT: child %d of op %s was not computed into a register",
                         i, info->name);
            }
            fprintf(stderr, "input register %d is %d\n", i, state->nodes_reg[child]);
            input_regs[i] = state->nodes_reg[child];
        }
        if (info->vtype == MVM_JIT_VOID) {
            emit_expr_op(tc, op, -1, input_regs, args);
        } else {
            /* Get output register to write our value in */
            X64_REGISTER out = get_free_reg(tc, state, node, nchild, input_regs);
            fprintf(stderr, "Got register nr %d\n", out);
            emit_expr_op(tc, op, out, input_regs, args);
            state->nodes_reg[node] = out;
        }
    } else {
        if (state->nodes_reg[node] < 0) {
            /* Spilled. emit a load */
            MVM_oop(tc, "/* not sure about loading just yet */\n");
        }
    }
}


void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitGraph *jg, MVMJitExprTree *tree) {
    /* VERY SIMPLE AND PROVISIONAL COMPILER FOR THE EXPRESSION TREE

     * We use the tree nodes as value numbers, and compile everything
     * to registers. We spill to stack (much simpler that way).
     */
    MVMJitTreeTraverser compiler;
    CompilerRegisterState state;
    /* Initialize state */
    memset(state.used_registers, -1, sizeof(CompilerRegisterState));
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
