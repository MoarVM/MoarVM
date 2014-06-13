#include "moar.h"
#include "platform/mmap.h"
#include <dasm_proto.h>
#include "emit.h"

static void append_ins(MVMJitGraph *jg, MVMJitIns *ins) {
    if (jg->last_ins) {
        jg->last_ins->next = ins;
        jg->last_ins = ins;
    } else {
        jg->first_ins = ins;
        jg->last_ins = ins;
    }
    ins->next = NULL;
}

static void append_primitive(MVMThreadContext *tc, MVMJitGraph *jg,
                             MVMSpeshIns * moar_ins) {
    MVMJitIns * ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    ins->type = MVM_JIT_INS_PRIMITIVE;
    ins->u.prim.ins = moar_ins;
    append_ins(jg, ins);
}


static void append_call_c(MVMThreadContext *tc, MVMJitGraph *jg,
                          void * func_ptr, MVMint16 num_args,
                          MVMJitCallArg *call_args) {
    MVMJitIns * ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    size_t args_size =  num_args * sizeof(MVMJitCallArg);
    ins->type = MVM_JIT_INS_CALL_C;
    ins->u.call.func_ptr = func_ptr;
    ins->u.call.num_args = num_args;
    ins->u.call.has_vargs = 0; // don't support them yet
    /* Call argument array is typically stack allocated,
     * so they need to be copied */
    ins->u.call.args = MVM_spesh_alloc(tc, jg->spesh, args_size);
    memcpy(ins->u.call.args, call_args, args_size);
    append_ins(jg, ins);
}

static void append_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                          MVMint32 destination) {
    MVMJitIns * ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    ins->type = MVM_JIT_INS_BRANCH;
    ins->u.branch.destination = destination;
    append_ins(jg, ins);
}

MVMJitGraph * MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg) {
    MVMSpeshBB  * current_bb = sg->entry;
    MVMSpeshIns * current_ins = current_bb->first_ins;
    MVMJitGraph * jit_graph;
    MVMJitIns   * jit_ins;

    if (!MVM_jit_support()) {
        return NULL;
    }

    if (tc->instance->spesh_log_fh) {
        fprintf(tc->instance->spesh_log_fh, "Can I make a JIT graph?\n");
    }
    /* Can't handle complex graphs yet */
    if (sg->num_bbs > 2) {
        if (tc->instance->spesh_log_fh) {
            fprintf(tc->instance->spesh_log_fh, "Can't make graph jit graph "
                    "because I have %d basic blocks\n", sg->num_bbs);
        }
        return NULL;
    }
    /* special case logic! */
    if (current_ins == current_bb->last_ins &&
        current_ins->info->opcode == MVM_OP_no_op) {
        current_bb = current_bb->linear_next;
        current_ins = current_bb->first_ins;
    }
    jit_graph = MVM_spesh_alloc(tc, sg, sizeof(MVMJitGraph));
    jit_graph->spesh = sg;
    while (current_ins) {
        if (tc->instance->spesh_log_fh) {
            fprintf(tc->instance->spesh_log_fh, "JIT: op-to-graph: <%s>\n", current_ins->info->name);
        }
        switch(current_ins->info->opcode) {
        case MVM_OP_no_op:
            /* srsly */
            break;
        case MVM_OP_add_i:
        case MVM_OP_const_i64:
        case MVM_OP_const_i64_16:
        case MVM_OP_const_s:
            append_primitive(tc, jit_graph, current_ins);
            break;
        case MVM_OP_say: {
            MVMint32 reg = current_ins->operands[0].reg.i;
            MVMJitCallArg args[] = { { MVM_JIT_ARG_STACK, MVM_JIT_STACK_TC},
                                     { MVM_JIT_ARG_REG, reg } };
            //append_call_c(tc, jit_graph, &MVM_string_say,  2, args);
            break;
        }
        case MVM_OP_return_i: {
            MVMint32 reg = current_ins->operands[0].reg.orig;
            MVMJitCallArg args[] = { { MVM_JIT_ARG_STACK, MVM_JIT_STACK_TC },
                                     { MVM_JIT_ARG_REG, reg },
                                     { MVM_JIT_ARG_CONST, 0 } };
            append_call_c(tc, jit_graph, &MVM_args_set_result_int, 3, args);
            append_branch(tc, jit_graph, MVM_JIT_BRANCH_EXIT);
            break;
        }
        default:
            if (tc->instance->spesh_log_fh) {
                fprintf(tc->instance->spesh_log_fh,
                        "Can't make graph due to opcode <%s>\n",
                        current_ins->info->name);
            }
            return NULL;
        }
        current_ins = current_ins->next;
    }
    /* Check if we've added a instruction at all */
    if (jit_graph->first_ins)
        return jit_graph;
    return NULL;
}

