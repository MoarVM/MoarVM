#include "moar.h"
#include "internal.h"
#include "platform/mmap.h"


void MVM_jit_compiler_init(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_compiler_deinit(MVMThreadContext *tc, MVMJitCompiler *compiler);
MVMJitCode * MVM_jit_compiler_assemble(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *graph, MVMJitExprTree *tree);
void MVM_jit_allocate_registers(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree, MVMJitTileList *list);

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
    cl->graph        = jg;
    /* next (internal) label to assign */
    cl->label_offset = jg->num_labels;
    /* space for dynamic labels */
    dasm_growpc(cl, jg->num_labels);
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
            MVM_jit_emit_block_branch(tc, &cl, jg, &node->u.branch);
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
    code->num_labels = jg->num_labels;
    code->labels = MVM_calloc(code->num_labels, sizeof(void*));

    for (i = 0; i < code->num_labels; i++) {
        MVMint32 offset = dasm_getpclabel(cl, i);
        if (offset < 0)
            MVM_jit_log(tc, "Got negative offset for dynamic label %d\n", i);
        code->labels[i] = memory + offset;
    }

    /* Copy the deopts, inlines, and handlers. Because these use the
     * label index rather than the direct pointer, no fixup is
     * necessary */
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
    MVM_free(code->labels);
    MVM_free(code->deopts);
    MVM_free(code->handlers);
    MVM_free(code->inlines);
    MVM_free(code);
}



#define NYI(x) MVM_oops(tc, #x " NYI")


void MVM_jit_compile_breakpoint(void) {
    fprintf(stderr, "Pause here please\n");
}



static void arglist_get_values(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint32 node, MVMJitExprValue **values) {
    MVMint32 i, nchild = tree->nodes[node+1];
    for (i = 0; i < nchild; i++) {
        MVMint32 carg = tree->nodes[node+2+i];
        MVMint32 val  = tree->nodes[carg+1];
        *values++     = &tree->info[val].value;
    }
}


static void MVM_jit_get_values(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree, MVMJitTile *tile) {
    MVMint32 node = tile->node;
    const MVMJitTileTemplate *template = tile->template;

    tile->values[0]       = &tree->info[node].value;
    tile->values[0]->type = template->vtype;
    switch (tree->nodes[node]) {
    case MVM_JIT_IF:
    {
        MVMint32 left = tree->nodes[node+2], right = tree->nodes[node+3];
        /* assign results of IF to values array */
        tile->values[1] = &tree->info[left].value;
        tile->values[2] = &tree->info[right].value;
        tile->num_vals  = 2;
        break;
    }
    case MVM_JIT_ARGLIST:
    {
        /* NB, arglist can conceivably use more than 7 values, although it can safely overflow into args, we may want to find a better solution */
        arglist_get_values(tc, tree, node, tile->values + 1);
        tile->num_vals = tree->nodes[node+1];
        break;
    }
    case MVM_JIT_DO:
    {
        MVMint32 nchild     = tree->nodes[node+1];
        MVMint32 last_child = tree->nodes[node+1+nchild];
        tile->values[1] = &tree->info[last_child].value;
        tile->num_vals  = 1;
        break;
    }
    default:
    {
        MVM_jit_tile_get_values(tc, tree, node,
                                template->path, template->regs,
                                tile->values + 1, tile->args);
        tile->num_vals = template->num_vals;
        break;
    }
    }
}



