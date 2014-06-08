#include "moar.h"
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include <dasm_proto.h>
#include "emit.h"

static struct MVMJitCCall opcode_func[] = {

};

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
    current_node->type = MVM_JIT_NODE_ENTRY;
    
    jit_graph->spesh = spesh;
    jit_graph->entry = current_node;
    current_bb = spesh->entry;
    current_ins = current_bb->first_ins;
    /* I don't actually think that the current setup makes a lot of sense. */
    while (current_ins) {
	switch(current_ins->info->opcode) {
	case MVM_OP_const_i64: {
	    MVMJitStore *next_node = MVM_spesh_alloc(tc, spesh, sizeof(MVMJitStore));
	    next_node->node.type = MVM_JIT_NODE_STORE;
	    next_node->value.u.i64 = current_ins->operands[1].lit_i64;
	    break;
	}
	case MVM_OP_const_n64: {
	    break;
	}
	case MVM_OP_add_i: {
	    break;
	}
	default: /* can't compile this opcode yet */
	    return NULL;
	}
	current_ins = current_ins->next;
    }
    /* finish the graph */
    current_node->next = MVM_spesh_alloc(tc, spesh, sizeof(MVMJitNode));
    current_node = current_node->next;
    current_node->type = MVM_JIT_NODE_EXIT;
    jit_graph->exit = current_node;
    return jit_graph;
}


MVMJitCode * MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph) {
    return NULL;
}
