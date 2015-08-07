#include "moar.h"
#include "internal.h"
#include "platform/mmap.h"


void MVM_jit_compiler_init(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_compiler_deinit(MVMThreadContext *tc, MVMJitCompiler *compiler);
MVMJitCode * MVM_jit_compiler_assemble(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *graph, MVMJitExprTree *tree);

#define COPY_ARRAY(a, n) memcpy(MVM_malloc(n * sizeof(a[0])), a, n * sizeof(a[0]))

static const MVMuint16 MAGIC_BYTECODE[] = { MVM_OP_sp_jit_enter, 0 };

void MVM_jit_compiler_init(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitGraph *jg) {
    MVMint32  num_globals = MVM_jit_num_globals();
    /* Create dasm state */
    dasm_init(cl, 1);
    cl->dasm_globals = MVM_malloc(num_globals * sizeof(void*));
    dasm_setupglobal(cl, cl->dasm_globals, num_globals);
    dasm_setup(cl, MVM_jit_actions());
    /* space for our dynamic labels */
    dasm_growpc(cl, jg->labels_num);
    /* next (internal) label to assign */
    cl->next_label = jg->labels_num;
}

void MVM_jit_compiler_deinit(MVMThreadContext *tc, MVMJitCompiler *cl) {
    MVM_free(cl->dasm_globals);
    dasm_free(cl);
}

MVMJitCode * MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *jg) {
    MVMJitCompiler cl;
    MVMJitCode *code;
    MVMJitNode *node = jg->first_node;

    MVM_jit_log(tc, "Starting compilation\n");
    /* initialation */
    MVM_jit_compiler_init(tc, &cl, jg);
    /* generate code */
    MVM_jit_emit_prologue(tc, &cl, jg);
    while (node) {
        switch(node->type) {
        case MVM_JIT_NODE_LABEL:
            MVM_jit_emit_label(tc, &cl, jg, &node->u.label);
            break;
        case MVM_JIT_NODE_PRIMITIVE:
            MVM_jit_emit_primitive(tc, &cl, jg, &node->u.prim);
            break;
        case MVM_JIT_NODE_BRANCH:
            MVM_jit_emit_branch(tc, &cl, jg, &node->u.branch);
            break;
        case MVM_JIT_NODE_CALL_C:
            MVM_jit_emit_call_c(tc, &cl, jg, &node->u.call);
            break;
        case MVM_JIT_NODE_GUARD:
            MVM_jit_emit_guard(tc, &cl, jg, &node->u.guard);
            break;
        case MVM_JIT_NODE_INVOKE:
            MVM_jit_emit_invoke(tc, &cl, jg, &node->u.invoke);
            break;
        case MVM_JIT_NODE_JUMPLIST:
            MVM_jit_emit_jumplist(tc, &cl, jg, &node->u.jumplist);
            break;
        case MVM_JIT_NODE_CONTROL:
            MVM_jit_emit_control(tc, &cl, jg, &node->u.control);
            break;
        case MVM_JIT_NODE_EXPR_TREE:
            MVM_jit_compile_expr_tree(tc, &cl, jg, node->u.tree);
            break;
        }
        node = node->next;
    }
    MVM_jit_emit_epilogue(tc, &cl, jg);

    /* Generate code */
    code = MVM_jit_compiler_assemble(tc, &cl, jg);

    /* Clear up the compiler */
    MVM_jit_compiler_deinit(tc, &cl);

    /* Logging for insight */
    if (tc->instance->jit_bytecode_dir) {
        MVM_jit_log_bytecode(tc, code);
    }
    if (tc->instance->jit_log_fh)
        fflush(tc->instance->jit_log_fh);
    return code;
}

MVMJitCode * MVM_jit_compiler_assemble(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitGraph *jg) {
    MVMJitCode * code;
    MVMint32 i;
    char * memory;
    size_t codesize;

   /* compile the function */
    dasm_link(cl, &codesize);
    memory = MVM_platform_alloc_pages(codesize, MVM_PAGE_READ|MVM_PAGE_WRITE);
    dasm_encode(cl, memory);

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
        MVMint32 offset = dasm_getpclabel(cl, i);
        if (offset < 0)
            MVM_jit_log(tc, "Got negative offset for dynamic label %d\n", i);
        code->labels[i] = memory + offset;
    }

    /* Copy the deopts, inlines, and handlers. Because these use the
     * label index rather than the direct pointer, no fixup is
     * necessary */
    code->num_bbs      = jg->bbs_num;
    code->bb_labels    = COPY_ARRAY(jg->bbs, jg->bbs_num);

    code->num_deopts   = jg->deopts_num;
    code->deopts       = code->num_deopts ? COPY_ARRAY(jg->deopts, jg->deopts_num) : NULL;
    code->num_handlers = jg->handlers_num;
    code->handlers     = code->num_handlers ? COPY_ARRAY(jg->handlers, jg->handlers_alloc) : NULL;
    code->num_inlines  = jg->inlines_num;
    code->inlines      = code->num_inlines ? COPY_ARRAY(jg->inlines, jg->inlines_alloc) : NULL;

    return code;
}

void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code) {
    MVM_platform_free_pages(code->func_ptr, code->size);
    MVM_free(code->bb_labels);
    MVM_free(code->deopts);
    MVM_free(code->handlers);
    MVM_free(code->inlines);
    MVM_free(code);
}


void MVM_jit_compile_tile(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprTree *tree, MVMint32 node) {
    MVMJitExprNodeInfo *info[8];
    MVMint32 nchild, first_child;
    info[0] = &tree->info[node];

}

void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg, MVMJitExprTree *tree) {
    /* NYI */
    MVM_oops(tc, "NYI");
}


/* Enter the JIT code segment. The label is a continuation point where control
 * is resumed after the frame is properly setup. */
void MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                        MVMJitCode *code) {
    void *label = tc->cur_frame->jit_entry_label;
    code->func_ptr(tc, cu, label);
}
