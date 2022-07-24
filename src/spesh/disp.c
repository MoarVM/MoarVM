#include "moar.h"

#define GET_UI16(pc, idx)   *((MVMuint16 *)((pc) + (idx)))

MVM_STATIC_INLINE MVMuint64 GET_UI64(const MVMuint8 *pc, MVMint32 idx) {
    MVMuint64 retval;
    memcpy(&retval, pc + idx, sizeof(retval));
    return retval;
}

/* Create op info for a dispatch instruction, so that during specialization we
 * can pretend it's not varargs. */
size_t MVM_spesh_disp_dispatch_op_info_size(MVMThreadContext *tc,
        const MVMOpInfo *base_info, MVMCallsite *callsite) {
    /* In general, ops support up to an operand limit; in the case that there are more,
     * we'd overrun the buffer. We thus allocate more. */
    MVMuint32 total_ops = base_info->num_operands + callsite->flag_count;
    return sizeof(MVMOpInfo) + (total_ops > MVM_MAX_OPERANDS
            ? total_ops - MVM_MAX_OPERANDS
            : 0) * sizeof(MVMuint8);
}
void MVM_spesh_disp_initialize_dispatch_op_info(MVMThreadContext *tc,
        const MVMOpInfo *base_info, MVMCallsite *callsite, MVMOpInfo *dispatch_info) {

    /* Populate based on the original operation. */
    memcpy(dispatch_info, base_info, sizeof(MVMOpInfo));

    /* Tweak the operand count and set up new operand info based on the callsite. */
    dispatch_info->num_operands += callsite->flag_count;
    MVMuint16 operand_index = base_info->num_operands;
    MVMuint16 flag_index;
    for (flag_index = 0; flag_index < callsite->flag_count; operand_index++, flag_index++) {
        MVMCallsiteFlags flag = callsite->arg_flags[flag_index];
        if (flag & MVM_CALLSITE_ARG_OBJ) {
            dispatch_info->operands[operand_index] = MVM_operand_obj;
        }
        else if (flag & MVM_CALLSITE_ARG_INT) {
            dispatch_info->operands[operand_index] = MVM_operand_int64;
        }
        else if (flag & MVM_CALLSITE_ARG_UINT) {
            dispatch_info->operands[operand_index] = MVM_operand_uint64;
        }
        else if (flag & MVM_CALLSITE_ARG_NUM) {
            dispatch_info->operands[operand_index] = MVM_operand_num64;
        }
        else if (flag & MVM_CALLSITE_ARG_STR) {
            dispatch_info->operands[operand_index] = MVM_operand_str;
        }
        dispatch_info->operands[operand_index] |= MVM_operand_read_reg;
    }
}

/* Returns the callsite argument offset for an opcode. */
MVMCallsite * MVM_spesh_disp_callsite_for_dispatch_op(MVMuint16 opcode, MVMuint8 *args,
        MVMCompUnit *cu) {
    switch (opcode) {
        case MVM_OP_dispatch_v:
        case MVM_OP_sp_dispatch_v:
            return cu->body.callsites[GET_UI16(args, 4)];
        case MVM_OP_dispatch_i:
        case MVM_OP_dispatch_u:
        case MVM_OP_dispatch_n:
        case MVM_OP_dispatch_s:
        case MVM_OP_dispatch_o:
        case MVM_OP_sp_dispatch_i:
        case MVM_OP_sp_dispatch_u:
        case MVM_OP_sp_dispatch_n:
        case MVM_OP_sp_dispatch_s:
        case MVM_OP_sp_dispatch_o:
            return cu->body.callsites[GET_UI16(args, 6)];
        case MVM_OP_sp_runbytecode_v:
        case MVM_OP_sp_runcfunc_v:
        case MVM_OP_sp_runnativecall_v:
            return (MVMCallsite *)GET_UI64(args, 2);
        case MVM_OP_sp_runbytecode_i:
        case MVM_OP_sp_runbytecode_u:
        case MVM_OP_sp_runbytecode_n:
        case MVM_OP_sp_runbytecode_s:
        case MVM_OP_sp_runbytecode_o:
        case MVM_OP_sp_runcfunc_i:
        case MVM_OP_sp_runcfunc_u:
        case MVM_OP_sp_runcfunc_n:
        case MVM_OP_sp_runcfunc_s:
        case MVM_OP_sp_runcfunc_o:
        case MVM_OP_sp_runnativecall_i:
        case MVM_OP_sp_runnativecall_u:
        case MVM_OP_sp_runnativecall_n:
        case MVM_OP_sp_runnativecall_s:
        case MVM_OP_sp_runnativecall_o:
            return (MVMCallsite *)GET_UI64(args, 4);
        default:
            MVM_panic(1, "Unknown dispatch op when resolving callsite");
    }
}

/* Hit count of an outcome, used for analizing how to optimize the dispatch. */
typedef struct {
    MVMuint32 outcome;
    MVMuint32 hits;
} OutcomeHitCount;

/* Rewrite a dispatch instruction into an sp_dispatch one, resolving only the
 * static frame and inline cache offset. */
static void rewrite_to_sp_dispatch(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
        MVMuint32 bytecode_offset) {
    /* Resolve the callsite. */
    MVMuint32 callsite_idx = ins->operands[ins->info->opcode == MVM_OP_dispatch_v ? 1 : 2].callsite_idx;
    MVMCallsite *callsite = g->sf->body.cu->body.callsites[callsite_idx];

    /* Pick the new dispatch instruction and create instruction info for it. */
    const MVMOpInfo *new_ins_base_info;
    switch (ins->info->opcode) {
        case MVM_OP_dispatch_v: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_v); break;
        case MVM_OP_dispatch_i: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_i); break;
        case MVM_OP_dispatch_u: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_u); break;
        case MVM_OP_dispatch_n: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_n); break;
        case MVM_OP_dispatch_o: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_o); break;
        case MVM_OP_dispatch_s: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_s); break;
        default:
            MVM_oops(tc, "Unexpected dispatch instruction to rewrite");
    }
    MVMOpInfo *new_ins_info = MVM_spesh_alloc(tc, g, MVM_spesh_disp_dispatch_op_info_size(
        tc, new_ins_base_info, callsite));
    MVM_spesh_disp_initialize_dispatch_op_info(tc, new_ins_base_info, callsite, new_ins_info);
    ins->info = new_ins_info;

    /* Rewrite the operands. */
    MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g,
            new_ins_info->num_operands * sizeof(MVMSpeshOperand));
    MVMuint32 target = 0;
    MVMuint32 source = 0;
    if (new_ins_info->opcode != MVM_OP_sp_dispatch_v)
        new_operands[target++] = ins->operands[source++]; /* Result value */
    new_operands[target++] = ins->operands[source++]; /* Dispatcher name */
    new_operands[target++] = ins->operands[source++]; /* Callsite index */
    new_operands[target++].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
            (MVMCollectable *)g->sf);
    new_operands[target++].lit_ui32 = MVM_disp_inline_cache_get_slot(tc, g->sf, bytecode_offset);
    memcpy(new_operands + target, ins->operands + source,
            callsite->flag_count * sizeof(MVMSpeshOperand));
    ins->operands = new_operands;
}

/* Get the index of the first normal argument operand to the dispatch
 * instruction (after result unless void, dispatcher name, and callsite). */
static MVMuint32 find_disp_op_first_real_arg(MVMThreadContext *tc, MVMSpeshIns *ins) {
    if (ins->info->opcode == MVM_OP_dispatch_v)
        return 2;
    assert(ins->info->opcode == MVM_OP_dispatch_i ||
            ins->info->opcode == MVM_OP_dispatch_u ||
            ins->info->opcode == MVM_OP_dispatch_n ||
            ins->info->opcode == MVM_OP_dispatch_s ||
            ins->info->opcode == MVM_OP_dispatch_o);
    return 3;
}

/* Find an annotation on a dispatch instruction and remove it. */
static MVMSpeshAnn * take_dispatch_annotation(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshIns *ins, MVMint32 kind) {
    MVMSpeshAnn *deopt_ann = ins->annotations;
    MVMSpeshAnn *prev = NULL;
    while (deopt_ann) {
        if (deopt_ann->type == kind) {
            if (prev)
                prev->next = deopt_ann->next;
            else
                ins->annotations = deopt_ann->next;
            deopt_ann->next = NULL;
            return deopt_ann;
        }
        prev = deopt_ann;
        deopt_ann = deopt_ann->next;
    }
    MVM_panic(1, "Spesh: unexpectedly missing annotation on dispatch op");
}

/* Creates an annotation relating a synthetic (added during optimization) deopt
 * point back to the original one whose usages will have been recorded in the
 * facts. */
static MVMSpeshAnn * create_synthetic_deopt_annotation(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshAnn *in) {
    MVMSpeshAnn *ann = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
    ann->type = MVM_SPESH_ANN_DEOPT_SYNTH;
    ann->data.deopt_idx = in->data.deopt_idx;
    return ann;
}

