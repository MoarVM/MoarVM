#include "moar.h"

/* Create op info for a dispatch instruction, so that during specialization we
 * can pretend it's not varargs. */
MVMOpInfo * MVM_spesh_disp_create_dispatch_op_info(MVMThreadContext *tc, MVMSpeshGraph *g,
        const MVMOpInfo *base_info, MVMCallsite *callsite) {
    /* In general, ops support up to an operand limit; in the case that there are more,
     * we'd overrun the buffer. We thus allocate more. */
    MVMuint32 total_ops = base_info->num_operands + callsite->flag_count;
    size_t total_size = sizeof(MVMOpInfo) + (total_ops > MVM_MAX_OPERANDS
            ? total_ops - MVM_MAX_OPERANDS
            : 0);
    MVMOpInfo *dispatch_info = MVM_spesh_alloc(tc, g, total_size);

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
        else if (flag & MVM_CALLSITE_ARG_NUM) {
            dispatch_info->operands[operand_index] = MVM_operand_num64;
        }
        else if (flag & MVM_CALLSITE_ARG_STR) {
            dispatch_info->operands[operand_index] = MVM_operand_str;
        }
        dispatch_info->operands[operand_index] |= MVM_operand_read_reg;
    }

    return dispatch_info;
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
        case MVM_OP_dispatch_n: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_n); break;
        case MVM_OP_dispatch_o: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_o); break;
        case MVM_OP_dispatch_s: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_s); break;
        default:
            MVM_oops(tc, "Unexpected dispatch instruction to rewrite");
    }
    MVMOpInfo *new_ins_info = MVM_spesh_disp_create_dispatch_op_info(tc, g,
            new_ins_base_info, callsite);
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
            ins->info->opcode == MVM_OP_dispatch_n ||
            ins->info->opcode == MVM_OP_dispatch_s ||
            ins->info->opcode == MVM_OP_dispatch_o);
    return 3;
}

/* Find the deopt annotation on a dispatch instruction and remove it. */
static MVMSpeshAnn * take_dispatch_deopt_annotation(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshIns *ins) {
    MVMSpeshAnn *deopt_ann = ins->annotations;
    MVMSpeshAnn *prev = NULL;
    while (deopt_ann) {
        if (deopt_ann->type == MVM_SPESH_ANN_DEOPT_PRE_INS) {
            if (prev)
                prev->next = deopt_ann->next;
            else
                ins->annotations = deopt_ann->next;
            return deopt_ann;
        }
        prev = deopt_ann;
        deopt_ann = deopt_ann->next;
    }
    MVM_panic(1, "Spesh: unexpectedly missing deopt annotation on dispatch op");
}

/* Copy a deopt annotation, allocating a new deopt index for it. */
MVMSpeshAnn * clone_deopt_ann(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshAnn *in) {
    MVMSpeshAnn *cloned = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
    MVMuint32 deopt_idx = g->num_deopt_addrs;
    cloned->type = in->type;
    cloned->data.deopt_idx = deopt_idx;
    MVM_spesh_graph_grow_deopt_table(tc, g);
    g->deopt_addrs[deopt_idx * 2] = g->deopt_addrs[in->data.deopt_idx * 2];
    g->num_deopt_addrs++;
    return cloned;
}

/* Takes an instruction that may deopt and an operand that should contain the
 * deopt index. Reuse the dispatch instruction deopt annotation if that did
 * not happen already, otherwise clone it. */
static void set_deopt(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
        MVMSpeshOperand *index_operand, MVMSpeshAnn *deopt_ann,
        MVMuint32 *reused_deopt_ann) {
    if (*reused_deopt_ann) {
        /* Already reused, clone needed. */
        deopt_ann = clone_deopt_ann(tc, g, deopt_ann);
    }
    else {
        /* First usage. */
        *reused_deopt_ann = 1;
    }
    deopt_ann->next = ins->annotations;
    ins->annotations = deopt_ann;
    index_operand->lit_ui32 = deopt_ann->data.deopt_idx;
}

