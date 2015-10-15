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
    /* Offset in temporary array in which we can spill */
    cl->spill_offset = (jg->sg->num_locals + jg->sg->sf->body.cu->body.max_callsite_size) * MVM_JIT_REG_SZ;
    cl->max_spill    = 2*MVM_JIT_PTR_SZ;

}

void MVM_jit_compiler_deinit(MVMThreadContext *tc, MVMJitCompiler *cl) {
    dasm_free(cl);
    MVM_free(cl->dasm_globals);
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
    if (dasm_link(cl, &codesize) != 0)
        MVM_oops(tc, "dynasm could not link :-(");
    memory = MVM_platform_alloc_pages(codesize, MVM_PAGE_READ|MVM_PAGE_WRITE);
    if (dasm_encode(cl, memory) != 0)
        MVM_oops(tc, "dynasm could not encode :-(");

    /* set memory readable + executable */
    MVM_platform_set_page_mode(memory, codesize, MVM_PAGE_READ|MVM_PAGE_EXEC);

    MVM_jit_log(tc, "Bytecode size: %"MVM_PRSz"\n", codesize);

    /* Create code segment */
    code = MVM_malloc(sizeof(MVMJitCode));
    code->func_ptr   = (void (*)(MVMThreadContext*,MVMCompUnit*,void*)) memory;
    code->size       = codesize;
    code->bytecode   = (MVMuint8*)MAGIC_BYTECODE;
    code->sf         = jg->sg->sf;
    code->spill_size = cl->max_spill;

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

    code->seq_nr = MVM_incr(&tc->instance->jit_seq_nr);

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


#define NYI(x) MVM_oops(tc, #x " NYI")


static void alloc_value(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprValue *value) {
    if (value->type == MVM_JIT_NUM) {
        MVMint8 reg = MVM_jit_register_alloc(tc, cl, MVM_JIT_REGCLS_NUM);
        MVM_jit_register_assign(tc, cl, value, MVM_JIT_REGCLS_NUM, reg);
    } else {
        MVMint8 reg = MVM_jit_register_alloc(tc, cl, MVM_JIT_REGCLS_GPR);
        MVM_jit_register_assign(tc, cl, value, MVM_JIT_REGCLS_GPR, reg);
    }
}

static void use_value(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprValue *value) {
    MVM_jit_register_use(tc, cl, value->reg_cls, value->reg_num);
}

static void release_value(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprValue *value) {
    MVM_jit_register_release(tc, cl, value->reg_cls, value->reg_num);
}

static void load_value(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprValue *value) {
    MVMint8 reg_num = MVM_jit_register_alloc(tc, cl, MVM_JIT_REGCLS_GPR);
    MVM_jit_register_load(tc, cl, value->spill_location, MVM_JIT_REGCLS_GPR, reg_num, value->size);
}

static void ensure_values(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprValue **values, MVMint32 num_values) {
    MVMint32 i;
    /* Ensure all register values are live */
    for (i = 0; i < num_values; i++) {
        MVMJitExprValue *value = values[i];
        if (value->type == MVM_JIT_REG) {
            if (value->state == MVM_JIT_VALUE_SPILLED)
                load_value(tc, cl, value);
            else if (value->state == MVM_JIT_VALUE_EMPTY ||
                     value->state == MVM_JIT_VALUE_DEAD) {
                MVM_oops(tc, "Required Value Is Not Live");
            }
            /* Mark value as in-use */
            use_value(tc, cl, value);
        }
    }

}

static void prepare_tile(MVMThreadContext *tc, MVMJitTreeTraverser *traverser, MVMJitExprTree *tree, MVMint32 node) {
    MVMJitCompiler *cl = traverser->data;
    switch (tree->nodes[node]) {
    case MVM_JIT_WHEN:
        {
            /* Before we enter this node, mark us as entering a conditional */
            MVMint32 cond = tree->nodes[node+1];
            MVM_jit_spill_before_conditional(tc, cl, tree, node);
            if (tree->nodes[cond] == MVM_JIT_ANY) {
                /* WHEN ANY requires two labels. One label for
                   skipping our block (tne normal WHEN label) and one
                   for entering it */
                tree->info[node].internal_label = alloc_internal_label(tc, cl, 2);
                tree->info[cond].internal_label = tree->info[node].internal_label;
            } else if (tree->nodes[cond] == MVM_JIT_ALL) {
                /* WHEN ALL requires just one label, because the
                 * short-circuit label of ALL is the same as the
                 * normal WHEN skip label */
                tree->info[node].internal_label = alloc_internal_label(tc, cl, 1);
                tree->info[cond].internal_label = tree->info[node].internal_label;
            } else {
                tree->info[node].internal_label = alloc_internal_label(tc, cl, 1);
            }
        }
        break;
    case MVM_JIT_IF:
    case MVM_JIT_EITHER:
        {
            MVMJitExprValue *if_val = &tree->info[node].value;
            MVMint32 cond = tree->nodes[node+1];
            MVM_jit_spill_before_conditional(tc, cl, tree, node);
            if (tree->nodes[cond] == MVM_JIT_ALL)  {
                /* IF ALL short-circuits into the right block, which is simply labeled with the normal IF label */
                tree->info[node].internal_label = alloc_internal_label(tc, cl, 2);
                tree->info[cond].internal_label = tree->info[node].internal_label;
            } else if(tree->nodes[cond] == MVM_JIT_ANY) {
                /* IF ANY short-circuits into the left block, meaning I need an additional label */
                tree->info[node].internal_label = alloc_internal_label(tc, cl, 3);
                tree->info[cond].internal_label = tree->info[node].internal_label;
            } else {
                tree->info[node].internal_label = alloc_internal_label(tc, cl, 2);
            }
            if (tree->nodes[node] == MVM_JIT_IF && if_val->state == MVM_JIT_VALUE_ALLOCATED) {
                /* We have been pre-assigned a register. Try to make sure the left and right branches also use that register */
                MVMJitExprValue *left_val = &tree->info[tree->nodes[node+2]].value,
                    *right_val = &tree->info[tree->nodes[node+3]].value;
                if (left_val->state == MVM_JIT_VALUE_EMPTY) {
                    MVM_jit_register_assign(tc, cl, left_val, if_val->reg_cls, if_val->reg_num);
                }
                if (right_val->state == MVM_JIT_VALUE_EMPTY) {
                    MVM_jit_register_assign(tc, cl, right_val, if_val->reg_cls, if_val->reg_num);
                }
            }
        }
        break;
    case MVM_JIT_ALL:
        {
            MVMint32 i, nchild = tree->nodes[node + 1], first_child = node + 2;
            /* Because of short-circuitng behavior, ALL is much like
             * nested WHEN, which means it's a conditional in
             * itself */
            MVM_jit_spill_before_conditional(tc, cl, tree, node);
            MVM_jit_enter_branch(tc, cl);
            for (i = 0; i < nchild; i++) {
                MVMint32 cond = tree->nodes[first_child+i];
                if (tree->nodes[cond] == MVM_JIT_ALL) {
                    /* Nested ALL short-circuits the same way as plain ALL */
                    tree->info[cond].internal_label = tree->info[node].internal_label;
                } else if (tree->nodes[cond] == MVM_JIT_ANY) {
                    /* Nested ANY short-circuits to continued evaluation if succesful,
                     * or out of ALL if not, thus we require a new label */
                    tree->info[cond].internal_label = alloc_internal_label(tc, cl, 1);
                }
            }
        }
        break;
    case MVM_JIT_ANY:
        {
            /* To deal with nested ANY/ALL combinations, we need to propagate and assign labels */
            MVMint32 i, nchild = tree->nodes[node + 1], first_child = node + 2;
            MVM_jit_spill_before_conditional(tc, cl, tree, node);
            MVM_jit_enter_branch(tc, cl);
            for (i = 0; i < nchild; i++) {
                MVMint32 cond = tree->nodes[first_child+i];
                if (tree->nodes[cond] == MVM_JIT_ALL) {
                    /* ANY ALL - if we reach the end of execution, that means ALL was valid,
                     * which implies the ANY should short-circuit. On the other hand, if ALL
                     * is not valid, it should jump beyond the short-circuit branch. Hence, we
                     * require a new label */
                    tree->info[cond].internal_label = alloc_internal_label(tc, cl, 1);
                } else if (tree->nodes[cond] == MVM_JIT_ANY) {
                    /* ANY ANY - is clearly equivalent to ANY */
                    tree->info[cond].internal_label = tree->info[node].internal_label;
                }
            }
        }
        break;
    default:
        break;
    }
}

/* Logical negation of MVMJitExprOp flags */
static enum MVMJitExprOp negate_flag(MVMThreadContext *tc, enum MVMJitExprOp op) {
    switch(op) {
    case MVM_JIT_LT:
        return MVM_JIT_GE;
    case MVM_JIT_LE:
        return MVM_JIT_GT;
    case MVM_JIT_EQ:
        return MVM_JIT_NE;
    case MVM_JIT_NE:
        return MVM_JIT_EQ;
    case MVM_JIT_GE:
        return MVM_JIT_LT;
    case MVM_JIT_GT:
        return MVM_JIT_LE;
    case MVM_JIT_NZ:
        return MVM_JIT_ZR;
    case MVM_JIT_ZR:
        return MVM_JIT_NZ;
    default:
        MVM_oops(tc, "Not a flag!");
    }
}

static void compile_labels(MVMThreadContext *tc, MVMJitTreeTraverser *traverser, MVMJitExprTree *tree, MVMint32 node, MVMint32 i) {
    MVMJitCompiler *cl = traverser->data;
    switch (tree->nodes[node]) {
    case MVM_JIT_WHEN:
        {
            MVMint32 cond  = tree->nodes[node+1];
            MVMint32 label = tree->info[node].internal_label;
            MVMint32 flag  = tree->nodes[cond];
            /* post-condition */
            if (i == 0) {
                if (flag == MVM_JIT_ALL) {
                    /* Do nothing, shortcircuit of ALL has skipped the
                       conditional block */
                } else if (flag == MVM_JIT_ANY) {
                    /* If ANY hasn't short-circuited into the
                       conditional block, jump beyond */
                    MVMJitBranch branch;
                    branch.ins  = NULL;
                    branch.dest = label + 1;
                    MVM_jit_emit_branch(tc, cl, cl->graph, &branch);
                    /* Emit label for the conditional block entry */
                    MVM_jit_emit_label(tc, cl, cl->graph, label);
                } else {
                    /* Other conditionals do not short-circuit, hence
                       require an explicit conditional branch */
                    MVM_jit_emit_conditional_branch(tc, cl, negate_flag(tc, flag), label);
                }
                MVM_jit_enter_branch(tc, cl);
            } else {
                if (flag == MVM_JIT_ANY) {
                    /* WHEN ANY skip label is label + 1 because label
                       + 0 is necessary to enter the conditional
                       block */
                    MVM_jit_emit_label(tc, cl, cl->graph, label + 1);
                } else {
                    /* That's not true of any other condition */
                    MVM_jit_emit_label(tc, cl, cl->graph, label);
                }
                MVM_jit_leave_branch(tc, cl);
            }
        }
        break;
    case MVM_JIT_IF:
    case MVM_JIT_EITHER:
        {
            MVMint32 cond  = tree->nodes[node+1];
            MVMint32 label = tree->info[node].internal_label;
            MVMint32 flag = tree->nodes[cond];
            if (i == 0) {
                if (flag == MVM_JIT_ALL) {
                    /* Like WHEN ALL, IF ALL short circuits into the
                       alternative block */
                } else if (flag == MVM_JIT_ANY) {
                    /* Like WHEN ANY, branch into the alternative
                     * block and emit a label for the conditional
                     * block */
                    MVMJitBranch branch;
                    branch.ins  = NULL;
                    branch.dest = label + 1;
                    MVM_jit_emit_branch(tc, cl, cl->graph, &branch);
                    MVM_jit_emit_label(tc, cl, cl->graph, label);
                } else {
                    MVM_jit_emit_conditional_branch(tc, cl, negate_flag(tc, flag), label);
                }
                MVM_jit_enter_branch(tc, cl);
            } else if (i == 1) {
                if (flag == MVM_JIT_ANY) {
                    /* IF ANY offsets the branch label by one */
                    MVMJitBranch branch;
                    branch.ins   = NULL;
                    branch.dest = label + 2;
                    MVM_jit_emit_branch(tc, cl, cl->graph, &branch);
                    MVM_jit_emit_label(tc, cl, cl->graph, label + 1);
                } else {
                    MVMJitBranch branch;
                    branch.ins   = NULL;
                    branch.dest = label + 1;
                    MVM_jit_emit_branch(tc, cl, cl->graph, &branch);
                    MVM_jit_emit_label(tc, cl, cl->graph, label);
                }
                MVM_jit_leave_branch(tc, cl);
                MVM_jit_enter_branch(tc, cl);
            } else {
                if (tree->nodes[node] == MVM_JIT_IF) {
                    MVMJitExprValue *left_val = &tree->info[tree->nodes[node+2]].value,
                        *right_val = &tree->info[tree->nodes[node+3]].value;
                    if (left_val->reg_cls != right_val->reg_cls || left_val->reg_num != right_val->reg_num) {
                        /* Ensure both left and right trees yield to the same value */
                        MVM_jit_emit_copy(tc, cl, left_val->reg_cls, left_val->reg_num, right_val->reg_cls, right_val->reg_num);
                    }
                }
                if (flag == MVM_JIT_ANY) {
                    MVM_jit_emit_label(tc, cl, cl->graph, label + 2);
                } else {
                    MVM_jit_emit_label(tc, cl, cl->graph, label + 1);
                }
                MVM_jit_leave_branch(tc, cl);
            }
        }
        break;
    case MVM_JIT_ALL:
        {
            MVMint32 cond = tree->nodes[node+2+i];
            MVMint32 label = tree->info[node].internal_label;
            if (tree->nodes[cond] == MVM_JIT_ALL) {
                /* Nested ALL short-circuits */
            } else if (tree->nodes[cond] == MVM_JIT_ANY) {
                /* If ANY reached it's end, that means it's false. So branch out */
                MVMJitBranch branch;
                branch.ins  = NULL;
                branch.dest = label;
                MVM_jit_emit_branch(tc, cl, cl->graph, &branch);
                /* And if ANY short-circuits we should continue the evaluation of ALL */
                MVM_jit_emit_label(tc, cl, cl->graph, tree->info[cond].internal_label);
            } else {
                /* Flag should be negated (if NOT condition we want to
                   short-circuit, otherwise we continue) */
                MVMint32 flag = negate_flag(tc, tree->nodes[cond]);
                MVM_jit_emit_conditional_branch(tc, cl, flag, label);
            }
        }
        break;
    case MVM_JIT_ANY:
        {
            MVMint32 cond  = tree->nodes[node+2+i];
            MVMint32 label = tree->info[node].internal_label;
            if (tree->nodes[cond] == MVM_JIT_ALL) {
                /* If ALL was succesful, we can branch out */
                MVMJitBranch branch;
                branch.ins  = NULL;
                branch.dest = label;
                MVM_jit_emit_branch(tc, cl, cl->graph, &branch);
                /* If not, it should short-circuit to continued evaluation */
                MVM_jit_emit_label(tc, cl, cl->graph, tree->info[cond].internal_label);
            } else if (tree->nodes[cond] == MVM_JIT_ANY) {
                /* Nothing to do here, since nested ANY already short-circuits */
            } else {
                /* Normal evaluation should short-circuit on truth values */
                MVM_jit_emit_conditional_branch(tc, cl, tree->nodes[cond], label);
            }
        }
        break;
    default:
        break;
    }
}



#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64
/* Localized definitions winning! */
static MVMint8 x64_gpr_args[] = {
    X64_ARG_GPR(MVM_JIT_REGNAME)
};


static MVMint8 x64_sse_args[] = {
    X64_ARG_SSE(MVM_JIT_REGNAME)
};


#if MVM_JIT_PLATFORM == MVM_JIT_PLATFORM_POSIX
static void compile_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler,
                            MVMJitExprTree *tree, MVMint32 node) {
    /* Let's first ensure we have all the necessary values in place */
    MVMint32 i, nchild = tree->nodes[node+1];
    MVMJitExprValue *gpr_values[6], *sse_values[8], *stack_values[16];
    MVMint32 num_gpr = 0, num_sse = 0, num_stack = 0;
    for (i = 0; i < nchild; i++) {
        MVMint32 carg = tree->nodes[node+2+i];
        MVMint32 argtyp = tree->nodes[carg+2];
        MVMJitExprValue *argval = &tree->info[tree->nodes[carg+1]].value;
        if (argtyp == MVM_JIT_NUM) {
            if (num_sse < (sizeof(sse_values)/sizeof(sse_values[0]))) {
                sse_values[num_sse++] = argval;
            } else {
                stack_values[num_stack++] = argval;
            }
        } else {
            if (num_gpr < (sizeof(gpr_values)/sizeof(gpr_values[0]))) {
                gpr_values[num_gpr++] = argval;
            } else {
                stack_values[num_stack++] = argval;
            }
        }
    }
    /* The following is pretty far from optimal. The optimal code
       would determine which values currently reside in registers and
       where they should be placed, and move them there, possibly
       freeing up registers for later access. But that requires some
       pretty complicated logic.  */

    /* Now emit the gpr values */
    for (i = 0; i < num_gpr; i++) {
        MVM_jit_register_put(tc, compiler, gpr_values[i], MVM_JIT_REGCLS_GPR, x64_gpr_args[i]);
        /* Mark GPR as used - nb, that means we need some cleanup logic afterwards */
        MVM_jit_register_use(tc, compiler, MVM_JIT_REGCLS_GPR, x64_gpr_args[i]);
    }

    /* SSE logic is pretty much the same as GPR logic, just with SSE rather than GPR args  */
    for (i = 0; i < num_sse; i++) {
        MVM_jit_register_put(tc, compiler, gpr_values[i], MVM_JIT_REGCLS_NUM, x64_sse_args[i]);
        MVM_jit_register_use(tc, compiler, MVM_JIT_REGCLS_NUM, x64_sse_args[i]);
    }

    /* Stack arguments are simpler than register arguments */
    for (i = 0; i < num_stack; i++) {
        MVMJitExprValue *argval = stack_values[i];
        if (argval->state == MVM_JIT_VALUE_SPILLED) {
            /* Allocate a temporary register, load and place, and free the register */
            MVMint8 reg_num = MVM_jit_register_alloc(tc, compiler, MVM_JIT_REGCLS_GPR);
            /* We do the load directly because, this value being
             * spilled and invalidated just after the call, there is
             * no reason to involve the register allocator */
            MVM_jit_emit_load(tc, compiler, argval->spill_location,
                              MVM_JIT_REGCLS_GPR, reg_num,  argval->size);
            MVM_jit_emit_stack_arg(tc, compiler, i * 8, MVM_JIT_REGCLS_GPR, reg_num, argval->size);
            MVM_jit_register_free(tc, compiler, MVM_JIT_REGCLS_GPR, reg_num);
        } else if (argval->state == MVM_JIT_VALUE_ALLOCATED) {
            /* Emitting a stack argument is not a free */
            MVM_jit_emit_stack_arg(tc, compiler, i * 8, argval->reg_cls,
                                   argval->reg_num, argval->size);
        } else {
            MVM_oops(tc, "ARGLIST: Live Value Inaccessible");
        }
    }

}
#else
static void compile_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                            MVMint32 node) {
    MVMint32 i, nchild = tree->nodes[node+1], first_child = node+2;
    /* TODO implement this too */
    NYI(compile_arglist_win32);
}
#endif

