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

static void append_rvh(MVMThreadContext *tc, MVMJitGraph *jg,
                       MVMJitRVMode mode, MVMJitAddrBase base,
                       MVMint32 idx) {
    MVMJitIns * ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    ins->type = MVM_JIT_INS_RVH;
    ins->u.rvh.mode = mode;
    ins->u.rvh.addr.base = base;
    ins->u.rvh.addr.idx = idx;
    append_ins(jg, ins);
}

/* Try to assign a label name for a basic block */
static MVMint32 get_label_name(MVMThreadContext *tc, MVMJitGraph *jg,
                               MVMSpeshBB *bb) {
    int i = 0;
    for (i = 0; i < jg->num_labels; i++) {
        if (jg->labels[i].bb == bb) {
            return i;
        } else if (jg->labels[i].bb == NULL) {
            jg->labels[i].bb = bb;
            return i;
        }
    }
    MVM_exception_throw_adhoc(tc, "JIT: Cannot assign %d labels", i);
}

static void append_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                          MVMint32 name, MVMSpeshIns *sp_ins) {

    MVMJitIns * ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    ins->type = MVM_JIT_INS_BRANCH;
    if (sp_ins == NULL) {
        ins->u.branch.ins = NULL;
        ins->u.branch.dest.bb = NULL;
        ins->u.branch.dest.name = name;
    }
    else {
        ins->u.branch.ins = sp_ins;
        if (sp_ins->info->opcode == MVM_OP_goto) {
            ins->u.branch.dest.bb = sp_ins->operands[0].ins_bb;
        }
        else {
            ins->u.branch.dest.bb = sp_ins->operands[1].ins_bb;
        }
        ins->u.branch.dest.name = get_label_name(tc, jg, ins->u.branch.dest.bb);
    }
    append_ins(jg, ins);
}

static void append_label(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMSpeshBB *bb) {

    MVMJitIns *ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    ins->type = MVM_JIT_INS_LABEL;
    ins->u.label.bb = bb;
    ins->u.label.name = get_label_name(tc, jg, bb);
    append_ins(jg, ins);
    MVM_jit_log(tc, "append label: %d\n", ins->u.label.name);
}

static MVMint32 append_op(MVMThreadContext *tc, MVMJitGraph *jg,
                           MVMSpeshIns *ins) {
    MVM_jit_log(tc, "append_ins: <%s>\n", ins->info->name);
    switch(ins->info->opcode) {
    case MVM_SSA_PHI:
    case MVM_OP_no_op:
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
    case MVM_OP_sp_getarg_o:
    case MVM_OP_sp_getarg_n:
    case MVM_OP_sp_getarg_s:
    case MVM_OP_set:
    case MVM_OP_const_s:
        append_primitive(tc, jg, ins);
        break;
    case MVM_OP_goto:
    case MVM_OP_if_i:
    case MVM_OP_unless_i:
        append_branch(tc, jg, 0, ins);
        break;
    case MVM_OP_say: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC},
                              { MVM_JIT_ADDR_REG, reg } };
        append_call_c(tc, jg, &MVM_string_say,  2, args);
        break;
    }
    case MVM_OP_print: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC},
                              { MVM_JIT_ADDR_REG, reg } };
        append_call_c(tc, jg, &MVM_string_print,  2, args);
        break;
    }
    case MVM_OP_return: {
        MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC},
                              { MVM_JIT_ADDR_LITERAL, 0 }};
        append_call_c(tc, jg, &MVM_args_assert_void_return_ok,
                      2, args);
        append_branch(tc, jg, MVM_JIT_BRANCH_EXIT, NULL);
        break;
    }
    case MVM_OP_return_s:
    case MVM_OP_return_i: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC },
                              { MVM_JIT_ADDR_REG, reg },
                              { MVM_JIT_ADDR_LITERAL, 0 } };
        void * func_ptr;
        switch(ins->info->opcode) {
        case MVM_OP_return_s: func_ptr = &MVM_args_set_result_str; break;
        case MVM_OP_return_i: func_ptr = &MVM_args_set_result_int; break;
        }
        append_call_c(tc, jg, func_ptr, 3, args);
        append_branch(tc, jg, MVM_JIT_BRANCH_EXIT, NULL);
        break;
    }
    case MVM_OP_coerce_sn:
    case MVM_OP_coerce_ns:
    case MVM_OP_coerce_si:
    case MVM_OP_coerce_is: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitAddr args[] = {{ MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC},
                             { MVM_JIT_ADDR_REG, src } };
        void * func_ptr;
        switch (ins->info->opcode) {
        case MVM_OP_coerce_sn: func_ptr = &MVM_coerce_s_n; break;
        case MVM_OP_coerce_ns: func_ptr = &MVM_coerce_n_s; break;
        case MVM_OP_coerce_si: func_ptr = &MVM_coerce_s_i; break;
        case MVM_OP_coerce_is: func_ptr = &MVM_coerce_i_s; break;
        default: MVM_exception_throw_adhoc(tc, "Whut");
        }
        append_call_c(tc, jg, func_ptr, 2, args);
        append_rvh(tc, jg, MVM_JIT_RV_VAL_TO_REG, MVM_JIT_ADDR_REG, dst);
        break;
    }
    default:
        MVM_jit_log(tc, "Don't know how to make a graph of opcode <%s>\n",
                    ins->info->name);
        return 0;
    }
    return 1;
}

static MVMint32 append_bb(MVMThreadContext *tc, MVMJitGraph *jg,
                          MVMSpeshBB *bb) {
    append_label(tc, jg, bb);
    MVMSpeshIns *cur_ins = bb->first_ins;
    while (cur_ins) {
        if(!append_op(tc, jg, cur_ins))
            return 0;
        if (cur_ins == bb->last_ins)
            break;
        cur_ins = cur_ins->next;
    }
    return 1;
}

MVMJitGraph * MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg) {
    MVMSpeshBB *cur_bb;
    MVMJitGraph * jg;
    int i;
    if (!MVM_jit_support()) {
        return NULL;
    }

    MVM_jit_log(tc, "Constructing JIT graph\n");

    jg = MVM_spesh_alloc(tc, sg, sizeof(MVMJitGraph));
    jg->spesh = sg;
    jg->num_labels = sg->num_bbs;
    jg->labels = MVM_spesh_alloc(tc, sg, sizeof(MVMJitLabel) * jg->num_labels);

    /* ignore first (nop) BB, don't need it */
    cur_bb = sg->entry->linear_next;
    /* loop over basic blocks, adding one after the other */
    while (cur_bb) {
        if (!append_bb(tc, jg, cur_bb))
            return NULL;
        cur_bb = cur_bb->linear_next;
    }

    /* Check if we've added a instruction at all */
    if (jg->first_ins)
        return jg;
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
        dump_bytecode(tc, memory, codesize);
    }
    if (tc->instance->jit_log_fh)
        fflush(tc->instance->jit_log_fh);
    return (MVMJitCode)memory;
}


/* inline this? maybe */
void MVM_jit_log(MVMThreadContext *tc, const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (tc->instance->jit_log_fh) {
        vfprintf(tc->instance->jit_log_fh, fmt, args);
    }
    va_end(args);
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