/* Takes an instruction that may deopt and an operand that should contain the
 * deopt index. Reuse the dispatch instruction deopt annotation if that did
 * not happen already, otherwise clone it. */
static void set_deopt(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
        MVMSpeshOperand *index_operand, MVMSpeshAnn *deopt_ann,
        MVMuint32 *reused_deopt_ann) {
    if (*reused_deopt_ann) {
        /* Already reused, clone needed. */
        MVMuint32 new_deopt_index = MVM_spesh_graph_add_deopt_annotation(tc, g,
            ins, g->deopt_addrs[deopt_ann->data.deopt_idx * 2], deopt_ann->type);
        index_operand->lit_ui32 = new_deopt_index;
        deopt_ann = create_synthetic_deopt_annotation(tc, g, deopt_ann);
    }
    else {
        /* First usage. */
        *reused_deopt_ann = 1;
        index_operand->lit_ui32 = deopt_ann->data.deopt_idx;
    }
    deopt_ann->next = ins->annotations;
    ins->annotations = deopt_ann;
}

/* Emit a type, concreteness or object literal guard instruction (when concreteness
 * only, comparee is null). */
static MVMSpeshOperand emit_guard(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMuint16 op,
        MVMSpeshOperand guard_reg, MVMCollectable *comparee, MVMSpeshAnn *deopt_ann,
        MVMuint32 *reused_deopt_ann) {
    /* Produce a new version for after the guarding. */
    MVMSpeshOperand guarded_reg = MVM_spesh_manipulate_split_version(tc, g,
            guard_reg, bb, *insert_after ? (*insert_after)->next : bb->first_ins);

    /* Produce the instruction and insert it. */
    MVMSpeshIns *guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(op);
    guard->operands = MVM_spesh_alloc(tc, g, (comparee ? 4 : 3) * sizeof(MVMSpeshOperand));
    guard->operands[0] = guarded_reg;
    guard->operands[1] = guard_reg;
    if (comparee) {
        guard->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                comparee);
        set_deopt(tc, g, guard, &(guard->operands[3]), deopt_ann, reused_deopt_ann);
    }
    else {
        set_deopt(tc, g, guard, &(guard->operands[2]), deopt_ann, reused_deopt_ann);
    }
    MVM_spesh_manipulate_insert_ins(tc, bb, *insert_after, guard);
    *insert_after = guard;

    /* Tweak usages. */
    MVM_spesh_get_facts(tc, g, guarded_reg)->writer = guard;
    MVM_spesh_usages_add_by_reg(tc, g, guard_reg, guard);

    /* Add facts to and return the guarded register. */
    MVM_spesh_facts_guard_facts(tc, g, bb, guard);
    return guarded_reg;
}

/* Emit a HLL guard instruction. */
static MVMSpeshOperand emit_hll_guard(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMSpeshOperand guard_reg,
        MVMHLLConfig *hll, MVMSpeshAnn *deopt_ann, MVMuint32 *reused_deopt_ann) {
    /* Produce a new version for after the guarding. */
    MVMSpeshOperand guarded_reg = MVM_spesh_manipulate_split_version(tc, g,
            guard_reg, bb, (*insert_after)->next);

    /* Produce the instruction and insert it. */
    MVMSpeshIns *guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(MVM_OP_sp_guardhll);
    guard->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
    guard->operands[0] = guarded_reg;
    guard->operands[1] = guard_reg;
    guard->operands[2].lit_ui64 = (MVMuint64)hll;
    set_deopt(tc, g, guard, &(guard->operands[3]), deopt_ann, reused_deopt_ann);
    MVM_spesh_manipulate_insert_ins(tc, bb, *insert_after, guard);
    *insert_after = guard;

    /* Tweak usages. */
    MVM_spesh_get_facts(tc, g, guarded_reg)->writer = guard;
    MVM_spesh_usages_add_by_reg(tc, g, guard_reg, guard);

    /* Add facts to and return the guarded register. */
    MVM_spesh_facts_guard_facts(tc, g, bb, guard);
    return guarded_reg;
}

/* Emit a simple binary instruction with a result register and either a
 * input register or input constant. */
static void emit_bi_op(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMuint16 op,
        MVMSpeshOperand to_reg, MVMSpeshOperand from_reg) {
    /* Produce the instruction and insert it. */
    MVMSpeshIns *ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    ins->info = MVM_op_get_op(op);
    ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * 2);
    ins->operands[0] = to_reg;
    ins->operands[1] = from_reg;
    MVM_spesh_manipulate_insert_ins(tc, bb, *insert_after, ins);
    *insert_after = ins;

    /* Tweak usages. */
    MVM_spesh_get_facts(tc, g, to_reg)->writer = ins;
    if ((ins->info->operands[1] & MVM_operand_rw_mask) == MVM_operand_read_reg)
        MVM_spesh_usages_add_by_reg(tc, g, from_reg, ins);
}

/* Emit a simple three-ary instruction where the first register is written
 * to and the other two operands are registers that are read from. */
static void emit_tri_op(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMuint16 op,
        MVMSpeshOperand to_reg, MVMSpeshOperand from_reg, MVMSpeshOperand third_reg) {
    /* Produce the instruction and insert it. */
    MVMSpeshIns *ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    ins->info = MVM_op_get_op(op);
    ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * 3);
    ins->operands[0] = to_reg;
    ins->operands[1] = from_reg;
    ins->operands[2] = third_reg;
    MVM_spesh_manipulate_insert_ins(tc, bb, *insert_after, ins);
    *insert_after = ins;

    /* Tweak usages. */
    MVM_spesh_get_facts(tc, g, to_reg)->writer = ins;
    MVM_spesh_usages_add_by_reg(tc, g, from_reg, ins);
    MVM_spesh_usages_add_by_reg(tc, g, third_reg, ins);
}

static void emit_iffy_op(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMuint16 op,
        MVMSpeshOperand condition_reg, MVMSpeshBB *target) {
    /* Produce the instruction and insert it. */
    MVMSpeshIns *ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    ins->info = MVM_op_get_op(op);
    ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * 2);
    ins->operands[0] = condition_reg;
    ins->operands[1].ins_bb = target;
    MVM_spesh_manipulate_insert_ins(tc, bb, *insert_after, ins);
    *insert_after = ins;

    /* Tweak usages. */
    if ((ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_read_reg)
        MVM_spesh_usages_add_by_reg(tc, g, condition_reg, ins);
}

/* Emit an instruction to load a value into a spesh slot. */
static void emit_load_spesh_slot(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMSpeshOperand to_reg,
        MVMCollectable *value) {
    /* Add the instruction. */
    MVMSpeshIns *ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
    ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * 2);
    ins->operands[0] = to_reg;
    ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g, value);
    MVM_spesh_manipulate_insert_ins(tc, bb, *insert_after, ins);
    *insert_after = ins;

    /* Set facts. */
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, to_reg);
    facts->writer = ins;
    facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE | MVM_SPESH_FACT_KNOWN_TYPE |
        (IS_CONCRETE(value) ? MVM_SPESH_FACT_CONCRETE : MVM_SPESH_FACT_TYPEOBJ);
    facts->value.o = (MVMObject *)value;
    facts->type = STABLE(value)->WHAT;
}

/* Emit an instruction to load an attribute. */
static void emit_load_attribute(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMuint16 op,
        MVMSpeshOperand to_reg, MVMSpeshOperand from_reg, MVMuint16 offset) {
    /* Produce instruction. */
    MVMSpeshIns *ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    ins->info = MVM_op_get_op(op);
    ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * 3);
    ins->operands[0] = to_reg;
    ins->operands[1] = from_reg;
    ins->operands[2].lit_i16 = offset;
    MVM_spesh_graph_add_comment(tc, g, ins, "emitted from dispatch program attribute load op");
    MVM_spesh_manipulate_insert_ins(tc, bb, *insert_after, ins);
    *insert_after = ins;

    /* Keep usage in sync. */
    MVM_spesh_get_facts(tc, g, to_reg)->writer = ins;
    MVM_spesh_usages_add_by_reg(tc, g, from_reg, ins);
}

/* Emit a guard that a value is a given literal string. */
static MVMSpeshOperand emit_literal_str_guard(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMSpeshOperand testee,
        MVMString *expected, MVMSpeshAnn *deopt_ann, MVMuint32 *reused_deopt_ann) {
    /* Load the string literal value from a spesh slot. */
    MVMSpeshOperand cmp_str_reg = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_str);
    emit_load_spesh_slot(tc, g, bb, insert_after, cmp_str_reg, (MVMCollectable *)expected);

    /* Emit the comparison op. */
    MVMSpeshOperand op_res_reg = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
    emit_tri_op(tc, g, bb, insert_after, MVM_OP_eq_s, op_res_reg, testee, cmp_str_reg);

    /* Emit the guard. */
    emit_guard(tc, g, bb, insert_after, MVM_OP_sp_guardnonzero, op_res_reg, NULL,
        deopt_ann, reused_deopt_ann);

    /* The temporary registers are immediately free for re-use. */
    MVM_spesh_manipulate_release_temp_reg(tc, g, cmp_str_reg);
    MVM_spesh_manipulate_release_temp_reg(tc, g, op_res_reg);

    return testee;
}

