#include "moar.h"
#include "internal.h"
#include "platform/mmap.h"


void MVM_jit_compiler_init(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_compiler_deinit(MVMThreadContext *tc, MVMJitCompiler *compiler);
MVMJitCode * MVM_jit_compiler_assemble(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *graph, MVMJitExprTree *tree);


#define COPY_ARRAY(a, n) ((n) > 0) ? memcpy(MVM_malloc((n) * sizeof(a[0])), a, (n) * sizeof(a[0])) : NULL;

static const MVMuint16 MAGIC_BYTECODE[] = { MVM_OP_sp_jit_enter, 0 };

void MVM_jit_compiler_init(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitGraph *jg) {
    /* Create dasm state */
    dasm_init(cl, 2);
    dasm_setupglobal(cl, cl->dasm_globals, MVM_JIT_MAX_GLOBALS);
    dasm_setup(cl, MVM_jit_actions());

    /* Store graph we're compiling */
    cl->graph        = jg;
    /* next (internal) label to assign */
    cl->label_offset = jg->num_labels;
    /* space for dynamic labels */
    dasm_growpc(cl, jg->num_labels);

    /* Spill offset and free list */
    cl->spills_base = jg->sg->num_locals * sizeof(MVMRegister);
    memset(cl->spills_free, -1, sizeof(cl->spills_free));
    MVM_VECTOR_INIT(cl->spills, 4);
}


void MVM_jit_compiler_deinit(MVMThreadContext *tc, MVMJitCompiler *cl) {
    dasm_free(cl);
    MVM_VECTOR_DESTROY(cl->spills);
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
            MVM_jit_emit_control(tc, &cl, &node->u.control, NULL);
            break;
        case MVM_JIT_NODE_EXPR_TREE:
            MVM_jit_compile_expr_tree(tc, &cl, jg, node->u.tree);
            break;
        case MVM_JIT_NODE_DATA:
            MVM_jit_emit_data(tc, &cl, &node->u.data);
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

    MVMint32 dasm_error = 0;

   /* compile the function */
    if ((dasm_error = dasm_link(cl, &codesize)) != 0) {
        MVM_jit_log(tc, "DynASM could not link, error: %d\n", dasm_error);
        return NULL;
    }

    memory = MVM_platform_alloc_pages(codesize, MVM_PAGE_READ|MVM_PAGE_WRITE);
    if ((dasm_error = dasm_encode(cl, memory)) != 0) {
        MVM_jit_log(tc, "DynASM could not encode, error: %d\n", dasm_error);
        return NULL;
    }

    /* set memory readable + executable */
    if (!MVM_platform_set_page_mode(memory, codesize, MVM_PAGE_READ|MVM_PAGE_EXEC)) {
        MVM_jit_log(tc, "Setting jit page executable failed or was denied. deactivating jit.\n");
        /* our caller allocated the compiler and our caller must clean it up */
        tc->instance->jit_enabled = 0;
        return NULL;
    }

    MVM_jit_log(tc, "Bytecode size: %"MVM_PRSz"\n", codesize);

    /* Create code segment */
    code = MVM_malloc(sizeof(MVMJitCode));
    code->func_ptr   = (void (*)(MVMThreadContext*,MVMCompUnit*,void*)) memory;
    code->size       = codesize;
    code->bytecode   = (MVMuint8*)MAGIC_BYTECODE;
    code->sf         = jg->sg->sf;
    code->spill_size = cl->spills_num;
    if (cl->spills_num > 0) {
        MVMint32 sg_num_locals = jg->sg->num_locals;
        code->num_locals  = sg_num_locals + cl->spills_num;
        code->local_types = MVM_malloc(code->num_locals * sizeof(MVMuint16));
        if (jg->sg->local_types != NULL) {
            memcpy(code->local_types, jg->sg->local_types, sizeof(MVMuint16)*sg_num_locals);
        } else {
            memcpy(code->local_types, code->sf->body.local_types, sizeof(MVMuint16)*sg_num_locals);
        }
        for (i = 0; i < cl->spills_num; i++) {
            code->local_types[sg_num_locals + i] = cl->spills[i].reg_type;
        }
    } else {
        code->local_types = NULL;
        code->num_locals  = 0;
    }

    /* Get the basic block labels */
    code->num_labels = jg->num_labels;
    code->labels = MVM_calloc(code->num_labels, sizeof(void*));

    for (i = 0; i < code->num_labels; i++) {
        MVMint32 offset = dasm_getpclabel(cl, i);
        if (offset < 0)
            MVM_jit_log(tc, "Got negative offset for dynamic label %d\n", i);
        code->labels[i] = memory + offset;
    }
    /* We only ever use one global label, which is the exit label */
    code->exit_label = cl->dasm_globals[0];

    /* Copy the deopts, inlines, and handlers. Because these use the
     * label index rather than the direct pointer, no fixup is
     * necessary */
    code->num_deopts   = jg->deopts_num;
    code->deopts       = COPY_ARRAY(jg->deopts, jg->deopts_num);
    code->num_handlers = jg->handlers_num;
    code->handlers     = COPY_ARRAY(jg->handlers, jg->handlers_alloc);
    code->num_inlines  = jg->inlines_num;
    code->inlines      = COPY_ARRAY(jg->inlines, jg->inlines_alloc);

    /* add sequence number */
    code->seq_nr       = tc->instance->jit_seq_nr++;

    return code;
}

void MVM_jit_code_destroy(MVMThreadContext *tc, MVMJitCode *code) {
    MVM_platform_free_pages(code->func_ptr, code->size);
    MVM_free(code->labels);
    MVM_free(code->deopts);
    MVM_free(code->handlers);
    MVM_free(code->inlines);
    MVM_free(code->local_types);
    MVM_free(code);
}



#define NYI(x) MVM_oops(tc, #x " NYI")


/* pseudotile emit functions */
void MVM_jit_compile_branch(MVMThreadContext *tc, MVMJitCompiler *compiler,
                            MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_branch(tc, compiler, tile->args[0]);
}

void MVM_jit_compile_conditional_branch(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                        MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_conditional_branch(tc, compiler, tile->args[0], tile->args[1]);
}

void MVM_jit_compile_label(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_label(tc, compiler, tree->graph, tile->args[0]);
}

void MVM_jit_compile_store(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_store(tc, compiler, tile->args[0], tile->args[1],
                       MVM_JIT_STORAGE_GPR, tile->values[1], sizeof(MVMRegister));
}

void MVM_jit_compile_memory_copy(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                 MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_load(tc, compiler, MVM_JIT_STORAGE_GPR, tile->values[1],
                      tile->args[2], tile->args[3], sizeof(MVMRegister));
    MVM_jit_emit_store(tc, compiler, tile->args[0], tile->args[1],
                       MVM_JIT_STORAGE_GPR, tile->values[1], sizeof(MVMRegister));
}

void MVM_jit_compile_move(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_copy(tc, compiler, MVM_JIT_STORAGE_GPR, tile->values[0],
                      MVM_JIT_STORAGE_GPR, tile->values[1]);
}

void MVM_jit_compile_load(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_load(tc, compiler,
                      MVM_JIT_STORAGE_GPR, tile->values[0],
                      tile->args[0], tile->args[1],
                      sizeof(MVMRegister));
}

void MVM_jit_compile_guard(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_control(tc, compiler, NULL, tile);
}

void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg, MVMJitExprTree *tree) {
    MVMJitTileList *list;
    MVMJitTile *tile;
    MVMint32 i;
    /* First stage, tile the tree */
    list = MVM_jit_tile_expr_tree(tc, compiler, tree);
    MVM_jit_log_tile_list(tc, list);

    /* Second stage, allocate registers */
    MVM_jit_linear_scan_allocate(tc, compiler, list);

    /* Allocate sufficient space for the new internal labels */
    dasm_growpc(compiler, compiler->label_offset);

    /* Third stage, emit the code */
    for (i = 0; i < list->items_num; i++) {
        tile = list->items[i];
        /* definition tiles etc. have NULL emit rules */
        if (tile->emit != NULL) {
            tile->emit(tc, compiler, tile, tree);
        }
    }
    /* Cleanup tile lits */
    MVM_jit_tile_list_destroy(tc, list);
}

