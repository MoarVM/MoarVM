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

static MVMuint32 find_disp_op_first_real_arg(MVMThreadContext *tc, MVMSpeshIns *ins) {
    if (ins->info->opcode == MVM_OP_dispatch_v) {
        return 2;
    }
    else if (ins->info->opcode == MVM_OP_dispatch_i || 
            ins->info->opcode == MVM_OP_dispatch_n || 
            ins->info->opcode == MVM_OP_dispatch_s || 
            ins->info->opcode == MVM_OP_dispatch_o) {
        return 3;
    }
    return 0;
}
/* XXX stolen from spesh/facts.c */
static void copy_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand to, MVMSpeshOperand from) {
    MVMSpeshFacts *tfacts = &g->facts[to.reg.orig][to.reg.i];
    MVMSpeshFacts *ffacts = &g->facts[from.reg.orig][from.reg.i];
    tfacts->flags         = ffacts->flags;
    tfacts->type          = ffacts->type;
    tfacts->decont_type   = ffacts->decont_type;
    tfacts->value         = ffacts->value;
    tfacts->log_guards    = ffacts->log_guards;
    tfacts->num_log_guards = ffacts->num_log_guards;
}
/* XXX Stolen from spesh/optimize.c */
static void find_deopt_target_and_index(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
                                 MVMuint32 *deopt_target_out, MVMuint32 *deopt_index_out) {
    MVMSpeshAnn *deopt_ann = ins->annotations;
    while (deopt_ann) {
        if (deopt_ann->type == MVM_SPESH_ANN_DEOPT_ONE_INS) {
            *deopt_target_out = g->deopt_addrs[2 * deopt_ann->data.deopt_idx];
            *deopt_index_out = deopt_ann->data.deopt_idx;
            return;
        }
        deopt_ann = deopt_ann->next;
    }
    MVM_panic(1, "Spesh: unexpectedly missing deopt annotation on prepargs");
}
static void add_synthetic_deopt_annotation(MVMThreadContext *tc, MVMSpeshGraph *g,
                                           MVMSpeshIns *ins, MVMuint32 deopt_index) {
    MVMSpeshAnn *ann = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
    ann->type = MVM_SPESH_ANN_DEOPT_SYNTH;
    ann->data.deopt_idx = deopt_index;
    ann->next = ins->annotations;
    ins->annotations = ann;
}
static MVMSpeshOperand insert_arg_type_guard(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                  MVMSpeshOperand operand, MVMSpeshIns *insert_pos,
                                  MVMint8 concness, MVMSTable *type,
                                  MVMuint32 add_comment) {
    MVMuint32 deopt_target, deopt_index, new_deopt_index;

    MVMuint8 has_type = type != NULL;

    /* Split the SSA version of the arg. */
    MVMSpeshOperand guard_reg = MVM_spesh_manipulate_split_version(tc, g,
            operand, bb, insert_pos);

    MVMSpeshFacts *guard_facts;

    /* Insert guard before prepargs (this means they stack up in order). */
    MVMSpeshIns *guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(
            concness == -1 ? (has_type ? MVM_OP_sp_guardtype : MVM_OP_sp_guardjusttype) :
            concness ==  0 ? (has_type ? MVM_OP_sp_guard : MVM_OP_no_op) :
            concness ==  1 ? (has_type ? MVM_OP_sp_guardconc : MVM_OP_sp_guardjustconc) : MVM_OP_no_op
        );
    guard->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
    guard->operands[0] = guard_reg;
    guard->operands[1] = operand;
    if (has_type)
        guard->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
            (MVMCollectable *)type);
    else
        guard->operands[2].lit_i16 = 0;
    find_deopt_target_and_index(tc, g, insert_pos, &deopt_target, &deopt_index);

    MVM_spesh_manipulate_insert_ins(tc, bb, insert_pos->prev, guard);
    MVM_spesh_usages_add_by_reg(tc, g, operand, guard);
    guard_facts = MVM_spesh_get_facts(tc, g, guard_reg);
    guard_facts->writer = guard;

    if (has_type) {
        guard_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
        guard_facts->type   = type->WHAT;
        MVM_spesh_graph_add_comment(tc, g, guard, "set facts to known type on %d(%d)", guard_reg.reg.orig, guard_reg.reg.i);
    }
    if (concness == -1) {
        guard_facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
        MVM_spesh_graph_add_comment(tc, g, guard, "set facts to known typeobject on %d(%d)", guard_reg.reg.orig, guard_reg.reg.i);
    }
    else if (concness == 1) {
        guard_facts->flags |= MVM_SPESH_FACT_CONCRETE;
        MVM_spesh_graph_add_comment(tc, g, guard, "set facts to known concrete on %d(%d)", guard_reg.reg.orig, guard_reg.reg.i);
    }

    if (add_comment) {
        if (has_type)
            MVM_spesh_graph_add_comment(tc, g, guard, "inserted dispatch program guard %d(%d) <- %d(%d) for type %s",
                    guard_reg.reg.orig, guard_reg.reg.i,
                    operand.reg.orig,   operand.reg.i,
                    MVM_6model_get_stable_debug_name(tc, type));
        else
            MVM_spesh_graph_add_comment(tc, g, guard, "inserted dispatch program guard");
    }

    /* Also give the instruction a deopt annotation, and related it to the
     * one on the prepargs. */
    new_deopt_index = MVM_spesh_graph_add_deopt_annotation(tc, g, guard, deopt_target,
        MVM_SPESH_ANN_DEOPT_ONE_INS);
    guard->operands[3].lit_ui32 = new_deopt_index;
    add_synthetic_deopt_annotation(tc, g, guard, deopt_index);

    return guard_reg;
}
static MVMSpeshIns *rewrite_dispatch_program(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins,
        MVMDispProgram *dp) {
    MVMuint32 i;
    MVMSpeshOperand *args = &ins->operands[find_disp_op_first_real_arg(tc, ins)];
    MVMSpeshOperand *temporaries = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * dp->num_temporaries);
    MVMSpeshIns *resume_optimization_point = ins->prev;

    if (dp->first_args_temporary != dp->num_temporaries) {
        fprintf(stderr, "----\nignoring program with first_args_temporary != num_temporaries\n----\n\n");
        return NULL;
    }
    fprintf(stderr, "----\nbuilding spesh code for disp program\n----\n\n");

    MVM_spesh_graph_add_comment(tc, g, ins->prev ? ins->prev : bb->first_ins, "Rewritten from a %s op", ins->info->name);

    for (i = 0; i < dp->num_temporaries; i++) {
        /* the "type" of the reg isn't explicit in the program's metadata, it's
         * implicit in the way it's used.
         * XXX just go with objects for temporaries
         */
        temporaries[i] = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
    }

    for (i = 0; i < dp->num_ops; i++) {
        MVMDispProgramOp *op = &(dp->ops[i]);
        switch (op->code) {
            case MVMDispOpcodeGuardArgType:
                fprintf(stderr, "  Guard arg %d (type=%s)\n",
                        op->arg_guard.arg_idx,
                        ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee])->debug_name);
                goto nyi;
                break;
            case MVMDispOpcodeGuardArgTypeConc:
                fprintf(stderr, "  Guard arg %d (type=%s, concrete)\n",
                        op->arg_guard.arg_idx,
                        ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee])->debug_name);
                args[op->arg_guard.arg_idx] =
                    insert_arg_type_guard(tc, g, bb,
                            args[op->arg_guard.arg_idx], ins,
                            1, ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee]),
                            1);
                break;
           case MVMDispOpcodeGuardArgTypeTypeObject:
                fprintf(stderr, "  Guard arg %d (type=%s, type object)\n",
                        op->arg_guard.arg_idx,
                        ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee])->debug_name);
                goto nyi;
                break;
            case MVMDispOpcodeGuardArgConc:
                fprintf(stderr, "  Guard arg %d (concrete)\n",
                        op->arg_guard.arg_idx);
                goto nyi;
                break;
            case MVMDispOpcodeGuardArgTypeObject:
                fprintf(stderr, "  Guard arg %d (type object)\n",
                        op->arg_guard.arg_idx);
                goto nyi;
                break;
            case MVMDispOpcodeGuardArgLiteralObj:
                fprintf(stderr, "  Guard arg %d (literal object of type %s)\n",
                        op->arg_guard.arg_idx,
                        STABLE(((MVMObject *)dp->gc_constants[op->arg_guard.checkee]))->debug_name);
                goto nyi;
                break;
            case MVMDispOpcodeGuardArgLiteralStr: {
                char *c_str = MVM_string_utf8_encode_C_string(tc, 
                        ((MVMString *)dp->gc_constants[op->arg_guard.checkee]));
                fprintf(stderr, "  Guard arg %d (literal string '%s')\n",
                        op->arg_guard.arg_idx, c_str);
                MVM_free(c_str);
                goto nyi;
                break;
            }
            case MVMDispOpcodeGuardArgLiteralInt:
                fprintf(stderr, "  Guard arg %d (literal integer %"PRIi64")\n",
                        op->arg_guard.arg_idx,
                        dp->constants[op->arg_guard.checkee].i64);
                goto nyi;
                break;
            case MVMDispOpcodeGuardArgLiteralNum:
                fprintf(stderr, "  Guard arg %d (literal number %g)\n",
                        op->arg_guard.arg_idx,
                        dp->constants[op->arg_guard.checkee].n64);
                goto nyi;
                break;
            case MVMDispOpcodeGuardArgNotLiteralObj:
                fprintf(stderr, "  Guard arg %d (not literal object of type %s)\n",
                        op->arg_guard.arg_idx,
                        STABLE(((MVMObject *)dp->gc_constants[op->arg_guard.checkee]))->debug_name);
                goto nyi;
                break;
            case MVMDispOpcodeLoadCaptureValue: {
                MVMSpeshIns *set_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                fprintf(stderr, "  adding a set op\n");
                set_ins->info = MVM_op_get_op(MVM_OP_set);
                set_ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * 2);
                set_ins->operands[0] = temporaries[op->load.temp];
                set_ins->operands[1] = args[op->arg_guard.arg_idx];
                MVM_spesh_usages_add_by_reg(tc, g, set_ins->operands[1], set_ins);
                MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, set_ins);
                break;
            }
            case MVMDispOpcodeResultValueObj: {
                MVMSpeshIns *set_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                MVMSpeshFacts *target_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                fprintf(stderr, "  Set result object value from temporary %d\n",
                        op->res_value.temp);
                set_ins->info = MVM_op_get_op(MVM_OP_set);
                set_ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * 2);
                set_ins->operands[0] = ins->operands[0];
                set_ins->operands[1] = temporaries[op->res_value.temp];
                copy_facts(tc, g, set_ins->operands[0], set_ins->operands[1]);
                MVM_spesh_graph_add_comment(tc, g, set_ins, "set instruction from ResultValueObj");
                MVM_spesh_usages_add_by_reg(tc, g, set_ins->operands[1], set_ins);
                MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, set_ins);
                MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
                target_facts->writer = set_ins;
                return resume_optimization_point;
            }
            default:
                fprintf(stderr, "  .... no %d.\n", op->code);
                return NULL;
        }
    }
