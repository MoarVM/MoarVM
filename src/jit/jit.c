#include "moar.h"
#include "platform/mmap.h"
#include <dasm_proto.h>
#include "emit.h"


MVMJitGraph * MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *spesh) {
    MVMSpeshBB * current_bb = spesh->entry;
    MVMSpeshIns * current_ins = current_bb->first_ins;
    MVMJitGraph * jit_graph;
    if (tc->instance->spesh_log_fh) {
        fprintf(tc->instance->spesh_log_fh, "Attempt to make a JIT tree");
    }

    if (spesh->num_bbs > 1) {
        return NULL;
    }
    while (current_ins) {
        switch(current_ins->info->opcode) {
        case MVM_OP_add_i:
        case MVM_OP_const_i64:
        case MVM_OP_return_i:
            break;
        default:
            return NULL;
        }
        if (current_ins == current_bb->last_ins) // don't continue past the bb
            break;
        current_ins = current_ins->next;
    }
    /* The jit graph is - for now - just a few pointers into the spesh graph */
    jit_graph = MVM_spesh_alloc(tc, spesh, sizeof(MVMJitGraph));
    jit_graph->entry = current_bb->first_ins;
    jit_graph->exit= current_bb->last_ins;
    return jit_graph;
}


MVMJitCode MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph, size_t *codesize_out) {
    dasm_State *state;
    char * memory;
    size_t codesize;
    MVMSpeshIns *ins = graph->entry;
    /* setup dasm */
    dasm_init(&state, 1);
    dasm_setup(&state, MVM_jit_actions());

    /* generate code */
    MVM_jit_emit_prologue(tc, &state);
    while (ins != graph->exit) {
        MVM_jit_emit_instruction(tc, ins, &state);
        ins = ins->next;
    }
    MVM_jit_emit_instruction(tc, ins, &state);
    MVM_jit_emit_epilogue(tc, &state);

    /* compile the function */
    dasm_link(&state, &codesize);
    memory = MVM_platform_alloc_pages(codesize, MVM_PAGE_READ|MVM_PAGE_WRITE);
    dasm_encode(&state, memory);
    /* protect memory from being overwritten */
    MVM_platform_set_page_mode(memory, codesize, MVM_PAGE_READ|MVM_PAGE_EXEC);

    *codesize_out = codesize;
    return (MVMJitCode)memory;
}

MVMuint8 * MVM_jit_magic_bytecode(MVMThreadContext *tc, MVMuint32 *magic_bytecode_size_out) {
    MVMuint16 magic_bytecode[] = { MVM_OP_sp_jit_enter, 0 };
    MVMuint8 * magic_block = malloc(sizeof(magic_bytecode));
    *magic_bytecode_size_out = sizeof(magic_bytecode);
    return memcpy(magic_block, magic_bytecode, sizeof(magic_bytecode));
}
