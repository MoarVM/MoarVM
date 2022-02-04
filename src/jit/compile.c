#include "moar.h"
#include "internal.h"
#include "platform/mmap.h"


void MVM_jit_compiler_init(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_compiler_deinit(MVMThreadContext *tc, MVMJitCompiler *compiler);
MVMJitCode * MVM_jit_compiler_assemble(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *graph, MVMJitExprTree *tree);


#define COPY_ARRAY(a, n) ((n) > 0) ? memcpy(MVM_malloc((n) * sizeof(a[0])), a, (n) * sizeof(a[0])) : NULL;

static const MVMuint16 MAGIC_BYTECODE[] = { MVM_OP_sp_jit_enter, 0 };

/* TODO this should be a debug utility somewhere */
const char * MVM_register_type(MVMint8 reg_type) {
    switch (reg_type) {
    case MVM_reg_int8: return "int8";
    case MVM_reg_int16: return "int16";
    case MVM_reg_int32: return "int32";
    case MVM_reg_int64: return "int64";
    case MVM_reg_num32: return "num32";
    case MVM_reg_num64: return "num64";
    case MVM_reg_str: return "str";
    case MVM_reg_obj: return "obj";
    case MVM_reg_uint8: return "uint8";
    case MVM_reg_uint16: return "uint16";
    case MVM_reg_uint32: return "uint32";
    case MVM_reg_uint64: return "uint64";
    default: return "unknown";
    }
}

static void debug_spill_map(MVMThreadContext *tc, MVMJitCompiler *cl) {
    MVMuint32 i;
    if (!MVM_jit_debug_enabled(tc))
        return;
    MVM_spesh_debug_printf(tc, "JIT Spilled: %d offset %x\n", MVM_VECTOR_ELEMS(cl->spills), cl->spills_base);
    for (i = 0; i < MVM_VECTOR_ELEMS(cl->spills); i++) {
        MVM_spesh_debug_printf(tc, "    r%u [%lx] = %s\n", i, cl->spills_base + i * sizeof(MVMRegister),
                               MVM_register_type(cl->spills[i].reg_type));
    }
}

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
        case MVM_JIT_NODE_RUNBYTECODE:
            MVM_jit_emit_runbytecode(tc, &cl, jg, &node->u.runbytecode);
            break;
        case MVM_JIT_NODE_RUNCCODE:
            MVM_jit_emit_runccode(tc, &cl, jg, &node->u.runccode);
            break;
        case MVM_JIT_NODE_RUNNATIVECALL:
            MVM_jit_emit_runnativecall(tc, &cl, jg, &node->u.runnativecall);
            break;
        case MVM_JIT_NODE_DISPATCH:
            MVM_jit_emit_dispatch(tc, &cl, jg, &node->u.dispatch);
            break;
        case MVM_JIT_NODE_ISTYPE:
            MVM_jit_emit_istype(tc, &cl, jg, &node->u.istype);
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
        case MVM_JIT_NODE_DEOPT_CHECK:
            MVM_jit_emit_deopt_check(tc, &cl);
            break;
        }
        node = node->next;
    }
    MVM_jit_emit_epilogue(tc, &cl, jg);

    debug_spill_map(tc, &cl);
    /* Generate code */
    code = MVM_jit_compiler_assemble(tc, &cl, jg);

    /* Clear up the compiler */
    MVM_jit_compiler_deinit(tc, &cl);

#if linux
    /* Native Call compiles code that doesn't correspond
     * to a staticframe, in which case we just skip this.
     * Sometimes code ends up null here as well, in which
     * case we also skip. */
    if (tc->instance->jit_perf_map && jg->sg->sf && code) {
        MVMStaticFrame *sf = jg->sg->sf;
        char symbol_name[1024];
        char *file_location = MVM_staticframe_file_location(tc, sf);
        char *frame_name = MVM_string_utf8_encode_C_string(tc, sf->body.name);
        snprintf(symbol_name, sizeof(symbol_name) - 1,
                 "%s(%s)",  frame_name, file_location);
        fprintf(tc->instance->jit_perf_map, "%lx %lx %s\n",
                (unsigned long) code->func_ptr, code->size, symbol_name);
        fflush(tc->instance->jit_perf_map);
        MVM_free(file_location);
        MVM_free(frame_name);
    }
#endif

    /* Logging for insight */
    if (MVM_jit_bytecode_dump_enabled(tc) && code)
        MVM_jit_dump_bytecode(tc, code);

    return code;
}