static MVMuint16 resumption_op_non_constant(MVMDispProgram *dp, MVMuint16 res_idx) {
    /* Count up how many non-constant values the resume init capture will
     * involve. */
    MVMDispProgramResumption *dpr = &(dp->resumptions[res_idx]);
    MVMuint16 non_constant;
    if (dpr->init_values) {
        MVMuint16 i;
        non_constant = 0;
        for (i = 0; i < dpr->init_callsite->flag_count; i++) {
            switch (dpr->init_values[i].source) {
                case MVM_DISP_RESUME_INIT_ARG:
                case MVM_DISP_RESUME_INIT_TEMP:
                    non_constant++;
                default:
                    break;
            }
        }
    }
    else {
        /* It's based on the initial argument catpure to the dispatch. */
        non_constant = dpr->init_callsite->flag_count;
    }
    return non_constant;
}

size_t MVM_spesh_disp_resumption_op_info_size(MVMThreadContext *tc,
        MVMDispProgram *dp, MVMuint16 res_idx) {

    MVMuint16 non_constant = resumption_op_non_constant(dp, res_idx);

    /* Form the operand info. */
    const MVMOpInfo *base_info = MVM_op_get_op(MVM_OP_sp_resumption);
    MVMuint16 total_ops = base_info->num_operands + non_constant;
    return sizeof(MVMOpInfo) + (total_ops > MVM_MAX_OPERANDS
            ? total_ops - MVM_MAX_OPERANDS
            : 0);
}

/* Form the instruction info for an sp_resumption given the specified dispatch
 * program resumption. This is a varargs instruction, thus why we need to
 * synthesize the instruction info. */
MVMOpInfo * MVM_spesh_disp_initialize_resumption_op_info(MVMThreadContext *tc,
        MVMDispProgram *dp, MVMuint16 res_idx, MVMOpInfo *res_info) {
    const MVMOpInfo *base_info = MVM_op_get_op(MVM_OP_sp_resumption);
    MVMuint16 non_constant = resumption_op_non_constant(dp, res_idx);
    MVMDispProgramResumption *dpr = &(dp->resumptions[res_idx]);

    memcpy(res_info, base_info, sizeof(MVMOpInfo));
    res_info->num_operands += non_constant;
    MVMuint16 operand_index = base_info->num_operands;
    MVMuint16 i;
    for (i = 0; i < dpr->init_callsite->flag_count; i++) {
        MVMint32 include = dpr->init_values
            ? dpr->init_values[i].source == MVM_DISP_RESUME_INIT_ARG ||
                dpr->init_values[i].source == MVM_DISP_RESUME_INIT_TEMP
            : 1;
        if (include) {
            MVMCallsiteFlags flag = dpr->init_callsite->arg_flags[i];
            if (flag & MVM_CALLSITE_ARG_OBJ)
                res_info->operands[operand_index] = MVM_operand_obj;
            else if (flag & MVM_CALLSITE_ARG_INT)
                res_info->operands[operand_index] = MVM_operand_int64;
            else if (flag & MVM_CALLSITE_ARG_UINT)
                res_info->operands[operand_index] = MVM_operand_uint64;
            else if (flag & MVM_CALLSITE_ARG_NUM)
                res_info->operands[operand_index] = MVM_operand_num64;
            else if (flag & MVM_CALLSITE_ARG_STR)
                res_info->operands[operand_index] = MVM_operand_str;
            res_info->operands[operand_index] |= MVM_operand_read_reg;
            operand_index++;
        }
    }

    return res_info;
}

/* Insert an instruction relating to each dispatch resumption init state that
 * is wanted by the dispatch program. This makes sure we preserve the various
 * registers that are used by the resumption. */
static void insert_resume_inits(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
        MVMSpeshIns **insert_after, MVMDispProgram *dp, MVMSpeshOperand *orig_args,
        MVMSpeshOperand *temporaries, MVMint32 deopt_idx) {
    MVMuint32 i;
    for (i = 0; i < dp->num_resumptions; i++) {
        /* Allocate the instruction. */
        MVMDispProgramResumption *dpr = &(dp->resumptions[i]);
        MVMSpeshIns *ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        MVMOpInfo *res_op_info = MVM_spesh_alloc(tc, g,
                MVM_spesh_disp_resumption_op_info_size(tc, dp, i));
        MVM_spesh_disp_initialize_resumption_op_info(tc, dp, i, res_op_info);
        ins->info = res_op_info;
        ins->operands = MVM_spesh_alloc(tc, g, ins->info->num_operands * sizeof(MVMSpeshOperand));

        /* Get a register to use for the resumption state, should it be
         * required. */
        ins->operands[0] = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
        MVM_spesh_get_facts(tc, g, ins->operands[0])->writer = ins;

        /* Prepare a spesh resume init record holding data about the dispatch
         * resumption and write it into the instruction operands. */
        ins->operands[1].lit_ui16 = MVM_VECTOR_ELEMS(g->resume_inits);
        MVMSpeshResumeInit init_record = {
            .dp = dp,
            .deopt_idx = deopt_idx,
            .res_idx = i,
            .state_register = 0,
            .init_registers = NULL,
        };
        MVM_VECTOR_PUSH(g->resume_inits, init_record);

        /* Add all of the non-constant args. */
        MVMuint16 j;
        MVMuint16 insert_pos = 3;
        for (j = 0; j < dpr->init_callsite->flag_count; j++) {
            MVMSpeshOperand source;
            if (!dpr->init_values) {
                source = orig_args[j];
            }
            else if (dpr->init_values[j].source == MVM_DISP_RESUME_INIT_ARG) {
                source = orig_args[dpr->init_values[j].index];
            }
            else if (dpr->init_values[j].source == MVM_DISP_RESUME_INIT_TEMP) {
                source = temporaries[dpr->init_values[j].index];
            }
            else {
                continue; /* Constant */
            }
            ins->operands[2].lit_ui16++;
            ins->operands[insert_pos++] = source;
            MVM_spesh_usages_add_by_reg(tc, g, source, ins);
        }

        /* Insert the instruction. */
        MVM_spesh_manipulate_insert_ins(tc, bb, *insert_after, ins);
        *insert_after = ins;
    }
}

/* Try to translate a dispatch program into a sequence of ops (which will
 * be subject to later optimization and potentially JIT compilation). */