/* Emit a type guard instruction. */
static MVMSpeshOperand emit_type_guard(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMuint16 op,
        MVMSpeshOperand guard_reg, MVMSTable *type, MVMSpeshAnn *deopt_ann,
        MVMuint32 *reused_deopt_ann) {
    /* Produce a new version for after the guarding. */
    MVMSpeshOperand guarded_reg = MVM_spesh_manipulate_split_version(tc, g,
            guard_reg, bb, (*insert_after)->next);

    /* Produce the instruction and insert it. */
    MVMSpeshIns *guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(op);
    guard->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
    guard->operands[0] = guarded_reg;
    guard->operands[1] = guard_reg;
    guard->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
            (MVMCollectable *)type);
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

/* Emit an instruction to load a value into a spesh slot. */
static void emit_load_spesh_slot(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns **insert_after, MVMSpeshOperand to_reg,
        MVMCollectable *value) {
    MVMSpeshIns *ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
    ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * 2);
    ins->operands[0] = to_reg;
    ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g, value);
    MVM_spesh_manipulate_insert_ins(tc, bb, *insert_after, ins);
    *insert_after = ins;
    MVM_spesh_get_facts(tc, g, to_reg)->writer = ins;
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

/* Try to translate a dispatch program into a sequence of ops (which will
 * be subject to later optimization and potentially JIT compilation). */