#else
/* No such architecture */
static void compile_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                            MVMint32 node) {
    NYI(compile_arglist);
}
#endif


static void pre_call(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprTree *tree, MVMint32 node) {
    /* Calls invalidate all caller-saved registers. Values that have
       not yet been spilled and are needed after the call will need to
       be spilled */
    MVM_jit_spill_before_call(tc, cl);
}

#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64
static void post_call(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprTree *tree, MVMint32 node) {
    /* Post-call, we might implement restore, if the call was
       conditional. But that is something for another day */
    MVMint32 i;
    /* ARGLIST locked all registers and we have to wait until after the CALL to unlock them */
    for (i = 0; i < sizeof(x64_gpr_args); i++) {
        MVM_jit_register_release(tc, cl, MVM_JIT_REGCLS_GPR, x64_gpr_args[i]);
    }
    /* TODO same for numargs */
}

#else
static void post_call(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprTree *tree, MVMint32 node) {
    NYI(post_call);
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

    /* Increment order nr - must follow the same convention as in tile.c:select_values */
    cl->order_nr++;
    values[0] = &tree->info[node].value;

    switch (tree->nodes[node]) {
    case MVM_JIT_CALL:
        /* we should have a tile */
        if (tile == NULL || tile->rule == NULL) {
            MVM_oops(tc, "Tile without a rule!");
        }
        /* pre call we should store all values */
        pre_call(tc, cl, tree, node);
        /* Emit call */
        MVM_jit_tile_get_values(tc, tree, node, tile->path, values+1);
        ensure_values(tc, cl, values+1, tile->num_values);
        tile->rule(tc, cl, tree, node, values, args);

        /* Update return value */
        if (args[0] == MVM_JIT_NUM) {
            MVM_jit_register_take(tc, cl, MVM_JIT_REGCLS_NUM, MVM_JIT_RETVAL_NUM);
            MVM_jit_register_assign(tc, cl, values[0], MVM_JIT_REGCLS_NUM, MVM_JIT_RETVAL_NUM);
        } else if (args[0] != MVM_JIT_VOID) {
            MVM_jit_register_take(tc, cl, MVM_JIT_REGCLS_GPR, MVM_JIT_RETVAL_GPR);
            MVM_jit_register_assign(tc, cl, values[0], MVM_JIT_REGCLS_GPR, MVM_JIT_RETVAL_GPR);
        }
        values[0]->type = (args[0] == MVM_JIT_VOID ? MVM_JIT_VOID : MVM_JIT_REG);
        post_call(tc, cl, tree, node);
        break;

    case MVM_JIT_ARGLIST:
        compile_arglist(tc, cl, tree, node);
        /* compile_arglist locks values into registers. */
        break;
    case MVM_JIT_CARG:
        break;
    case MVM_JIT_ALL:
    case MVM_JIT_ANY:
        MVM_jit_leave_branch(tc, cl);
        break;
    case MVM_JIT_WHEN:
    case MVM_JIT_EITHER:
        break;
    case MVM_JIT_IF:
        /* The IF conditional assigned register is live by construction */
        {
            MVMJitExprValue *left_val = &tree->info[tree->nodes[node+2]].value;
            values[0]->type == MVM_JIT_REG;
            MVM_jit_register_take(tc, cl, left_val->reg_cls, left_val->reg_num);
            MVM_jit_register_assign(tc, cl, values[0], left_val->reg_cls, left_val->reg_num);
        }
        break;
    case MVM_JIT_DO:
        {
            MVMint32 last = node + 1 + tree->nodes[node+1];
            MVMint32 last_child = tree->nodes[last];
            MVMJitExprValue *last_value  = &tree->info[last_child].value;
            if (last_value->type == MVM_JIT_REG) {
                /* Virtual copy */
                MVM_jit_register_assign(tc, cl, values[0], last_value->reg_cls, last_value->reg_num);
            }
            /* otherwise is the void case */
        }
        break;
    case MVM_JIT_COPY:
        /* Virtual copy */
        {
            values[0]->type = MVM_JIT_REG;
            values[1] = &tree->info[tree->nodes[node+1]].value;
            MVM_jit_register_assign(tc, cl, values[0], values[1]->reg_cls, values[1]->reg_num);
        }
        break;
    case MVM_JIT_LOCAL:
    case MVM_JIT_FRAME:
    case MVM_JIT_TC:
    case MVM_JIT_CU:
    case MVM_JIT_STACK:
        if (tile->rule == NULL)
            return;
        tile->rule(tc, cl, tree, node, values, args);
        break;
    default:
        {
            if (tile->rule == NULL)
                /* Empty tile rule */
                return;
            /* Extract value pointers from the tree */
            MVM_jit_tile_get_values(tc, tree, node, tile->path, values+1);
            ensure_values(tc, cl, values+1, tile->num_values);

            if (tile->vtype == MVM_JIT_REG) {
                /* allocate a register for the result */
                if (tile->num_values > 0 &&
                    values[1]->type == MVM_JIT_REG &&
                    values[1]->state == MVM_JIT_VALUE_ALLOCATED &&
                    values[1]->last_use == cl->order_nr) {
                    /* First register expires immediately, therefore we can safely cross-assign */
                    MVM_jit_register_assign(tc, cl, values[0], values[1]->reg_cls, values[1]->reg_num);
                } else {
                    alloc_value(tc, cl, values[0]);
                }
                use_value(tc, cl, values[0]);
            }
            values[0]->type = tile->vtype;
            /* Emit code */
            tile->rule(tc, cl, tree, node, values, args);
            /* Release registers from use */
            for (i = 0; i < tile->num_values + 1; i++) {
                if (values[i]->type == MVM_JIT_REG) {
                    release_value(tc, cl, values[i]);
                }
            }
        }
        break;
    }
    /* Expire dead values */
    MVM_jit_expire_values(tc, cl);
}

void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;
    MVMJitRegisterAllocator allocator;
    /* First stage, tile the tree */
    MVM_jit_tile_expr_tree(tc, tree);
    /* Second stage, emit the code - interleaved with the register allocator */
    traverser.policy    = MVM_JIT_TRAVERSER_ONCE;
    traverser.preorder  = &prepare_tile;
    traverser.inorder   = &compile_labels;
    traverser.postorder = &compile_tile;
    traverser.data      = compiler;
    compiler->order_nr  = 0;

    MVM_jit_register_allocator_init(tc, compiler, &allocator);
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
    MVM_jit_register_allocator_deinit(tc, compiler, &allocator);
}


/* Enter the JIT code segment. The label is a continuation point where control
 * is resumed after the frame is properly setup. */
void MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                        MVMJitCode *code) {
    void *label = tc->cur_frame->jit_entry_label;
    code->func_ptr(tc, cu, label);
}
