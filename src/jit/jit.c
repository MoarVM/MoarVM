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
                          MVMJitAddr *call_args) {
    MVMJitIns * ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    size_t args_size =  num_args * sizeof(MVMJitAddr);
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

/* inline this? maybe */
void MVM_jit_log(MVMThreadContext *tc, const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (tc->instance->jit_log_fh) {
        vfprintf(tc->instance->jit_log_fh, fmt, args);
    }
}

MVMJitGraph * MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg) {
    MVMSpeshBB  * current_bb = sg->entry;
    MVMSpeshIns * current_ins = current_bb->first_ins;
    MVMJitGraph * jit_graph;
    MVMJitIns   * jit_ins;

    if (!MVM_jit_support()) {
        return NULL;
    }

    MVM_jit_log(tc, "Constructing JIT graph\n");
     /* Can't handle complex graphs yet */
    if (sg->num_bbs > 2) {
        MVM_jit_log(tc, "Can't make JIT graph because "
                    "spesh graph has %d basic blocks", sg->num_bbs);
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
        MVM_jit_log(tc, "op-to-graph: <%s>", current_ins->info->name);
        switch(current_ins->info->opcode) {
        case MVM_OP_no_op:
            /* srsly */
            break;
        case MVM_OP_add_i:
        case MVM_OP_sub_i:
        case MVM_OP_inc_i:
        case MVM_OP_dec_i:
        case MVM_OP_eq_i:
        case MVM_OP_ne_i:
        case MVM_OP_lt_i:
        case MVM_OP_le_i:
        case MVM_OP_gt_i:
        case MVM_OP_ge_i:
        case MVM_OP_const_i64:
        case MVM_OP_const_i64_16:
        case MVM_OP_sp_getarg_i:
        case MVM_OP_set:
        case MVM_OP_const_s:
            append_primitive(tc, jit_graph, current_ins);
            break;
        case MVM_OP_say: {
            MVMint32 reg = current_ins->operands[0].reg.orig;
            MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC},
                                  { MVM_JIT_ADDR_REG, reg } };
            append_call_c(tc, jit_graph, &MVM_string_say,  2, args);
            break;
        }
        case MVM_OP_return: {
            MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC},
                                  { MVM_JIT_ADDR_LITERAL, 0 }};
            append_call_c(tc, jit_graph, &MVM_args_assert_void_return_ok,
                          2, args);
            break;
        }
        case MVM_OP_return_i: {
            MVMint32 reg = current_ins->operands[0].reg.orig;
            MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC },
                                  { MVM_JIT_ADDR_REG, reg },
                                  { MVM_JIT_ADDR_LITERAL, 0 } };
            append_call_c(tc, jit_graph, &MVM_args_set_result_int, 3, args);
            append_branch(tc, jit_graph, MVM_JIT_BRANCH_EXIT);
            break;
        }
        default:
            MVM_jit_log(tc, "Don't know how to make a graph of opcode <%s>\n",
                        current_ins->info->name);
            return NULL;
        }
        current_ins = current_ins->next;
    }
    /* Check if we've added a instruction at all */
    if (jit_graph->first_ins)
        return jit_graph;
    return NULL;
}


static void dump_bytecode(MVMThreadContext *tc, void * code, size_t codesize) {
    size_t dirname_length = strlen(tc->instance->jit_bytecode_dir);
    char * filename = malloc(dirname_length + strlen("/jit-code.bin") + 1);
    strcpy(filename, tc->instance->jit_bytecode_dir);
    strcpy(filename + dirname_length, "/jit-code.bin");
    FILE * f = fopen(filename, "w");
    fwrite(code, sizeof(char), codesize, f);
    fclose(f);
}

MVMJitCode MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *jg,
                                 size_t *codesize_out) {
    dasm_State *state;
    void * memory;
    size_t codesize;
    /* Space for globals */
    MVMint32  num_globals = MVM_jit_num_globals();
    void ** dasm_globals = malloc(num_globals * sizeof(void*));
    MVMJitIns * ins = jg->first_ins;

    MVM_jit_log(tc, "Starting compilation");

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
        case MVM_JIT_INS_PRIMITIVE:
            MVM_jit_emit_primitive(tc, jg, &ins->u.prim, &state);
            break;
        case MVM_JIT_INS_CALL_C:
            MVM_jit_emit_call_c(tc, jg, &ins->u.call, &state);
            break;
        case MVM_JIT_INS_BRANCH:
            MVM_jit_emit_branch(tc, jg, &ins->u.branch, &state);
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

    MVM_jit_log(tc, "Bytecode size: %d", codesize);
    if (tc->instance->jit_bytecode_dir) {
        dump_bytecode(tc, memory, codesize);
    }
    return (MVMJitCode)memory;
}

MVMuint8 * MVM_jit_magic_bytecode(MVMThreadContext *tc,
                                  MVMuint32 *magic_bytecode_size_out) {
    MVMuint16 magic_bytecode[] = { MVM_OP_sp_jit_enter, 0 };
    MVMuint8 * magic_block = malloc(sizeof(magic_bytecode));
    *magic_bytecode_size_out = sizeof(magic_bytecode);
    return memcpy(magic_block, magic_bytecode, sizeof(magic_bytecode));
}

void MVM_jit_enter(MVMThreadContext *tc, MVMFrame *f, MVMJitCode code) {
    /* this should do something intelligent, of course :-) */
    code(tc, f, NULL);
}