static MVMSpeshIns * translate_dispatch_program(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns *ins, MVMDispProgram *dp) {
    /* First, validate it is a dispatch program we know how to compile. */
    if (dp->first_args_temporary != dp->num_temporaries)
        return NULL;
    if (dp->num_resumptions > 0)
        return NULL;
    MVMuint32 i;
    for (i = 0; i < dp->num_ops; i++) {
        switch (dp->ops[i].code) {
            case MVMDispOpcodeGuardArgType:
            case MVMDispOpcodeGuardArgTypeConc:
            case MVMDispOpcodeGuardArgTypeTypeObject:
            case MVMDispOpcodeLoadCaptureValue:
            case MVMDispOpcodeLoadConstantObjOrStr:
            case MVMDispOpcodeLoadConstantInt:
            case MVMDispOpcodeLoadConstantNum:
            case MVMDispOpcodeLoadAttributeObj:
            case MVMDispOpcodeLoadAttributeInt:
            case MVMDispOpcodeLoadAttributeNum:
            case MVMDispOpcodeLoadAttributeStr:
            case MVMDispOpcodeSet:
            case MVMDispOpcodeResultValueObj:
            case MVMDispOpcodeResultValueInt:
            case MVMDispOpcodeUseArgsTail:
            case MVMDispOpcodeResultBytecode:
            case MVMDispOpcodeResultCFunction:
                break;
            default:
                MVM_spesh_graph_add_comment(tc, g, ins, "dispatch not compiled: op %s NYI",
                                MVM_disp_opcode_to_name(dp->ops[i].code));
                return NULL;
        }
    }

    /* We'll re-use the deopt annotation on the dispatch instruction for
     * the first guard, and then clone it later if needed. */
    MVMSpeshAnn *deopt_ann = take_dispatch_deopt_annotation(tc, g, ins);
    MVMuint32 reused_deopt_ann = 0;

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

    /* Visit the ops of the dispatch program and translate them. */
    MVMSpeshIns *insert_after = ins;
    MVMCallsite *callsite = NULL;
    MVMint32 skip_args = -1;
    for (i = 0; i < dp->num_ops; i++) {
        MVMDispProgramOp *op = &(dp->ops[i]);
        switch (op->code) {
            case MVMDispOpcodeGuardArgType:
                args[op->arg_guard.arg_idx] = emit_type_guard(tc, g, bb, &insert_after,
                        MVM_OP_sp_guard, args[op->arg_guard.arg_idx],
                        (MVMSTable *)dp->gc_constants[op->arg_guard.checkee],
                        deopt_ann, &reused_deopt_ann);
                break;
            case MVMDispOpcodeGuardArgTypeConc:
                args[op->arg_guard.arg_idx] = emit_type_guard(tc, g, bb, &insert_after,
                        MVM_OP_sp_guardconc, args[op->arg_guard.arg_idx],
                        (MVMSTable *)dp->gc_constants[op->arg_guard.checkee],
                        deopt_ann, &reused_deopt_ann);
                break;
            case MVMDispOpcodeGuardArgTypeTypeObject:
                args[op->arg_guard.arg_idx] = emit_type_guard(tc, g, bb, &insert_after,
                        MVM_OP_sp_guardtype, args[op->arg_guard.arg_idx],
                        (MVMSTable *)dp->gc_constants[op->arg_guard.checkee],
                        deopt_ann, &reused_deopt_ann);
                break;
            case MVMDispOpcodeLoadCaptureValue:
                /* We already have all the capture values in the arg registers
                 * so just alias. */
                temporaries[op->load.temp] = args[op->arg_guard.arg_idx];
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
                break;
            }
            case MVMDispOpcodeLoadConstantNum: {
                MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_num64);
                temporaries[op->load.temp] = temp;
                MVM_VECTOR_PUSH(allocated_temps, temp);
                MVMSpeshOperand constnum = { .lit_i64 = dp->constants[op->load.idx].n64 };
                emit_bi_op(tc, g, bb, &insert_after, MVM_OP_const_n64,
                    temporaries[op->load.temp], constnum);
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
                        break;
                    case MVM_OP_dispatch_i:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_unbox_i, ins->operands[0],
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
                    case MVM_OP_dispatch_n:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_ni, ins->operands[0],
                            temporaries[op->res_value.temp]);
                        break;
                    case MVM_OP_dispatch_s:
                        emit_bi_op(tc, g, bb, &insert_after, MVM_OP_coerce_si, ins->operands[0],
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
                    case MVM_OP_dispatch_n:
                        base_op = MVM_op_get_op(c ? MVM_OP_sp_runcfunc_n : MVM_OP_sp_runbytecode_n);
                        break;
                    case MVM_OP_dispatch_s:
                        base_op = MVM_op_get_op(c ? MVM_OP_sp_runcfunc_s : MVM_OP_sp_runbytecode_s);
                        break;
                    default:
                        MVM_oops(tc, "Unexpected dispatch op when translating bytecode result");
                }

                /* Form the varargs op and create the instruction. */
                MVMOpInfo *rb_op = MVM_spesh_disp_create_dispatch_op_info(tc, g,
                    base_op, callsite);
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

                /* Add the argument operands. */
                MVMuint16 j;
                for (j = 0; j < callsite->flag_count; j++) {
                    rb_ins->operands[cur_op] = args[skip_args + j];
                    MVM_spesh_usages_add_by_reg(tc, g, rb_ins->operands[cur_op], rb_ins);
                    cur_op++;
                }

                /* Insert the produced instruction. */
                MVM_spesh_manipulate_insert_ins(tc, bb, insert_after, rb_ins);
                insert_after = rb_ins;
                break;
            }
            default:
                /* Should never happen due to the validation earlier. */
                MVM_oops(tc, "Unexpectedly hit dispatch op that cannot be translated");
        }
    }

    /* Release temporaries. */
    for (i = 0; i < MVM_VECTOR_ELEMS(allocated_temps); i++)
        MVM_spesh_manipulate_release_temp_reg(tc, g, allocated_temps[i]);
    MVM_VECTOR_DESTROY(allocated_temps);

    /* Annotate start and end of translated dispatch program. */
    MVMSpeshIns *first_inserted = ins->next;
    MVM_spesh_graph_add_comment(tc, g, first_inserted,
            "Start of dispatch program translation");
    MVM_spesh_graph_add_comment(tc, g, insert_after,
            "End of dispatch program translation");

    /* Delete the dispatch instruction and return the first inserted
     * instruction as the next thing to optimize. Make sure we don't
     * mark it as having a dead writer (since we inserted a replacement
     * instruction above). */
    MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
    if (ins->info->opcode != MVM_OP_dispatch_v)
        MVM_spesh_get_facts(tc, g, ins->operands[0])->dead_writer = 0;
    return first_inserted;
}

