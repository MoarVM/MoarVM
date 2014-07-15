#include "moar.h"
#include "dasm_proto.h"
#include "platform/mmap.h"
#include "emit.h"

static const MVMuint16 MAGIC_BYTECODE[] = { MVM_OP_sp_jit_enter, 0 };

MVMJitCode * MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *jg) {
    dasm_State *state;
    char * memory;
    size_t codesize;
    /* Space for globals */
    MVMint32  num_globals = MVM_jit_num_globals();
    void ** dasm_globals = malloc(num_globals * sizeof(void*));
    MVMJitNode * node = jg->first_node;
    MVMJitCode * code;
    MVMint32 i;

    MVM_jit_log(tc, "Starting compilation\n");

    /* setup dasm */
    dasm_init(&state, 1);
    dasm_setupglobal(&state, dasm_globals, num_globals);
    dasm_setup(&state, MVM_jit_actions());
    /* For the dynamic labels (not necessary right now) */
    dasm_growpc(&state, jg->num_labels);

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


    MVM_jit_log(tc, "Bytecode size: %d\n", codesize);
    /* Create code segment */
    code = malloc(sizeof(MVMJitCode));
    code->func_ptr   = (MVMJitFunc)memory;
    code->size       = codesize;
    code->sf         = jg->sg->sf;
    code->num_locals = jg->sg->num_locals;
    code->bytecode   = (MVMuint8*)MAGIC_BYTECODE;

    /* setup labels */
    code->num_labels = jg->num_labels;
    code->labels = malloc(sizeof(void*) * code->num_labels);
    for (i = 0; i < code->num_labels; i++) {
        code->labels[i] = memory + dasm_getpclabel(&state, i);
    }

    /* clear up the assembler */
    dasm_free(&state);
    free(dasm_globals);

    if (tc->instance->jit_bytecode_dir) {
        MVM_jit_log_bytecode(tc, code);
    }
    if (tc->instance->jit_log_fh)
        fflush(tc->instance->jit_log_fh);
    return code;
}

void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code) {
    MVM_platform_free_pages(code->func_ptr, code->size);
    free(code);
}

/* Returns 1 if we should return from the frame, the function, 0 otherwise */
MVMint32 MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                            MVMJitCode *code) {
    /* The actual JIT code returns 0 if it went through to the exit */
    void *label = tc->cur_frame->jit_entry_label;
    MVMint32 ctrl = code->func_ptr(tc, cu, label);
    return ctrl ? 0 : 1;
}
