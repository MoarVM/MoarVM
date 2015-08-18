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
    /* Store graph we're compiling */
    cl->graph      = jg;
    /* next (internal) label to assign */
    cl->next_label = jg->labels_num;
    cl->label_max  = jg->labels_num + 8;
    /* space for dynamic labels */
    dasm_growpc(cl, cl->label_max);
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
            MVM_jit_emit_label(tc, &cl, jg, node->u.label.name);
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
    code->func_ptr   = (void (*)(MVMThreadContext*,MVMCompUnit*,void*)) memory;
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

/* Compile time labelling facility, as opposed to graph labels; these
 * don't need to be stored for access later */
static MVMint32 alloc_internal_label(MVMThreadContext *tc, MVMJitCompiler *cl, MVMint32 num) {
    MVMint32 next_label = cl->next_label;
    if (num + next_label >= cl->label_max) {
        /* Double the compile-time allocated labels */
        cl->label_max = cl->graph->labels_num + 2 * (cl->label_max - cl->graph->labels_num);
        dasm_growpc(cl, cl->label_max);
    }
    /* 'Allocate' num labels */
    cl->next_label += num;
    return next_label;
}


static void prepare_tile(MVMThreadContext *tc, MVMJitTreeTraverser *traverser, MVMJitExprTree *tree, MVMint32 node) {
    MVMJitCompiler *cl = traverser->data;
    switch (tree->nodes[node]) {
    case MVM_JIT_WHEN:
        {
            MVMint32 cond = tree->nodes[node+1];
            tree->info[node].internal_label = alloc_internal_label(tc, cl, 1);
            if (tree->nodes[cond] == MVM_JIT_ALL || tree->nodes[cond] == MVM_JIT_ANY) {
                /* Assign the label downward for short-circuit evaluation */
                tree->info[cond].internal_label = tree->info[node].internal_label;
            }
        }
        break;
    case MVM_JIT_IF:
    case MVM_JIT_EITHER:
        {
            MVMint32 cond = tree->nodes[node+1];
            tree->info[node].internal_label = alloc_internal_label(tc, cl, 2);
            if (tree->nodes[cond] == MVM_JIT_ALL || tree->nodes[cond] == MVM_JIT_ANY) {
                /* Assign the label downward for short-circuit evaluation */
                tree->info[cond].internal_label = tree->info[node].internal_label;
            }
        }
        break;
    default:
        break;
    }
}

static void compile_labels(MVMThreadContext *tc, MVMJitTreeTraverser *traverser, MVMJitExprTree *tree, MVMint32 node, MVMint32 i) {
    MVMJitCompiler *cl = traverser->data;
    switch (tree->nodes[node]) {
    case MVM_JIT_WHEN:
            if (i == 0) {
                /* after condition */
                MVMint32 cond = tree->nodes[node+1];
                if (tree->nodes[cond] == MVM_JIT_ALL || tree->nodes[cond] == MVM_JIT_ANY) {
                    /* short-circuit evaluation of ALL and ANY has
                       already taken care of the jump beyond the
                       block */
                } else {
                    MVM_jit_emit_conditional_branch(tc, cl, tree->nodes[cond],
                                                    tree->info[node].internal_label);
                }
                break;
            } else {
                /* i == 1, so we're ready to emit our internal label now */
                MVM_jit_emit_label(tc, cl, cl->graph, tree->info[node].internal_label);
            }
            break;
    case MVM_JIT_IF:
    case MVM_JIT_EITHER:
        /* TODO take care of (delimited) register invalidation! */
        if (i == 0) {
            MVMint32 cond = tree->nodes[node+1];
            if (tree->nodes[cond] == MVM_JIT_ALL || tree->nodes[cond] == MVM_JIT_ANY) {
                /* Nothing to do */
            } else {
                MVM_jit_emit_conditional_branch(tc, cl, tree->nodes[cond],
                                                tree->info[node].internal_label);
            }
        } else if (i == 1) {
            MVMJitBranch branch;
            branch.ins   = NULL;
            branch.dest = tree->info[node].internal_label + 1;
            MVM_jit_emit_branch(tc, cl, cl->graph, &branch);
            MVM_jit_emit_label(tc, cl, cl->graph, tree->info[node].internal_label);
        } else {
            MVM_jit_emit_label(tc, cl, cl->graph, tree->info[node].internal_label + 1);
        }
        break;
    case MVM_JIT_ALL:
        {
            MVMint32 cond = tree->nodes[node+2+i];
            break;
        }
    default:
        break;
    }

}

