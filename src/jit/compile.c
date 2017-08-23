#include "moar.h"
#include "internal.h"
#include "platform/mmap.h"


void MVM_jit_compiler_init(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_compiler_deinit(MVMThreadContext *tc, MVMJitCompiler *compiler);
MVMJitCode * MVM_jit_compiler_assemble(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);

#define COPY_ARRAY(a, n) ((n) > 0) ? memcpy(MVM_malloc((n) * sizeof(a[0])), a, (n) * sizeof(a[0])) : NULL;

static const MVMuint16 MAGIC_BYTECODE[] = { MVM_OP_sp_jit_enter, 0 };

void MVM_jit_compiler_init(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitGraph *jg) {
    MVMint32  num_globals = MVM_jit_num_globals();
    /* Create dasm state */
    dasm_init(cl, 2);
    cl->dasm_globals = MVM_malloc(num_globals * sizeof(void*));
    dasm_setupglobal(cl, cl->dasm_globals, num_globals);
    dasm_setup(cl, MVM_jit_actions());

    /* space for dynamic labels */
    dasm_growpc(cl, jg->num_labels);
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
        case MVM_JIT_NODE_DATA:
            MVM_jit_emit_data(tc, &cl, &node->u.data);
            break;
        case MVM_JIT_NODE_SAVE_RV:
            MVM_jit_emit_save_rv(tc, &cl);
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
        tc->instance->jit_enabled = 0;
        return NULL;
    }

    MVM_jit_log(tc, "Bytecode size: %"MVM_PRSz"\n", codesize);
    /* Create code segment */
    code = MVM_malloc(sizeof(MVMJitCode));
    code->func_ptr   = (MVMJitFunc)memory;
    code->size       = codesize;
    code->bytecode   = (MVMuint8*)MAGIC_BYTECODE;
    code->sf         = jg->sg->sf;

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
    code->deopts       = COPY_ARRAY(jg->deopts, jg->deopts_num);
    code->num_handlers = jg->handlers_num;
    code->handlers     = COPY_ARRAY(jg->handlers, jg->handlers_alloc);
    code->num_inlines  = jg->inlines_num;
    code->inlines      = COPY_ARRAY(jg->inlines, jg->inlines_alloc);

    /* add sequence number */
    code->seq_nr       = tc->instance->jit_seq_nr++;

    return code;
}

void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code) {
    MVM_platform_free_pages(code->func_ptr, code->size);
    MVM_free(code->deopts);
    MVM_free(code->handlers);
    MVM_free(code->inlines);
    MVM_free(code);
}

/* Enter the JIT code segment. The label is a continuation point where control
 * is resumed after the frame is properly setup. */
void MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                        MVMJitCode *code) {
    void *label = tc->cur_frame->jit_entry_label;
    if (label < (void*)code->func_ptr || (char*)label > (((char*)code->func_ptr) + code->size))
        MVM_oops(tc, "JIT entry label out of range for code!\n"
                 "(label %p, func_ptr %p, code size %lui, offset %li, frame_nr %i, seq nr %i)",
                 label, code->func_ptr, code->size, ((char*)label) - ((char*)code->func_ptr),
                 tc->cur_frame->sequence_nr, code->seq_nr);
    code->func_ptr(tc, cu, label);
}
