#include "moar.h"
#include "dasm_proto.h"
#include "platform/mmap.h"
#include "emit.h"

static const MVMuint16 MAGIC_BYTECODE[] = { MVM_OP_sp_jit_enter, 0 };

MVMJitCode * MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *jg) {
    dasm_State *state;
    void * memory;
    size_t codesize;
    /* Space for globals */
    MVMint32  num_globals = MVM_jit_num_globals();
    void ** dasm_globals = malloc(num_globals * sizeof(void*));
    MVMJitIns * ins = jg->first_ins;
    MVMJitCode * code;

    MVM_jit_log(tc, "Starting compilation\n");

    /* setup dasm */
    dasm_init(&state, 1);
    dasm_setupglobal(&state, dasm_globals, num_globals);
    dasm_setup(&state, MVM_jit_actions());
    /* For the dynamic labels (not necessary right now) */
    dasm_growpc(&state, jg->num_labels);
    /* generate code */

    MVM_jit_emit_prologue(tc, jg,  &state);
    while (ins) {
        switch(ins->type) {
        case MVM_JIT_INS_LABEL:
            MVM_jit_emit_label(tc, jg, &ins->u.label, &state);
            break;
        case MVM_JIT_INS_PRIMITIVE:
            MVM_jit_emit_primitive(tc, jg, &ins->u.prim, &state);
            break;
        case MVM_JIT_INS_BRANCH:
            MVM_jit_emit_branch(tc, jg, &ins->u.branch, &state);
            break;
        case MVM_JIT_INS_CALL_C:
            MVM_jit_emit_call_c(tc, jg, &ins->u.call, &state);
            break;
        case MVM_JIT_INS_GUARD:
            MVM_jit_emit_guard(tc, jg, &ins->u.guard, &state);
            break;
        }
        ins = ins->next;
    }
    MVM_jit_emit_epilogue(tc, jg, &state);

    /* compile the function */
    dasm_link(&state, &codesize);
    memory = MVM_platform_alloc_pages(codesize, MVM_PAGE_READ|MVM_PAGE_WRITE);
    dasm_encode(&state, memory);
    /* protect memory from being overwritten */
    MVM_platform_set_page_mode(memory, codesize, MVM_PAGE_READ|MVM_PAGE_EXEC);
    /* clear up the assembler */
    dasm_free(&state);
    free(dasm_globals);

    MVM_jit_log(tc, "Bytecode size: %d\n", codesize);
    /* Create code segment */
    code = malloc(sizeof(MVMJitCode));
    code->func_ptr   = (MVMJitFunc)memory;
    code->size       = codesize;
    code->sf         = jg->spesh->sf;
    code->num_locals = jg->spesh->num_locals;
    code->bytecode   = (MVMuint8*)MAGIC_BYTECODE;

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
    MVMint32 ctrl = code->func_ptr(tc, cu, NULL);
    return ctrl ? 0 : 1;
}