#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64

static MVMint8 x64_gpr_args[] = {
    X64_ARG_GPR(MVM_JIT_REGNAME)
};


static MVMint8 x64_sse_args[] = {
    X64_ARG_SSE(MVM_JIT_REGNAME)
};

#if MVM_JIT_PLATFORM == MVM_JIT_PLATFORM_WIN32
static void compile_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler,
    MVMJitExprTree *tree, MVMint32 node) {
    MVMint32 i, nchild = tree->nodes[node+1], first_child = node + 2;
    for (i = 0; i < MIN(nchild, 4); i++) {
        MVMint32 carg   = tree->nodes[first_child+i];
        MVMint32 val    = tree->nodes[carg+1];
        MVMint32 argtyp = tree->nodes[carg+2];
        if (argtype == MVM_JIT_NUM) {
            MVMint8 reg = x64_sse_args[i];
            MVM_jit_register_take(tc, cl, reg, MVM_JIT_X64_SSE, val);
            MVM_jit_emit_load(tc, cl, reg, MVM_JIT_X64_SSE);
        } else {
            MVMint8 reg = x64_gpr_args[i];
            MVM_jit_register_take(tc, cl, reg, MVM_JIT_X64_GPR, val);
        }
    }
    for (. i < nchild; i++) {
    }
}
#else

static void compile_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                            MVMint32 node) {
    MVMint32 i, nchild = tree->nodes[node+1], first_child = node+2;
}
#endif

#else
static void compile_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                            MVMint32 node) {
    MVM_oops(tc, "compile_arglist NYI for this architecture");
}

#endif





static void compile_tile(MVMThreadContext *tc, MVMJitTreeTraverser *traverser, MVMJitExprTree *tree, MVMint32 node) {
    MVMJitCompiler *cl = traverser->data;
    MVMJitExprNodeInfo *info = &tree->info[node];
    const MVMJitTile *tile   = info->tile;
    MVMJitExprValue *values[8];
    MVMint32 first_child = node + 1;
    MVMint32 nchild = info->op_info->nchild < 0 ? tree->nodes[first_child++] : info->op_info->nchild;
    MVMJitExprNode *args = tree->nodes + first_child + nchild;
    MVMint32 i;
    switch (tree->nodes[node]) {
    case MVM_JIT_ARGLIST:
        compile_arglist(tc, cl, tree, node);
        break;
    default:
        {
            if (tile->rule == NULL)
                return;
            values[0] = &info->value;
            MVM_jit_tile_get_values(tc, tree, node, tile->path, values+1);

            for (i = 0; i < tile->num_values; i++) {
                /* TODO - ensure all values that are typed registers, are placed
                 * into registers! */
            }

            if (tile->vtype == MVM_JIT_REG) {
                /* allocate a register */
                if (values[1]->type == MVM_JIT_REG && values[1]->last_use == node) {
                    values[0]->u.reg.num = values[1]->u.reg.num;
                    values[0]->u.reg.cls = values[1]->u.reg.cls;
                    /* Free register so it can be reused */
                    MVM_jit_register_release(tc, cl, values[0]->u.reg.cls, values[0]->u.reg.num);
                } else {
                    values[0]->u.reg.num = MVM_jit_register_alloc(tc, cl, MVM_JIT_X64_GPR);
                    values[0]->u.reg.cls = MVM_JIT_X64_GPR;
                }
                MVM_jit_register_use(tc, cl, values[0]->u.reg.cls, values[0]->u.reg.num, node);
            }
            values[0]->type = tile->vtype;
            info->tile->rule(tc, cl, tree, node, values, args);
            /* clear up registers afterwards */
            for (i = 0; i < tile->num_values + 1; i++) {
                if (values[i]->type == MVM_JIT_REG) {
                    /* Releasing a register means it may be spilled */
                    MVM_jit_register_release(tc, cl, values[i]->u.reg.cls,  values[i]->u.reg.num);
                }
            }
        }
    }

}

void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;

    traverser.preorder  = &prepare_tile;
    traverser.inorder   = &compile_labels;
    traverser.postorder = &compile_tile;

    /* First stage, tile the tree */
    MVM_jit_tile_expr_tree(tc, tree);

    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
}


/* Enter the JIT code segment. The label is a continuation point where control
 * is resumed after the frame is properly setup. */
void MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                        MVMJitCode *code) {
    void *label = tc->cur_frame->jit_entry_label;
    code->func_ptr(tc, cu, label);
}
