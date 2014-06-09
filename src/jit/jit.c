#include "moar.h"
#include "platform/mmap.h"
#include <dasm_proto.h>
#include "emit.h"



MVMJitGraph * MVM_jit_try_make_jit_graph(MVMThreadContext *tc, MVMSpeshGraph *spesh) {
    /* right now i'm planning to only jit a few very simple things. */
    MVMSpeshBB * current_bb;
    MVMSpeshIns * current_ins;
    MVMJitGraph * jit_graph;
    MVMJitNode * current_node;
    if (spesh->num_bbs > 1) {
	return NULL;
    }
    /* set up the graph */
    jit_graph = MVM_spesh_alloc(tc, spesh, sizeof(MVMJitGraph));
    current_node = MVM_spesh_alloc(tc, spesh, sizeof(MVMJitNode));
    
    jit_graph->spesh = spesh;
    jit_graph->entry = current_node;
    current_bb = spesh->entry;
    current_ins = current_bb->first_ins;

    while (current_ins) {
	current_ins = current_ins->next;
    }
    /* finish the graph */
    current_node->next = MVM_spesh_alloc(tc, spesh, sizeof(MVMJitNode));
    current_node = current_node->next;

    jit_graph->exit = current_node;
    return jit_graph;
}


MVMJitCode MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph) {
    
    dasm_State *state;
    char * memory;
    size_t codesize;
    
    dasm_init(&state, 1);
    dasm_setup(&state, MVM_jit_actions());

    /* here we generate code */
    MVM_jit_emit_prologue(tc, &state);
    /* tumbleweed */
    MVM_jit_emit_epilogue(tc, &state);
    
    dasm_link(&state, &codesize);
    memory = MVM_platform_alloc_pages(codesize, MVM_PAGE_READ|MVM_PAGE_WRITE);
    dasm_encode(&state, memory);
    MVM_platform_set_page_mode(memory, codesize, MVM_PAGE_READ|MVM_PAGE_EXEC);

    return (MVMJitCode)memory;
}