/* Rewrite an unhit dispatch instruction. */
static MVMSpeshIns *rewrite_unhit(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins,
        MVMuint32 bytecode_offset) {
    MVM_spesh_graph_add_comment(tc, g, ins, "Never dispatched");
    rewrite_to_sp_dispatch(tc, g, ins, bytecode_offset);
    return ins;
}

/* Rewrite a dispatch instruction that is considered monomorphic. */
static MVMSpeshIns *rewrite_monomorphic(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins,
        MVMuint32 bytecode_offset, MVMuint32 outcome) {
    MVMDispInlineCache *cache = &(g->sf->body.inline_cache);
    MVMDispInlineCacheEntry *entry = cache->entries[bytecode_offset >> cache->bit_shift];
    MVMuint32 kind = MVM_disp_inline_cache_get_kind(tc, entry);
    if (kind == MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH) {
        MVMSpeshIns *result = translate_dispatch_program(tc, g, bb, ins,
                ((MVMDispInlineCacheEntryMonomorphicDispatch *)entry)->dp);
        if (result)
            return result;
    }
    MVM_spesh_graph_add_comment(tc, g, ins,
            "Deemed monomorphic (outcome %d, entry kind %d)", outcome, kind);
    rewrite_to_sp_dispatch(tc, g, ins, bytecode_offset);
    return ins;
}

/* Rewrite a dispatch instruction that is polymorphic or megamorphic. */
static MVMSpeshIns *rewrite_polymorphic(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins,
        MVMuint32 bytecode_offset, OutcomeHitCount *outcomes, MVMuint32 num_outcomes) {
    // TODO
    MVM_spesh_graph_add_comment(tc, g, ins, "Deemed polymorphic");
    rewrite_to_sp_dispatch(tc, g, ins, bytecode_offset);
    return ins;
}

/* Drives the overall process of optimizing a dispatch instruction. The instruction
 * will always recieve some transformation, even if it's simply to sp_dispatch_*,
 * which pre-resolves the inline cache (and so allows inlining of code that still
 * wants to reference the inline cache of the place it came from). */
static int compare_hits(const void *a, const void *b) {
    return ((OutcomeHitCount *)b)->hits - ((OutcomeHitCount *)a)->hits;
}
MVMSpeshIns *MVM_spesh_disp_optimize(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshPlanned *p,
        MVMSpeshIns *ins) {
    MVMSpeshIns *result = ins;
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

    /* Find any statistics we have on what outcomes were selected by the
     * dispatcher and aggregate them. */
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
    qsort(outcome_hits, MVM_VECTOR_ELEMS(outcome_hits), sizeof(OutcomeHitCount), compare_hits);


    /* If there are no hits, we can only rewrite it to sp_dispatch. */
    if (MVM_VECTOR_ELEMS(outcome_hits) == 0)
        result = rewrite_unhit(tc, g, bb, ins, bytecode_offset);

    /* If there's one hit, *or* the top hit has > 99% of the total hits, then we
     * rewrite it to monomorphic. */
    else if ((100 * outcome_hits[0].hits) / total_hits >= 99)
        result = rewrite_monomorphic(tc, g, bb, ins, bytecode_offset, outcome_hits[0].outcome);

    /* Otherwise, it's polymoprhic or megamorphic. */
    else
        result = rewrite_polymorphic(tc, g, bb, ins, bytecode_offset, outcome_hits, MVM_VECTOR_ELEMS(outcome_hits));

    MVM_VECTOR_DESTROY(outcome_hits);
    return result;
}