static int translate_dispatch_program(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns *ins, MVMDispProgram *dp, MVMSpeshIns **next_ins) {
    /* First, validate it is a dispatch program we know how to compile. */
    MVMuint32 i;
    for (i = 0; i < dp->num_ops; i++) {
        switch (dp->ops[i].code) {
            case MVMDispOpcodeGuardArgType:
            case MVMDispOpcodeGuardArgTypeConc:
            case MVMDispOpcodeGuardArgTypeTypeObject:
            case MVMDispOpcodeGuardArgConc:
            case MVMDispOpcodeGuardArgTypeObject:
            case MVMDispOpcodeGuardArgLiteralObj:
            case MVMDispOpcodeGuardArgLiteralStr:
            case MVMDispOpcodeGuardArgNotLiteralObj:
            case MVMDispOpcodeGuardArgHLL:
            case MVMDispOpcodeGuardTempType:
            case MVMDispOpcodeGuardTempTypeConc:
            case MVMDispOpcodeGuardTempTypeTypeObject:
            case MVMDispOpcodeGuardTempConc:
            case MVMDispOpcodeGuardTempTypeObject:
            case MVMDispOpcodeGuardTempLiteralObj:
            case MVMDispOpcodeGuardTempLiteralStr:
            case MVMDispOpcodeGuardTempNotLiteralObj:
            case MVMDispOpcodeGuardTempHLL:
            case MVMDispOpcodeLoadCaptureValue:
            case MVMDispOpcodeLoadConstantObjOrStr:
            case MVMDispOpcodeLoadConstantInt:
            case MVMDispOpcodeLoadConstantNum:
            case MVMDispOpcodeLoadAttributeObj:
            case MVMDispOpcodeLoadAttributeInt:
            case MVMDispOpcodeLoadAttributeNum:
            case MVMDispOpcodeLoadAttributeStr:
            case MVMDispOpcodeUnboxInt:
            case MVMDispOpcodeUnboxNum:
            case MVMDispOpcodeUnboxStr:
            case MVMDispOpcodeLoadHOW:
            case MVMDispOpcodeLookup:
            case MVMDispOpcodeSet:
            case MVMDispOpcodeResultValueObj:
            case MVMDispOpcodeResultValueStr:
            case MVMDispOpcodeResultValueInt:
            case MVMDispOpcodeResultValueNum:
            case MVMDispOpcodeUseArgsTail:
            case MVMDispOpcodeCopyArgsTail:
            case MVMDispOpcodeResultBytecode:
            case MVMDispOpcodeResultCFunction:
            case MVMDispOpcodeResultForeignCode:
                break;
            default:
                MVM_spesh_graph_add_comment(tc, g, ins, "dispatch not compiled: op %s NYI",
                                MVM_disp_opcode_to_name(dp->ops[i].code));
                return 0;
        }
    }

    /* We'll re-use the deopt annotation on the dispatch instruction for
     * the first guard, and then clone it later if needed. */
    MVMSpeshAnn *deopt_ann = take_dispatch_annotation(tc, g, ins,
        MVM_SPESH_ANN_DEOPT_PRE_INS);
    MVMuint32 reused_deopt_ann = 0;

    /* Steal other annotations in order to put them onto any runbytecode
     * instruction we generate. */
    MVMSpeshAnn *deopt_all_ann = take_dispatch_annotation(tc, g, ins,
        MVM_SPESH_ANN_DEOPT_ALL_INS);
    MVMSpeshAnn *cached_ann = take_dispatch_annotation(tc, g, ins,
        MVM_SPESH_ANN_CACHED);

    /* Find the arguments that are the input to the dispatch and copy
     * them, since we'll mutate this list. */
    MVMuint32 first_real_arg = find_disp_op_first_real_arg(tc, ins);
    MVMSpeshOperand *orig_args = &ins->operands[first_real_arg];
    MVMuint32 num_real_args = ins->info->num_operands - first_real_arg;
    size_t args_size = num_real_args * sizeof(MVMSpeshOperand);
    MVMSpeshOperand *args = MVM_spesh_alloc(tc, g, args_size);
    memcpy(args, orig_args, args_size);

    /* Registers holding temporaries, which may be registers that we have
     * allocated, or may refer to the input arguments. */
    MVMSpeshOperand *temporaries = MVM_spesh_alloc(tc, g,
            sizeof(MVMSpeshOperand) * dp->num_temporaries);

    /* Registers that we have allocated, and should release after the
     * emitting of the dispatch program. (Since the temporaries could get
     * reused for something else and another register aliased there, we
     * need to keep track of the list of things to release separately
     * from the temporaries array above.) */
    MVM_VECTOR_DECL(MVMSpeshOperand, allocated_temps);
    MVM_VECTOR_INIT(allocated_temps, 0);

    /* In the case we emit bytecode, we may also need to delay release of
     * the temporaries until after runbytecode optimization. */
    MVMint32 delay_temps_release = 0;

    /* Keep result register alive */
    MVMint32 keep_result_register = ins->info->opcode != MVM_OP_dispatch_v;

    /* Visit the ops of the dispatch program and translate them. */
    MVMSpeshIns *insert_after = ins;
    MVMSpeshIns *orig_next = ins->next;
    MVMCallsite *callsite = NULL;
    MVMint32 skip_args = -1;
    for (i = 0; i < dp->num_ops; i++) {
        MVMDispProgramOp *op = &(dp->ops[i]);
        switch (op->code) {
            case MVMDispOpcodeGuardArgType: {
                MVMCollectable *wanted_st = dp->gc_constants[op->arg_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    args[op->arg_guard.arg_idx]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type) == (MVMSTable *)wanted_st)
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    args[op->arg_guard.arg_idx] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guard, args[op->arg_guard.arg_idx],
                            wanted_st, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardArgTypeConc: {
                MVMCollectable *wanted_st = dp->gc_constants[op->arg_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    args[op->arg_guard.arg_idx]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type) == (MVMSTable *)wanted_st &&
                        (facts->flags & MVM_SPESH_FACT_CONCRETE))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    args[op->arg_guard.arg_idx] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardconc, args[op->arg_guard.arg_idx],
                            wanted_st, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardArgTypeTypeObject: {
                MVMCollectable *wanted_st = dp->gc_constants[op->arg_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    args[op->arg_guard.arg_idx]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type) == (MVMSTable *)wanted_st &&
                        (facts->flags & MVM_SPESH_FACT_TYPEOBJ))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    args[op->arg_guard.arg_idx] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardtype, args[op->arg_guard.arg_idx],
                            wanted_st, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardArgConc: {
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    args[op->arg_guard.arg_idx]);
                if (facts->flags & MVM_SPESH_FACT_CONCRETE)
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    args[op->arg_guard.arg_idx] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardjustconc, args[op->arg_guard.arg_idx],
                            NULL, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardArgTypeObject: {
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    args[op->arg_guard.arg_idx]);
                if (facts->flags & MVM_SPESH_FACT_TYPEOBJ)
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    args[op->arg_guard.arg_idx] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardjusttype, args[op->arg_guard.arg_idx],
                            NULL, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardArgLiteralObj: {
                MVMCollectable *wanted_obj = dp->gc_constants[op->arg_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    args[op->arg_guard.arg_idx]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) &&
                        facts->value.o == (MVMObject *)wanted_obj)
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    args[op->arg_guard.arg_idx] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardobj, args[op->arg_guard.arg_idx],
                            wanted_obj, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardArgLiteralStr: {
                MVMString *wanted_str = (MVMString *)dp->gc_constants[op->arg_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    args[op->arg_guard.arg_idx]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) &&
                        MVM_string_equal(tc, facts->value.s, wanted_str))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    args[op->arg_guard.arg_idx] = emit_literal_str_guard(tc, g, bb,
                            &insert_after, args[op->arg_guard.arg_idx], wanted_str,
                            deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardArgNotLiteralObj: {
                MVMCollectable *unwanted_obj = dp->gc_constants[op->arg_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    args[op->arg_guard.arg_idx]);
                if (((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) &&
                        facts->value.o != (MVMObject *)unwanted_obj) ||
                        ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type) != STABLE(unwanted_obj)))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    args[op->arg_guard.arg_idx] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardnotobj, args[op->arg_guard.arg_idx],
                            unwanted_obj, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardArgHLL: {
                MVMHLLConfig *wanted_hll = dp->constants[op->arg_guard.checkee].hll;
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    args[op->arg_guard.arg_idx]);
                if (((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) &&
                        STABLE(facts->value.o)->hll_owner == wanted_hll) ||
                        ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type)->hll_owner == wanted_hll))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    args[op->arg_guard.arg_idx] = emit_hll_guard(tc, g, bb,
                            &insert_after, args[op->arg_guard.arg_idx], wanted_hll,
                            deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardTempType: {
                MVMCollectable *wanted_st = dp->gc_constants[op->temp_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    temporaries[op->temp_guard.temp]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type) == (MVMSTable *)wanted_st)
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    temporaries[op->temp_guard.temp] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guard, temporaries[op->temp_guard.temp],
                            wanted_st, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardTempTypeConc: {
                MVMCollectable *wanted_st = dp->gc_constants[op->temp_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    temporaries[op->temp_guard.temp]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type) == (MVMSTable *)wanted_st &&
                        (facts->flags & MVM_SPESH_FACT_CONCRETE))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    temporaries[op->temp_guard.temp] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardconc, temporaries[op->temp_guard.temp],
                            wanted_st, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardTempTypeTypeObject: {
                MVMCollectable *wanted_st = dp->gc_constants[op->temp_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    temporaries[op->temp_guard.temp]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type) == (MVMSTable *)wanted_st &&
                        (facts->flags & MVM_SPESH_FACT_TYPEOBJ))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    temporaries[op->temp_guard.temp] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardtype, temporaries[op->temp_guard.temp],
                            wanted_st, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardTempConc: {
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    temporaries[op->temp_guard.temp]);
                if (facts->flags & MVM_SPESH_FACT_CONCRETE)
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    temporaries[op->temp_guard.temp] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardjustconc, temporaries[op->temp_guard.temp],
                            NULL, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardTempTypeObject: {
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    temporaries[op->temp_guard.temp]);
                if (facts->flags & MVM_SPESH_FACT_TYPEOBJ)
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    temporaries[op->temp_guard.temp] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardjusttype, temporaries[op->temp_guard.temp],
                            NULL, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardTempLiteralObj: {
                MVMCollectable *wanted_obj = dp->gc_constants[op->temp_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    temporaries[op->temp_guard.temp]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) &&
                        facts->value.o == (MVMObject *)wanted_obj)
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    temporaries[op->temp_guard.temp] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardobj, temporaries[op->temp_guard.temp],
                            wanted_obj, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardTempLiteralStr: {
                MVMString *wanted_str = (MVMString *)dp->gc_constants[op->temp_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    temporaries[op->temp_guard.temp]);
                if ((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) &&
                        MVM_string_equal(tc, facts->value.s, wanted_str))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    temporaries[op->temp_guard.temp] = emit_literal_str_guard(tc, g, bb,
                        &insert_after, temporaries[op->temp_guard.temp],
                        wanted_str, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardTempNotLiteralObj: {
                MVMCollectable *unwanted_obj = dp->gc_constants[op->temp_guard.checkee];
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    temporaries[op->temp_guard.temp]);
                if (((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) &&
                        facts->value.o != (MVMObject *)unwanted_obj) ||
                        ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type) != STABLE(unwanted_obj)))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    temporaries[op->temp_guard.temp] = emit_guard(tc, g, bb, &insert_after,
                            MVM_OP_sp_guardnotobj, temporaries[op->temp_guard.temp],
                            unwanted_obj, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeGuardTempHLL: {
                MVMHLLConfig *wanted_hll = dp->constants[op->temp_guard.checkee].hll;
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g,
                    temporaries[op->temp_guard.temp]);
                if (((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) &&
                        STABLE(facts->value.o)->hll_owner == wanted_hll) ||
                        ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                        STABLE(facts->type)->hll_owner == wanted_hll))
                    MVM_spesh_use_facts(tc, g, facts);
                else
                    temporaries[op->temp_guard.temp] = emit_hll_guard(tc, g, bb,
                            &insert_after, temporaries[op->temp_guard.temp],
                            wanted_hll, deopt_ann, &reused_deopt_ann);
                break;
            }
            case MVMDispOpcodeLoadCaptureValue:
                /* We already have all the capture values in the arg registers
                 * so just alias. */
                temporaries[op->load.temp] = args[op->load.idx];
                break;
            case MVMDispOpcodeLoadConstantObjOrStr: {
                MVMCollectable *value = dp->gc_constants[op->load.idx];
                MVMuint16 reg_kind = REPR(value)->ID == MVM_REPR_ID_MVMString
                    ? MVM_reg_str
                    : MVM_reg_obj;
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, reg_kind);
                temporaries[op->load.temp] = temp;
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_load_spesh_slot(tc, g, bb, &insert_after, temp, value);
                break;
            }
            case MVMDispOpcodeLoadConstantInt: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
                temporaries[op->load.temp] = temp;
                MVM_VECTOR_PUSH(allocated_temps, temp);
                MVMSpeshOperand constint = { .lit_i64 = dp->constants[op->load.idx].i64 };
                emit_bi_op(tc, g, bb, &insert_after, MVM_OP_const_i64,
                    temporaries[op->load.temp], constint);
                MVMSpeshFacts *f = MVM_spesh_get_facts(tc, g, temp);
                f->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                f->value.i = constint.lit_i64;
                break;
            }
            case MVMDispOpcodeLoadConstantNum: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_num64);
                temporaries[op->load.temp] = temp;
                MVM_VECTOR_PUSH(allocated_temps, temp);
                MVMSpeshOperand constnum = { .lit_n64 = dp->constants[op->load.idx].n64 };
                emit_bi_op(tc, g, bb, &insert_after, MVM_OP_const_n64,
                    temporaries[op->load.temp], constnum);
                MVMSpeshFacts *f = MVM_spesh_get_facts(tc, g, temp);
                f->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                f->value.n = constnum.lit_n64;
                break;
            }
            case MVMDispOpcodeLoadAttributeObj: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_load_attribute(tc, g, bb, &insert_after, MVM_OP_sp_p6oget_o, temp,
                        temporaries[op->load.temp], op->load.idx);
                temporaries[op->load.temp] = temp;
                break;
            }
            case MVMDispOpcodeLoadAttributeInt: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_load_attribute(tc, g, bb, &insert_after, MVM_OP_sp_p6oget_i, temp,
                        temporaries[op->load.temp], op->load.idx);
                temporaries[op->load.temp] = temp;
                break;
            }
            case MVMDispOpcodeLoadAttributeNum: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_num64);
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_load_attribute(tc, g, bb, &insert_after, MVM_OP_sp_p6oget_n, temp,
                        temporaries[op->load.temp], op->load.idx);
                temporaries[op->load.temp] = temp;
                break;
            }
            case MVMDispOpcodeLoadAttributeStr: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_str);
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_load_attribute(tc, g, bb, &insert_after, MVM_OP_sp_p6oget_s, temp,
                        temporaries[op->load.temp], op->load.idx);
                temporaries[op->load.temp] = temp;
                break;
            }
            case MVMDispOpcodeUnboxInt: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
                temporaries[op->load.temp] = temp;
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_bi_op(tc, g, bb, &insert_after, MVM_OP_unbox_i,
                    temporaries[op->load.temp], temporaries[op->load.idx]);
                break;
            }
            case MVMDispOpcodeUnboxNum: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_num64);
                temporaries[op->load.temp] = temp;
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_bi_op(tc, g, bb, &insert_after, MVM_OP_unbox_n,
                    temporaries[op->load.temp], temporaries[op->load.idx]);
                break;
            }
            case MVMDispOpcodeUnboxStr: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_str);
                temporaries[op->load.temp] = temp;
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_bi_op(tc, g, bb, &insert_after, MVM_OP_unbox_s,
                    temporaries[op->load.temp], temporaries[op->load.idx]);
                break;
            }
            case MVMDispOpcodeLoadHOW: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
                temporaries[op->load.temp] = temp;
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_bi_op(tc, g, bb, &insert_after, MVM_OP_gethow,
                    temporaries[op->load.temp], temporaries[op->load.idx]);
                break;
            }
            case MVMDispOpcodeLookup: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_str);
                MVM_VECTOR_PUSH(allocated_temps, temp);
                emit_tri_op(tc, g, bb, &insert_after, MVM_OP_atkey_o, temp,
                    temporaries[op->load.temp], temporaries[op->load.idx]);
                temporaries[op->load.temp] = temp;
                break;
            }
            case MVMDispOpcodeSet:
                /* Don't need to turn this into a set for now at least, since
                 * dispatch programs currently have temporaries as write only. */
                temporaries[op->load.temp] = temporaries[op->load.idx];
                break;
            case MVMDispOpcodeResultValueObj:
                /* Emit instruction according to result type. */
                switch (ins->info->opcode) {
                    case MVM_OP_dispatch_v:
                        break;
                    case MVM_OP_dispatch_o:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_set, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        MVM_spesh_copy_facts(tc, g, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_i:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_unbox_i, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_u:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_unbox_u, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_n:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_unbox_n, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_s:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_unbox_s, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    default:
                        MVM_oops(tc, "Unexpected dispatch op when translating object result");
                }
                break;
            case MVMDispOpcodeResultValueStr:
                switch (ins->info->opcode) {
                    case MVM_OP_dispatch_v:
                        break;
                    case MVM_OP_dispatch_o: {
                        MVMObject *box_type = g->sf->body.cu->body.hll_config->str_box_type;
                        MVMSpeshOperand type_temp = MVM_spesh_manipulate_get_temp_reg(tc,
                            g, MVM_reg_obj);
                        MVM_VECTOR_PUSH(allocated_temps, type_temp);
                        emit_load_spesh_slot(tc, g, bb, &insert_after, type_temp,
                            (MVMCollectable *)box_type);
                        emit_tri_op(tc, g, bb, &insert_after, MVM_OP_box_s, ins->operands[0],
                            temporaries[op->res_value.temp], type_temp);
                        break;
                    }
                    case MVM_OP_dispatch_i:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_si, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_u:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_su, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_n:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_sn, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_s:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_set, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    default:
                        MVM_oops(tc, "Unexpected dispatch op when translating string result");
                }
                break;
            case MVMDispOpcodeResultValueInt:
                switch (ins->info->opcode) {
                    case MVM_OP_dispatch_v:
                        break;
                    case MVM_OP_dispatch_o: {
                        MVMObject *box_type = g->sf->body.cu->body.hll_config->int_box_type;
                        MVMSpeshOperand type_temp = MVM_spesh_manipulate_get_temp_reg(tc,
                            g, MVM_reg_obj);
                        MVM_VECTOR_PUSH(allocated_temps, type_temp);
                        emit_load_spesh_slot(tc, g, bb, &insert_after, type_temp,
                            (MVMCollectable *)box_type);
                        emit_tri_op(tc, g, bb, &insert_after, MVM_OP_box_i, ins->operands[0],
                            temporaries[op->res_value.temp], type_temp);
                        break;
                    }
                    case MVM_OP_dispatch_i:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_set, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_u:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_set, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_n:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_in, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_s:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_is, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    default:
                        MVM_oops(tc, "Unexpected dispatch op when translating int result");
                }
                break;
            case MVMDispOpcodeResultValueNum:
                switch (ins->info->opcode) {
                    case MVM_OP_dispatch_v:
                        break;
                    case MVM_OP_dispatch_o: {
                        MVMObject *box_type = g->sf->body.cu->body.hll_config->num_box_type;
                        MVMSpeshOperand type_temp = MVM_spesh_manipulate_get_temp_reg(tc,
                            g, MVM_reg_obj);
                        MVM_VECTOR_PUSH(allocated_temps, type_temp);
                        emit_load_spesh_slot(tc, g, bb, &insert_after, type_temp,
                            (MVMCollectable *)box_type);
                        emit_tri_op(tc, g, bb, &insert_after, MVM_OP_box_n, ins->operands[0],
                            temporaries[op->res_value.temp], type_temp);
                        break;
                    }
                    case MVM_OP_dispatch_i:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_ni, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_u:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_nu, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_n:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_set, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_s:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_ns, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    default:
                        MVM_oops(tc, "Unexpected dispatch op when translating int result");
                }
                break;
            case MVMDispOpcodeUseArgsTail:
                callsite = dp->constants[op->use_arg_tail.callsite_idx].cs;
                skip_args = op->use_arg_tail.skip_args;
                break;
            case MVMDispOpcodeCopyArgsTail:
                callsite = dp->constants[op->copy_arg_tail.callsite_idx].cs;
                if (op->copy_arg_tail.tail_args > 0) {
                    MVMuint32 to_copy = op->copy_arg_tail.tail_args;
                    MVMuint32 source_idx = num_real_args - to_copy;
                    MVMuint32 target_idx = dp->first_args_temporary +
                            (callsite->flag_count - to_copy);
                    MVMuint32 i;
                    for (i = 0; i < to_copy; i++)
                        temporaries[target_idx++] = args[source_idx++];
                }
                break;
            case MVMDispOpcodeResultForeignCode: {
                /* Determine the op we'll specialize to. */
                MVMOpInfo const *base_op;
                switch (ins->info->opcode) {
                    case MVM_OP_dispatch_v:
                        base_op = MVM_op_get_op(MVM_OP_sp_runnativecall_v);
                        break;
                    case MVM_OP_dispatch_o:
                        base_op = MVM_op_get_op(MVM_OP_sp_runnativecall_o);
                        break;
                    case MVM_OP_dispatch_i:
                        base_op = MVM_op_get_op(MVM_OP_sp_runnativecall_i);
                        break;
                    case MVM_OP_dispatch_u:
                        base_op = MVM_op_get_op(MVM_OP_sp_runnativecall_u);
                        break;
                    case MVM_OP_dispatch_n:
                        base_op = MVM_op_get_op(MVM_OP_sp_runnativecall_n);
                        break;
                    case MVM_OP_dispatch_s:
                        base_op = MVM_op_get_op(MVM_OP_sp_runnativecall_s);
                        break;
                    default:
                        MVM_oops(tc, "Unexpected dispatch op when translating bytecode result");
                }

                MVMSpeshFacts *object_facts = MVM_spesh_get_facts(tc, g, temporaries[op->res_code.temp_invokee]);
                MVMNativeCallBody *body = (object_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE)
                    ? MVM_nativecall_get_nc_body(tc, object_facts->value.o)
                    : NULL;

                int box_return_value = 0;
                int has_return_value = (ins->info->opcode != MVM_OP_dispatch_v);
                int undef_return_value = 0;
                if (ins->info->opcode == MVM_OP_dispatch_o && body) {
                    /* Let's see if we can turn it into a more JITable instruction */
                    switch (body->ret_type & MVM_NATIVECALL_ARG_TYPE_MASK) {
                        case MVM_NATIVECALL_ARG_VOID:
                            base_op = MVM_op_get_op(MVM_OP_sp_runnativecall_v);
                            has_return_value = 0;
                            undef_return_value = 1;
                            break;
                        case MVM_NATIVECALL_ARG_CHAR:
                        case MVM_NATIVECALL_ARG_SHORT:
                        case MVM_NATIVECALL_ARG_INT:
                        case MVM_NATIVECALL_ARG_LONG:
                        case MVM_NATIVECALL_ARG_LONGLONG:
                        case MVM_NATIVECALL_ARG_CPOINTER:
                            base_op = MVM_op_get_op(MVM_OP_sp_runnativecall_i);
                            box_return_value = 1;
                            break;
                    }
                }

                /* Form the varargs op and create the instruction. */
                MVMOpInfo *rb_op = MVM_spesh_alloc(tc, g, MVM_spesh_disp_dispatch_op_info_size(
                    tc, base_op, callsite));
                MVM_spesh_disp_initialize_dispatch_op_info(tc, base_op, callsite, rb_op);
                MVMSpeshIns *rb_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                rb_ins->info = rb_op;
                rb_ins->operands = MVM_spesh_alloc(tc, g,
                     rb_op->num_operands * sizeof(MVMSpeshOperand));

                /* Write result into dispatch result register unless void. */
                MVMuint16 cur_op = 0;
                if (has_return_value) {
                    if (box_return_value) {
                        rb_ins->operands[cur_op] = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
                    }
                    else {
                        rb_ins->operands[cur_op] = ins->operands[0];
                    }
                    MVM_spesh_get_facts(tc, g, rb_ins->operands[cur_op])->writer = rb_ins;
                    cur_op++;
                }

                /* Add the operand with the bytecode to run. */
                rb_ins->operands[cur_op] = temporaries[op->res_code.temp_invokee];
                MVM_spesh_usages_add_by_reg(tc, g, rb_ins->operands[cur_op], rb_ins);
                cur_op++;

                int has_rw_dummy = 0;
                MVMSpeshOperand rw_dummy = { 0 };

                MVMuint8 *is_rw_operand = MVM_spesh_alloc(tc, g, rb_op->num_operands * sizeof(MVMuint8));
                MVMSpeshOperand *rw_operands = MVM_spesh_alloc(tc, g, rb_op->num_operands * sizeof(MVMSpeshOperand));
                if (body) {
                    MVMint16 *arg_types = body->arg_types;
                    MVMuint16 j;
                    for (j = 1; j < callsite->flag_count; j++) { /* first arg is return type */
                        if ((arg_types[j - 1] & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW) {
                            switch (arg_types[j - 1] & MVM_NATIVECALL_ARG_TYPE_MASK) {
                                case MVM_NATIVECALL_ARG_CHAR:
                                case MVM_NATIVECALL_ARG_SHORT:
                                case MVM_NATIVECALL_ARG_INT:
                                case MVM_NATIVECALL_ARG_LONG:
                                case MVM_NATIVECALL_ARG_LONGLONG: {
                                    MVMSpeshOperand var = skip_args >= 0 ? args[skip_args + j] : temporaries[dp->first_args_temporary + j];
                                    is_rw_operand[j] = 1;
                                    rw_operands[j] = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
                                    emit_bi_op(tc, g, bb, &insert_after, MVM_OP_decont_i, rw_operands[j], var);

                                    callsite = MVM_callsite_replace_positional(tc, callsite, j, MVM_CALLSITE_ARG_INT);
                                    MVM_callsite_intern(tc, &callsite, 1, 0);

                                    if (has_return_value && var.reg.orig == rb_ins->operands[0].reg.orig) {
                                        rw_dummy = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
                                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_set, rw_dummy, var);
                                        has_rw_dummy = 1;
                                    }
                                    break;
                                }
                                case MVM_NATIVECALL_ARG_UCHAR:
                                case MVM_NATIVECALL_ARG_USHORT:
                                case MVM_NATIVECALL_ARG_UINT:
                                case MVM_NATIVECALL_ARG_ULONG:
                                case MVM_NATIVECALL_ARG_ULONGLONG: {
                                    MVMSpeshOperand var = skip_args >= 0 ? args[skip_args + j] : temporaries[dp->first_args_temporary + j];
                                    is_rw_operand[j] = 1;
                                    rw_operands[j] = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_uint64);
                                    emit_bi_op(tc, g, bb, &insert_after, MVM_OP_decont_u, rw_operands[j], var);

                                    callsite = MVM_callsite_replace_positional(tc, callsite, j, MVM_CALLSITE_ARG_UINT);
                                    MVM_callsite_intern(tc, &callsite, 1, 0);

                                    if (has_return_value && var.reg.orig == rb_ins->operands[0].reg.orig) {
                                        rw_dummy = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
                                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_set, rw_dummy, var);
                                        has_rw_dummy = 1;
                                    }
                                    break;
                                }
                                default:
                                    MVM_spesh_graph_add_comment(tc, g, ins, "dispatch not compiled: unsupported NativeCall rw type %d",
                                                    arg_types[j - 1] & MVM_NATIVECALL_ARG_TYPE_MASK);
                                    /* Clean up the inserted guards */
                                    ins = ins->next;
                                    while (ins && ins != orig_next) {
                                        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
                                        ins = ins->next;
                                    }
                                    return 0;
                            }
                        }
                    }
                }

                /* Add the callsite operand, smuggled as a 64-bit int. */
                rb_ins->operands[cur_op++].lit_ui64 = (MVMuint64)callsite;

                /* Add the argument operands. */
                if (skip_args >= 0) {
                    MVMuint16 j;
                    for (j = 0; j < callsite->flag_count; j++) {
                        rb_ins->operands[cur_op] = is_rw_operand[j]
                            ? rw_operands[j]
                            : args[skip_args + j];
                        MVM_spesh_usages_add_by_reg(tc, g, rb_ins->operands[cur_op], rb_ins);
                        cur_op++;
                    }
                }
                else {
                    MVMuint16 j;
                    for (j = 0; j < callsite->flag_count; j++) {
                        rb_ins->operands[cur_op] = is_rw_operand[j]
                            ? rw_operands[j]
                            : temporaries[dp->first_args_temporary + j];
                        MVM_spesh_usages_add_by_reg(tc, g, rb_ins->operands[cur_op], rb_ins);
                        cur_op++;
                    }
                }

                /* Insert the produced instruction. */
                MVM_spesh_manipulate_insert_ins(tc, bb, insert_after, rb_ins);
                insert_after = rb_ins;

                MVMSpeshIns *post_call_instructions = NULL;

                MVMuint16 j;
                for (j = 1; j < callsite->flag_count; j++) {
                    if (is_rw_operand[j]) {
                        MVMSpeshOperand var = has_rw_dummy
                            ? rw_dummy
                            : skip_args >= 0 ? args[skip_args + j] : temporaries[dp->first_args_temporary + j];
                        emit_bi_op(tc, g, bb->linear_next, &post_call_instructions,
                            g->local_types[rw_operands[j].reg.orig] == MVM_reg_int64
                                ? MVM_OP_assign_i
                                : MVM_OP_assign_u,
                            var, rw_operands[j]);
                        MVM_spesh_usages_add_by_reg(tc, g, var, post_call_instructions);
                        MVM_spesh_manipulate_release_temp_reg(tc, g, rw_operands[j]);
                        if (has_rw_dummy)
                            MVM_spesh_manipulate_release_temp_reg(tc, g, var);
                    }
                }

                if (undef_return_value) {
                    emit_bi_op(tc, g, bb->linear_next, &post_call_instructions, MVM_OP_set,
                        ins->operands[0],
                        rb_ins->operands[2]);
                    MVM_spesh_get_facts(tc, g, post_call_instructions->operands[0])->writer = post_call_instructions;
                }

                if (box_return_value) {
                    MVMSpeshFacts *return_type_facts = MVM_spesh_get_facts(tc, g, rb_ins->operands[3]);
                    if ((body->ret_type & MVM_NATIVECALL_ARG_TYPE_MASK) == MVM_NATIVECALL_ARG_CPOINTER) {
                        /* For NULL pointers, we have to return a type object instead of a boxed NULL */
                        MVMSpeshOperand set_target_reg = MVM_spesh_manipulate_new_version(tc, g, ins->operands[0].reg.orig);
                        emit_bi_op(tc, g, bb->linear_next, &post_call_instructions, MVM_OP_set,
                            set_target_reg,
                            rb_ins->operands[3]);

                        MVMSpeshBB *target_bb = MVM_spesh_manipulate_split_BB_at(
                            tc, g, bb->linear_next, post_call_instructions->next);

                        emit_iffy_op(tc, g, bb->linear_next, &post_call_instructions, MVM_OP_unless_i,
                            rb_ins->operands[0],
                            target_bb);

                        MVMSpeshOperand box_target_reg = MVM_spesh_manipulate_new_version(tc, g, ins->operands[0].reg.orig);
                        emit_tri_op(tc, g, bb->linear_next, &post_call_instructions, MVM_OP_box_i,
                            box_target_reg,
                            rb_ins->operands[0],
                            rb_ins->operands[3]);

                        MVMSpeshBB *box_bb = MVM_spesh_manipulate_split_BB_at(
                            tc, g, bb->linear_next, post_call_instructions);

                        bb->linear_next->children[1] = target_bb;
                        bb->linear_next->succ[1] = target_bb;
                        box_bb->num_succ = 1;
                        box_bb->num_children = 0;
                        box_bb->pred[0] = bb->linear_next;
                        target_bb->num_pred = 2;
                        target_bb->pred = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB*) * 2);
                        target_bb->pred[0] = bb->linear_next;
                        target_bb->pred[1] = box_bb;

                        MVM_spesh_graph_place_phi(tc, g, target_bb, 2, ins->operands[0].reg.orig);
                        MVMSpeshIns *phi = target_bb->first_ins;
                        phi->operands[0].reg.i = ins->operands[0].reg.i;
                        phi->operands[1].reg.i = set_target_reg.reg.i;
                        phi->operands[2].reg.i = box_target_reg.reg.i;
                        MVM_spesh_get_facts(tc, g, phi->operands[0])->writer = phi;
                        MVM_spesh_usages_add_by_reg(tc, g, set_target_reg, phi);
                        MVM_spesh_usages_add_by_reg(tc, g, box_target_reg, phi);
                    }
                    else {
                        emit_tri_op(tc, g, bb->linear_next, &post_call_instructions, MVM_OP_box_i,
                            ins->operands[0],
                            rb_ins->operands[0],
                            rb_ins->operands[3]);
                    }
                    if (return_type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
                        MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                        result_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
                        result_facts->type = return_type_facts->type;
                    }
                    MVM_spesh_manipulate_release_temp_reg(tc, g, rb_ins->operands[0]);
                }

                /* Make sure we delay release of temporaries since optimization
                 * can add further ones. */
                delay_temps_release = 1;
                break;
            }
            case MVMDispOpcodeResultBytecode:
            case MVMDispOpcodeResultCFunction: {
                /* Determine the op we'll specialize to. */
                MVMOpInfo const *base_op;
                MVMuint32 c = op->code == MVMDispOpcodeResultCFunction;
                switch (ins->info->opcode) {
                    case MVM_OP_dispatch_v:
                        base_op = MVM_op_get_op(c ? MVM_OP_sp_runcfunc_v : MVM_OP_sp_runbytecode_v);
                        break;
                    case MVM_OP_dispatch_o:
                        base_op = MVM_op_get_op(c ? MVM_OP_sp_runcfunc_o : MVM_OP_sp_runbytecode_o);
                        break;
                    case MVM_OP_dispatch_i:
                        base_op = MVM_op_get_op(c ? MVM_OP_sp_runcfunc_i : MVM_OP_sp_runbytecode_i);
                        break;
                    case MVM_OP_dispatch_u:
                        base_op = MVM_op_get_op(c ? MVM_OP_sp_runcfunc_u : MVM_OP_sp_runbytecode_u);
                        break;
                    case MVM_OP_dispatch_n:
                        base_op = MVM_op_get_op(c ? MVM_OP_sp_runcfunc_n : MVM_OP_sp_runbytecode_n);
                        break;
                    case MVM_OP_dispatch_s:
                        base_op = MVM_op_get_op(c ? MVM_OP_sp_runcfunc_s : MVM_OP_sp_runbytecode_s);
                        break;
                    default:
                        MVM_oops(tc, "Unexpected dispatch op when translating bytecode result");
                }

                /* For bytecode invocations, insert ops to capture any resume
                 * initialization states. */
                if (!c)
                    insert_resume_inits(tc, g, bb, &insert_after, dp, orig_args, temporaries,
                            deopt_all_ann->data.deopt_idx);

                /* Form the varargs op and create the instruction. */
                MVMOpInfo *rb_op = MVM_spesh_alloc(tc, g, MVM_spesh_disp_dispatch_op_info_size(
                    tc, base_op, callsite));
                MVM_spesh_disp_initialize_dispatch_op_info(tc, base_op, callsite, rb_op);
                MVMSpeshIns *rb_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                rb_ins->info = rb_op;
                rb_ins->operands = MVM_spesh_alloc(tc, g,
                     rb_op->num_operands * sizeof(MVMSpeshOperand));

                /* Write result into dispatch result register unless void. */
                MVMuint16 cur_op = 0;
                if (ins->info->opcode != MVM_OP_dispatch_v) {
                    rb_ins->operands[cur_op] = ins->operands[0];
                    MVM_spesh_get_facts(tc, g, rb_ins->operands[cur_op])->writer = rb_ins;
                    cur_op++;
                }

                /* Add the operand with the bytecode to run. */
                rb_ins->operands[cur_op] = temporaries[op->res_code.temp_invokee];
                MVM_spesh_usages_add_by_reg(tc, g, rb_ins->operands[cur_op], rb_ins);
                cur_op++;

                /* Add the callsite operand, smuggled as a 64-bit int. */
                rb_ins->operands[cur_op++].lit_ui64 = (MVMuint64)callsite;

                /* If it's not C, we can pre-select a spesh candidate. This will
                 * be taken care of by the optimizer; for now we poke -1 into the
                 * value to mean that we didn't pre-select a candidate. We also
                 * add deopt all and logged annotations, plus the predeopt one
                 * if we inserted no guards. */
                if (!c) {
                    rb_ins->operands[cur_op++].lit_i16 = -1;
                    deopt_all_ann->next = cached_ann;
                    if (!reused_deopt_ann) {
                        deopt_ann->next = deopt_all_ann;
                        rb_ins->annotations = deopt_ann;
                    }
                    else {
                        rb_ins->annotations = deopt_all_ann;
                    }
                }

                /* Add the argument operands. */
                if (skip_args >= 0) {
                    MVMuint16 j;
                    for (j = 0; j < callsite->flag_count; j++) {
                        rb_ins->operands[cur_op] = args[skip_args + j];
                        MVM_spesh_usages_add_by_reg(tc, g, rb_ins->operands[cur_op], rb_ins);
                        cur_op++;
                    }
                }
                else {
                    MVMuint16 j;
                    for (j = 0; j < callsite->flag_count; j++) {
                        rb_ins->operands[cur_op] = temporaries[dp->first_args_temporary + j];
                        MVM_spesh_usages_add_by_reg(tc, g, rb_ins->operands[cur_op], rb_ins);
                        cur_op++;
                    }
                }

                /* Insert the produced instruction. */
                MVM_spesh_manipulate_insert_ins(tc, bb, insert_after, rb_ins);
                insert_after = rb_ins;

                /* Make sure we delay release of temporaries since optimization
                 * can add further ones. */
                delay_temps_release = 1;
                break;
            }
            default:
                /* Should never happen due to the validation earlier. */
                MVM_oops(tc, "Unexpectedly hit dispatch op that cannot be translated");
        }
    }

    /* Annotate start and end of translated dispatch program. */
    MVMSpeshIns *first_inserted = ins->next;
    if (first_inserted)
        MVM_spesh_graph_add_comment(tc, g, first_inserted,
                "Start of dispatch program translation");

    /* Delete the dispatch instruction. Make sure we don't mark it as having
     * a dead writer (since we inserted a replacement instruction above). */
    MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
    if (keep_result_register)
        MVM_spesh_get_facts(tc, g, ins->operands[0])->dead_writer = 0;

    /* Release temporaries now or pass them along for later. */
    if (delay_temps_release && MVM_VECTOR_ELEMS(allocated_temps)) {
        MVMSpeshOperand end_sentinel = { .lit_i64 = -1 };
        MVM_VECTOR_PUSH(allocated_temps, end_sentinel);
        MVMSpeshAnn *ann = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
        ann->type = MVM_SPESH_ANN_DELAYED_TEMPS;
        ann->data.temps_to_release = allocated_temps;
        ann->next = insert_after->annotations;
        insert_after->annotations = ann;
    }
    else {
        for (i = 0; i < MVM_VECTOR_ELEMS(allocated_temps); i++)
            MVM_spesh_manipulate_release_temp_reg(tc, g, allocated_temps[i]);
        MVM_VECTOR_DESTROY(allocated_temps);
    }

    /* Return the first inserted instruction as the next thing to optimize. */
    *next_ins = first_inserted;
    return 1;
}