MVMJitCode * MVM_jit_compiler_assemble(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitGraph *jg) {
    MVMJitCode * code;
    MVMuint32 i;
    char * memory;
    size_t codesize;

    MVMint32 dasm_error = 0;

   /* compile the function */
    if ((dasm_error = dasm_link(cl, &codesize)) != 0) {
        if (tc->instance->jit_debug_enabled)
            fprintf(stderr, "DynASM could not link, error: %d\n", dasm_error);
        return NULL;
    }

    memory = MVM_platform_alloc_pages(codesize, MVM_PAGE_READ|MVM_PAGE_WRITE);
    if ((dasm_error = dasm_encode(cl, memory)) != 0) {
        if (tc->instance->jit_debug_enabled)
            fprintf(stderr, "DynASM could not encode, error: %d\n", dasm_error);
        return NULL;
    }

    /* set memory readable + executable */
    if (!MVM_platform_set_page_mode(memory, codesize, MVM_PAGE_READ|MVM_PAGE_EXEC)) {
        if (tc->instance->jit_debug_enabled)
            fprintf(stderr, "JIT: Impossible to mark code read/executable");
        /* our caller allocated the compiler and our caller must clean it up */
        tc->instance->jit_enabled = 0;
        return NULL;
    }

    /* Create code segment */
    code = MVM_calloc(1, sizeof(MVMJitCode));

    code->func_ptr   = (void (*)(MVMThreadContext*,MVMCompUnit*,void*)) memory;
    code->size       = codesize;
    code->bytecode   = (MVMuint8*)MAGIC_BYTECODE;

    /* add sequence number */
    code->seq_nr       = tc->instance->spesh_produced;

    /* by definition */
    code->ref_cnt      = 1;

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
        if (offset < 0) {
            if (tc->instance->jit_debug_enabled)
                fprintf(stderr, "JIT ERROR: Negative offset for dynamic label %d\n", i);
            MVM_jit_code_destroy(tc, code);
            return NULL;
        }
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


    return code;
}

MVMJitCode* MVM_jit_code_copy(MVMThreadContext *tc, MVMJitCode * const code) {
#ifdef MVM_USE_C11_ATOMICS
    atomic_fetch_add_explicit(&code->ref_cnt, 1, memory_order_relaxed);
#else
    AO_fetch_and_add1(&code->ref_cnt);
#endif
    return code;
}

void MVM_jit_code_destroy(MVMThreadContext *tc, MVMJitCode *code) {
    /* fetch_and_sub1 returns previous value, so check if there's only 1 reference */
#ifdef MVM_USE_C11_ATOMICS
    if (atomic_fetch_sub_explicit(&code->ref_cnt, 1, memory_order_relaxed) > 1)
        return;
#else
    if (AO_fetch_and_sub1(&code->ref_cnt) > 1)
        return;
#endif
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
    MVM_jit_emit_conditional_branch(tc, compiler, tile->args[0], tile->args[1], tile->args[2]);
}

void MVM_jit_compile_label(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_label(tc, compiler, tree->graph, tile->args[0]);
}

void MVM_jit_compile_store(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_store(tc, compiler, tile->args[0], tile->args[1], tile->values[1], sizeof(MVMRegister));
}

void MVM_jit_compile_memory_copy(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                 MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_load(tc, compiler, tile->values[1], tile->args[2], tile->args[3], sizeof(MVMRegister));
    MVM_jit_emit_store(tc, compiler, tile->args[0], tile->args[1], tile->values[1], sizeof(MVMRegister));
}

void MVM_jit_compile_move(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_copy(tc, compiler, tile->values[0], tile->values[1]);
}

void MVM_jit_compile_load(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_load(tc, compiler, tile->values[0],  tile->args[0], tile->args[1], sizeof(MVMRegister));
}

void MVM_jit_compile_guard(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMJitTile *tile, MVMJitExprTree *tree) {
    MVM_jit_emit_control(tc, compiler, NULL, tile);
}

void MVM_jit_compile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg, MVMJitExprTree *tree) {
    MVMJitTileList *list;
    MVMJitTile *tile;
    MVMuint32 i;

    /* Log what we are planning to compile */
    if (MVM_jit_debug_enabled(tc))
        MVM_jit_dump_expr_tree(tc, tree);

    /* First stage, tile the tree */
    list = MVM_jit_tile_expr_tree(tc, compiler, tree);

    if (MVM_jit_debug_enabled(tc))
        MVM_jit_dump_tile_list(tc, list);

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


MVMuint32 MVM_jit_spill_memory_select(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint8 reg_type) {
    MVMuint32 idx;
    MVMint8 bucket = reg_type_bucket(reg_type);

    if (compiler->spills_free[bucket] >= 0) {
        idx = compiler->spills_free[bucket];
        compiler->spills_free[bucket] = compiler->spills[idx].next;
    } else {
        MVM_VECTOR_ENSURE_SPACE(compiler->spills, compiler->spills_num);
        idx = compiler->spills_num++;
        compiler->spills[idx].reg_type = reg_type;
    }
    return compiler->spills_base + idx * sizeof(MVMRegister);
}

void MVM_jit_spill_memory_release(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMuint32 pos, MVMint8 reg_type) {
    MVMuint32 idx   = (pos - compiler->spills_base) / sizeof(MVMRegister);
    MVMint8 bucket = reg_type_bucket(reg_type);
    compiler->spills[idx].next    = compiler->spills_free[bucket];
    compiler->spills_free[bucket] = idx;
}
