#include "moar.h"
#include "dasm_proto.h"
#include "platform/mmap.h"
#include "emit.h"


MVMJitCode MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *jg,
                                 size_t *codesize_out) {
    dasm_State *state;
    void * memory;
    size_t codesize;
    /* Space for globals */
    MVMint32  num_globals = MVM_jit_num_globals();
    void ** dasm_globals = malloc(num_globals * sizeof(void*));
    MVMJitIns * ins = jg->first_ins;

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
        case MVM_JIT_INS_RVH:
            MVM_jit_emit_rvh(tc, jg, &ins->u.rvh, &state);
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
    *codesize_out = codesize;
    /* clear up the assembler */
    dasm_free(&state);
    free(dasm_globals);

    MVM_jit_log(tc, "Bytecode size: %d\n", codesize);
    if (tc->instance->jit_bytecode_dir) {
        MVM_jit_log_bytecode(tc, memory, codesize);
    }
    if (tc->instance->jit_log_fh)
        fflush(tc->instance->jit_log_fh);
    return (MVMJitCode)memory;
}


MVMuint8 * MVM_jit_magic_bytecode(MVMThreadContext *tc) {
    MVMuint16 magic_bytecode[] = { MVM_OP_sp_jit_enter, 0 };
    MVMuint8 *mbc = malloc(sizeof(magic_bytecode));
    return memcpy(mbc, magic_bytecode, sizeof(magic_bytecode));
}

void MVM_jit_enter(MVMThreadContext *tc, MVMFrame *f, MVMJitCode code) {
    /* this should do something intelligent, of course :-) */
    code(tc, f, NULL);
}