static void dump_code(FILE *f, unsigned char * code, size_t codesize) {
    int i;
    fprintf(f, "JIT Bytecode Dump:\n");
    for (i = 0; i < codesize; i++) {
        fprintf(f, "%02X%c", code[i], ((i + 1) % 32) == 0 ? '\n' : ' ');
    }
    fprintf(f, "\n\n");
}

static void dump_binary(char * filename, unsigned char * code, size_t codesize) {
    FILE * f = fopen(filename, "w");
    fwrite(code, sizeof(char), codesize, f);
    fclose(f);
}

MVMJitCode MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *jg,
                                 size_t *codesize_out) {
    dasm_State *state;
    unsigned char * memory;
    size_t codesize;
    /* Space for globals */
    MVMint32  num_globals = MVM_jit_num_globals();
    void ** dasm_globals = malloc(num_globals * sizeof(void*));
    MVMJitIns * ins = jg->first_ins;

    if (tc->instance->spesh_log_fh) {
        fprintf(tc->instance->spesh_log_fh, "JIT compiling code\n");
    }

    /* setup dasm */
    dasm_init(&state, 1);
    dasm_setupglobal(&state, dasm_globals, num_globals);
    dasm_setup(&state, MVM_jit_actions());
    /* For the dynamic labels (not necessary right now) */
    dasm_growpc(&state, jg->num_labels);
    /* generate code */

    MVM_jit_emit_prologue(tc, &state);
    while (ins) {
        if (ins->label) {
            MVM_jit_emit_label(tc, ins->label, &state);
        }
        switch(ins->type) {
        case MVM_JIT_INS_PRIMITIVE:
            MVM_jit_emit_primitive(tc, &ins->u.prim, &state);
            break;
        case MVM_JIT_INS_CALL_C:
            MVM_jit_emit_call_c(tc, &ins->u.call, &state);
            break;
        case MVM_JIT_INS_BRANCH:
            MVM_jit_emit_branch(tc, &ins->u.branch, &state);
            break;
        }
        ins = ins->next;
    }
    MVM_jit_emit_epilogue(tc, &state);

    /* compile the function */
    dasm_link(&state, &codesize);
    memory = MVM_platform_alloc_pages(codesize, MVM_PAGE_READ|MVM_PAGE_WRITE);
    memset(memory, 0, codesize);
    dasm_encode(&state, memory);
    /* protect memory from being overwritten */
    MVM_platform_set_page_mode(memory, codesize, MVM_PAGE_READ|MVM_PAGE_EXEC);
    *codesize_out = codesize;
    /* clear up the assembler */
    dasm_free(&state);
    free(dasm_globals);

    if (tc->instance->spesh_log_fh) {
        fprintf(tc->instance->spesh_log_fh, "JIT compiled code: %d bytes", codesize);
        dump_code(tc->instance->spesh_log_fh, memory, codesize);
    }
    dump_binary("jit-code.bin", memory, codesize);

    return (MVMJitCode)memory;
}

MVMuint8 * MVM_jit_magic_bytecode(MVMThreadContext *tc, MVMuint32 *magic_bytecode_size_out) {
    MVMuint16 magic_bytecode[] = { MVM_OP_sp_jit_enter, 0, MVM_OP_return };
    MVMuint8 * magic_block = malloc(sizeof(magic_bytecode));
    *magic_bytecode_size_out = sizeof(magic_bytecode);
    return memcpy(magic_block, magic_bytecode, sizeof(magic_bytecode));
}

void MVM_jit_enter(MVMThreadContext *tc, MVMFrame *f, MVMJitCode code) {
    code(tc, f, NULL);
}