/* Drives the overall process of optimizing a dispatch instruction. The instruction
 * will always recieve some transformation, even if it's simply to sp_dispatch_*,
 * which pre-resolves the inline cache (and so allows inlining of code that still
 * wants to reference the inline cache of the place it came from). */
static int compare_hits(const void *a, const void *b) {
    return ((OutcomeHitCount *)b)->hits - ((OutcomeHitCount *)a)->hits;
}
int MVM_spesh_disp_optimize(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
        MVMSpeshPlanned *p, MVMSpeshIns *ins, MVMSpeshIns **next_ins) {
    /* Locate the inline cache bytecode offset. There must always be one. */
    MVMSpeshAnn *ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_CACHED)
            break;
        ann = ann->next;
    }
    if (!ann)
        MVM_oops(tc, "Dispatch specialization could not find bytecode offset for dispatch instruction");
    MVMuint32 bytecode_offset = ann->data.bytecode_offset;

    /* Now find the inline cache entry, see what kind of entry it is, and
     * optimize appropriately. We return if we manage to translate it into
     * something better. */
    MVMDispInlineCache *cache = &(g->sf->body.inline_cache);
    MVMDispInlineCacheEntry *entry = cache->entries[bytecode_offset >> cache->bit_shift];
    MVMuint32 kind = MVM_disp_inline_cache_get_kind(tc, entry);
    switch (kind) {
        case MVM_INLINE_CACHE_KIND_INITIAL:
        case MVM_INLINE_CACHE_KIND_INITIAL_FLATTENING:
            /* Never hit. */
            MVM_spesh_graph_add_comment(tc, g, ins, "Never dispatched");
            break;
        case MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH:
            /* Monomorphic, so translate the dispatch program if we can. */
            MVM_spesh_graph_add_comment(tc, g, ins, "Monomorphic in the inline cache");
            if (translate_dispatch_program(tc, g, bb, ins,
                ((MVMDispInlineCacheEntryMonomorphicDispatch *)entry)->dp, next_ins)) {
                return 1;
            }
            break;
        case MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH_FLATTENING:
            MVM_spesh_graph_add_comment(tc, g, ins, "Monomorphic but flattening (no opt yet)");
            break;
        case MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH: {
            /* Interesting. It's polymorphic in the inline cache, *but* we are
             * producing a specialization, and it may be monomorphic in this
             * specialization. See if this is so. */
            MVM_VECTOR_DECL(OutcomeHitCount, outcome_hits);
            MVM_VECTOR_INIT(outcome_hits, 0);
            MVMuint32 total_hits = 0;
            MVMuint32 i;
            for (i = 0; i < (p ? p->num_type_stats : 0); i++) {
                MVMSpeshStatsByType *ts = p->type_stats[i];
                MVMuint32 j;
                for (j = 0; j < ts->num_by_offset; j++) {
                    if (ts->by_offset[j].bytecode_offset == bytecode_offset) {
                        /* We found some stats at the offset of the dispatch. Count the
                         * hits. */
                        MVMuint32 k;
                        for (k = 0; k < ts->by_offset[j].num_dispatch_results; k++) {
                            MVMSpeshStatsDispatchResultCount *outcome_count =
                                    &(ts->by_offset[j].dispatch_results[k]);
                            MVMuint32 l;
                            MVMuint32 found = 0;
                            for (l = 0; l < MVM_VECTOR_ELEMS(outcome_hits); l++) {
                                if (outcome_hits[l].outcome == outcome_count->result_index) {
                                    outcome_hits[l].hits += outcome_count->count;
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found) {
                                OutcomeHitCount ohc = {
                                    .outcome = outcome_count->result_index,
                                    .hits = outcome_count->count
                                };
                                MVM_VECTOR_PUSH(outcome_hits, ohc);
                            }
                            total_hits += outcome_count->count;
                        }
                        break;
                    }
                }
            }
            qsort(outcome_hits, MVM_VECTOR_ELEMS(outcome_hits), sizeof(OutcomeHitCount),
                compare_hits);
            MVMint32 selected_outcome = -1;
            if (MVM_VECTOR_ELEMS(outcome_hits) == 0) {
                MVM_spesh_graph_add_comment(tc, g, ins, p
                    ? "Polymorphic callsite and polymorphic in this specialization"
                    : "No stats available to resolve polymorphic callsite");
            }
            else if ((100 * outcome_hits[0].hits) / total_hits >= 99) {
                MVM_spesh_graph_add_comment(tc, g, ins,
                        "Polymorphic callsite made monomorphic by specialization");
                selected_outcome = outcome_hits[0].outcome;
            }
            else {
                MVM_spesh_graph_add_comment(tc, g, ins,
                        "Polymorphic callsite still polymorphic in specialization");
            }
            MVM_VECTOR_DESTROY(outcome_hits);

            /* If we managed to make it monomorphic by specialization, then
             * extract and translate the dispatch program. */
            if (selected_outcome >= 0) {
                MVMDispInlineCacheEntryPolymorphicDispatch *pd =
                    (MVMDispInlineCacheEntryPolymorphicDispatch *)entry;
                if ((MVMuint32)selected_outcome < pd->num_dps) {
                    if (translate_dispatch_program(tc, g, bb, ins,
                        pd->dps[selected_outcome], next_ins)) {
                        return 1;
                    }
                }
            }
            break;
        }
        case MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH_FLATTENING:
            MVM_spesh_graph_add_comment(tc, g, ins, "Polymorphic and flattening (no opt yet)");
            break;
        default:
            /* Really no idea... */
            MVM_spesh_graph_add_comment(tc, g, ins, "Unknown inline cache entry kind");
            break;
    }

    /* If we make it here, rewrite it into a sp_dispatch_* op. */
    rewrite_to_sp_dispatch(tc, g, ins, bytecode_offset);
    return 0;
}