void MVM_jit_allocate_registers(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree, MVMJitTileList *list) {
    MVMJitTile *tile;
    MVMJitExprValue *value;
    MVMint32 i, j;
    MVMint8 reg;
    /* Get value descriptors and calculate live ranges */
    for (tile = list->first; tile != NULL; tile = tile->next) {
        if (tile->template == NULL) /* pseudotiles */
            continue;
        MVM_jit_get_values(tc, compiler, list->tree, tile);
        tile->values[0]->first_created = tile->order_nr;
        for (i = 0; i < tile->num_vals; i++) {
            tile->values[i+1]->last_use = tile->order_nr;
            tile->values[i+1]->num_use++;
        }
    }

    /* Assign registers */
    i = 0;
    for (tile = list->first; tile != NULL; tile = tile->next) {
        if (tile->template == NULL)
            continue;
        i++;
        /* ensure that register values are live */
        for (j = 1; j < tile->num_vals; j++) {
            value = tile->values[j];
            if (value->type != MVM_JIT_REG)
                continue;
            if (value->state == MVM_JIT_VALUE_SPILLED) {
                /* TODO insert load in place */
                NYI(load_spilled);
            } else if (value->state == MVM_JIT_VALUE_EMPTY ||
                       value->state == MVM_JIT_VALUE_DEAD) {
                MVM_oops(tc, "Required value is not live");
            }
            /* Mark value as in-use */
            MVM_jit_register_use(tc, compiler, value->reg_cls, value->reg_num);
        }

        /* allocate input register if necessary */
        value = tile->values[0];
        switch(tree->nodes[tile->node]) {
        case MVM_JIT_COPY:
            /* use same register as input  */
            value->type = MVM_JIT_REG;
            MVM_jit_register_assign(tc, compiler, value, tile->values[1]->reg_cls, tile->values[1]->reg_num);
            break;
        case MVM_JIT_TC:
            /* TODO, this isn't really portable, we should have register
             * attributes assigned to the tile itself */
            value->type = MVM_JIT_REG;
            value->state = MVM_JIT_VALUE_IMMORTAL;
            value->reg_cls = MVM_JIT_REGCLS_GPR;
            value->reg_num = MVM_JIT_REG_TC;
            break;
        case MVM_JIT_CU:
            value->type = MVM_JIT_REG;
            value->state = MVM_JIT_VALUE_IMMORTAL;
            value->reg_cls = MVM_JIT_REGCLS_GPR;
            value->reg_num = MVM_JIT_REG_CU;
            break;
        case MVM_JIT_LOCAL:
            value->type = MVM_JIT_REG;
            value->state = MVM_JIT_VALUE_IMMORTAL;
            value->reg_cls = MVM_JIT_REGCLS_GPR;
            value->reg_num = MVM_JIT_REG_LOCAL;
            break;
        case MVM_JIT_STACK:
            value->type = MVM_JIT_REG;
            value->state = MVM_JIT_VALUE_IMMORTAL;
            value->reg_cls = MVM_JIT_REGCLS_GPR;
            value->reg_num = MVM_JIT_REG_STACK;
            break;
        default:
            if (value != NULL && value->type == MVM_JIT_REG) {
                /* allocate a register for the result */
                if (tile->num_vals > 0 &&
                    tile->values[1]->type == MVM_JIT_REG &&
                    tile->values[1]->state == MVM_JIT_VALUE_ALLOCATED &&
                    tile->values[1]->last_use == i) {
                    /* First register expires immediately, therefore we can safely cross-assign */
                    MVM_jit_register_assign(tc, compiler, value, tile->values[1]->reg_cls, tile->values[1]->reg_num);
                } else {
                    reg = MVM_jit_register_alloc(tc, compiler, MVM_JIT_REGCLS_GPR);
                    MVM_jit_register_assign(tc, compiler, value, MVM_JIT_REGCLS_GPR, reg);
                }
            }
            MVM_jit_register_use(tc, compiler, value->reg_cls, value->reg_num);
            break;
        }
        for (j = 0; j < tile->num_vals; j++) {
            if (tile->values[j] != NULL && tile->values[j]->type == MVM_JIT_REG) {
                MVM_jit_register_release(tc, compiler, tile->values[j]->reg_cls, tile->values[j]->reg_num);
            }
        }
        /* Expire dead values */
        MVM_jit_expire_values(tc, compiler, i);
    }
}



/* pseudotile emit functions */
void MVM_jit_compile_branch(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                            MVMint32 node, MVMJitExprValue **value, MVMJitExprNode *args) {
    MVM_jit_emit_branch(tc, compiler, args[0] + compiler->label_offset);
}

void MVM_jit_compile_conditional_branch(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                        MVMJitExprTree *tree, MVMint32 node,
                                        MVMJitExprValue **values, MVMJitExprNode *args) {
    MVM_jit_emit_conditional_branch(tc, compiler, args[0], args[1] + compiler->label_offset);
}

void MVM_jit_compile_label(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMJitExprTree *tree, MVMint32 node,
                           MVMJitExprValue **values, MVMJitExprNode *args) {
    MVM_jit_emit_label(tc, compiler, tree->graph, args[0] + compiler->label_offset);
}


void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg, MVMJitExprTree *tree) {
    MVMJitRegisterAllocator allocator;
    MVMJitTileList *list;
    MVMJitTile *tile;
    /* First stage, tile the tree */
    list = MVM_jit_tile_expr_tree(tc, tree);

    /* log it, replacing logigng-during-compilation */
    MVM_jit_log_tile_list(tc, list);

    /* Second stage, allocate registers */
    MVM_jit_register_allocator_init(tc, compiler, &allocator);
    MVM_jit_allocate_registers(tc, compiler, tree, list);
    MVM_jit_register_allocator_deinit(tc, compiler, &allocator);

    /* Allocate sufficient space for the internal labels */
    dasm_growpc(compiler, compiler->label_offset + tree->num_labels);

    /* Third stage, emit the code */
    for (tile = list->first; tile != NULL; tile = tile->next) {
        tile->emit(tc, compiler, tree, tile->node, tile->values, tile->args);
    }


    /* Make sure no other tree reuses the same labels */
    compiler->label_offset += tree->num_labels;
}



/* Enter the JIT code segment. The label is a continuation point where control
 * is resumed after the frame is properly setup. */
void MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                        MVMJitCode *code) {
    void *label = tc->cur_frame->jit_entry_label;
    if (label < (void*)code->func_ptr || (char*)label > (((char*)code->func_ptr) + code->size))
        MVM_oops(tc, "JIT entry label out of range for code!\n"
                 "(label %x, func_ptr %x, code size %d, offset %d, frame_nr %d, seq nr %d)",
                 label, code->func_ptr, code->size, ((char*)label) - ((char*)code->func_ptr),
                 tc->cur_frame->sequence_nr, code->seq_nr);
    code->func_ptr(tc, cu, label);
}