nyi:
    fprintf(stderr, " NYI, sorry!\n");
cleanup:
    for (i = 0; i < dp->num_temporaries; i++) {
        /* the "type" of the reg isn't explicit in the program's metadata, it's
         * implicit in the way it's used.
         * XXX just go with objects for temporaries
         */
        MVM_spesh_manipulate_release_temp_reg(tc, g, temporaries[i]);
    }
    return NULL;
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
    MVMuint32 disp_slot = MVM_disp_inline_cache_get_slot(tc, g->sf, bytecode_offset);
    MVMDispInlineCache *cache = &(g->sf->body.inline_cache);
    MVMDispInlineCacheEntry *entry = cache->entries[bytecode_offset >> cache->bit_shift];
    MVMuint32 kind = MVM_disp_inline_cache_get_kind(tc, entry);
    if (kind == MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH) {
        MVMSpeshIns *result = rewrite_dispatch_program(tc, g, bb, ins, ((MVMDispInlineCacheEntryMonomorphicDispatch *)entry)->dp);
        if (result)
            return result;
    }
    MVM_spesh_graph_add_comment(tc, g, ins, "Deemed monomorphic (outcome %d, entry kind %d)", outcome, kind);
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
                for (k = 0; k < ts->by_offset[j].num_plugin_guards; k++) {
                    MVMSpeshStatsPluginGuardCount *outcome_count =
                            &(ts->by_offset[j].plugin_guards[k]);
                    MVMuint32 l;
                    MVMuint32 found = 0;
                    for (l = 0; l < MVM_VECTOR_ELEMS(outcome_hits); l++) {
                        if (outcome_hits[l].outcome == outcome_count->guard_index) {
                            outcome_hits[l].hits += outcome_count->count;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        OutcomeHitCount ohc = {
                            .outcome = outcome_count->guard_index,
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