MVM_STATIC_INLINE MVMint32 reg_type_bucket(MVMint8 reg_type) {
    switch (reg_type) {
    case MVM_reg_num32:
    case MVM_reg_num64:
        return 1;
        break;
    case MVM_reg_str:
        return 2;
        break;
    case MVM_reg_obj:
        return 3;
        break;
    default:
        break;
    }
    return 0;
}


MVMint32 MVM_jit_spill_memory_select(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint8 reg_type) {
    MVMint32 idx;
    MVMint8 bucket = reg_type_bucket(reg_type);

    if (compiler->spills_free[bucket] >= 0) {
        idx = compiler->spills_free[bucket];
        compiler->spills_free[bucket] = compiler->spills[idx].next;
    } else {
        MVM_VECTOR_ENSURE_SPACE(compiler->spills, idx = compiler->spills_num++);
        compiler->spills[idx].reg_type = reg_type;
    }
    return compiler->spills_base + idx * sizeof(MVMRegister);
}

void MVM_jit_spill_memory_release(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 pos, MVMint8 reg_type) {
    MVMint32 idx   = (pos - compiler->spills_base) / sizeof(MVMRegister);
    MVMint8 bucket = reg_type_bucket(reg_type);
    compiler->spills[idx].next    = compiler->spills_free[bucket];
    compiler->spills_free[bucket] = idx;
}
