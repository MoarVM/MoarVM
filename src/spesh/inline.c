#include "moar.h"

/* Ensures that a given compilation unit has access to the specified extop. */
static void demand_extop(MVMThreadContext *tc, MVMCompUnit *target_cu, MVMCompUnit *source_cu,
                         const MVMOpInfo *info) {
    MVMExtOpRecord *extops;
    MVMuint16 i, num_extops;

    uv_mutex_lock(target_cu->body.inline_tweak_mutex);

    /* See if the target compunit already has the extop. */
    extops     = target_cu->body.extops;
    num_extops = target_cu->body.num_extops;
    for (i = 0; i < num_extops; i++)
        if (extops[i].info == info) {
            uv_mutex_unlock(target_cu->body.inline_tweak_mutex);
            return;
        }

    /* If not, need to add it. Locate it in the source CU. */
    extops     = source_cu->body.extops;
    num_extops = source_cu->body.num_extops;
    for (i = 0; i < num_extops; i++) {
        if (extops[i].info == info) {
            MVMuint32 orig_size = target_cu->body.num_extops * sizeof(MVMExtOpRecord);
            MVMuint32 new_size = (target_cu->body.num_extops + 1) * sizeof(MVMExtOpRecord);
            MVMExtOpRecord *new_extops = MVM_fixed_size_alloc(tc,
                tc->instance->fsa, new_size);
            memcpy(new_extops, target_cu->body.extops, orig_size);
            memcpy(&new_extops[target_cu->body.num_extops], &extops[i], sizeof(MVMExtOpRecord));
            if (target_cu->body.extops)
                MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa, orig_size,
                   target_cu->body.extops);
            target_cu->body.extops = new_extops;
            target_cu->body.num_extops++;
            uv_mutex_unlock(target_cu->body.inline_tweak_mutex);
            return;
        }
    }

    /* Didn't find it; should be impossible. */
    uv_mutex_unlock(target_cu->body.inline_tweak_mutex);
    MVM_oops(tc, "Spesh: inline failed to find source CU extop entry");
}

/* Considers a static frame and decides if it's possible to inline it into the
 * current inliner. If there are no blockers, non-zero. Otherwise, sets the
 * reason for not inlining and returns zero. */
static int is_static_frame_inlineable(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                                      MVMStaticFrame *target_sf, char **no_inline_reason) {
    /* Check inlining is enabled. */
    if (!tc->instance->spesh_inline_enabled) {
        *no_inline_reason = "inlining is disabled";
        return 0;
    }

    /* Check frame is not marked as not being allowed to be inlined. */
    if (target_sf->body.no_inline) {
        *no_inline_reason = "the frame is marked as no-inline";
        return 0;
    }

    /* Ensure that this isn't a recursive inlining. */
    if (target_sf == inliner->sf) {
        *no_inline_reason = "recursive calls cannot be inlined";
        return 0;
    }

    /* Ensure it has no state vars (these need the setup code in frame
     * invoke). */
    if (target_sf->body.has_state_vars) {
        *no_inline_reason = "cannot inline code that declares a state variable";
        return 0;
    }

    /* Ensure it's not a thunk (need to skip over those in exception search). */
    if (target_sf->body.is_thunk) {
        *no_inline_reason = "cannot inline code marked as a thunk";
        return 0;
    }

    return 1;
}

/* Checks a spesh graph to see if it contains any uninlineable elements. If
 * it is possbile to inline, returns non-zero. Otherwise, sets no_inline_reason
 * and returns zero. Optionally builds up usage counts on the graph. */
static int is_graph_inlineable(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                               MVMStaticFrame *target_sf, MVMSpeshIns *invoke_ins,
                               MVMSpeshGraph *ig, char **no_inline_reason) {
    MVMSpeshBB *bb = ig->entry;
    MVMint32 same_hll = target_sf->body.cu->body.hll_config ==
            inliner->sf->body.cu->body.hll_config;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            MVMint32 opcode = ins->info->opcode;
            MVMint32 is_phi = opcode == MVM_SSA_PHI;

            /* Instruction may be marked directly as not being inlinable, in
             * which case we're done. */
            if (!is_phi && ins->info->no_inline) {
                *no_inline_reason = "target has a :noinline instruction";
                return 0;
            }

            /* XXX TODO: See if the workaround below is still required */
            if (opcode == MVM_OP_throwpayloadlexcaller && tc->instance->profiling) {
                *no_inline_reason = "target has throwpayloadlexcaller, which currently causes problems when profiling is on";
                return 0;
            }

            /* If we don't have the same HLL and there's a :useshll op, we
             * cannot inline. */
            if (!same_hll && ins->info->uses_hll) {
                *no_inline_reason = "target has a :useshll instruction and HLLs are different";
                return 0;
            }

            /* If we have an invoke_o, but a return_[ins] that would require
             * boxing, we can't inline if it's not the same HLL. */
            if (!same_hll && invoke_ins->info->opcode == MVM_OP_invoke_o) {
                switch (opcode) {
                    case MVM_OP_return_i:
                    case MVM_OP_return_n:
                    case MVM_OP_return_s:
                        *no_inline_reason = "target needs a return boxing and HLLs are different";
                        return 0;
                }
            }

            /* If we have lexical bind, make sure it's within the frame. */
            if (opcode == MVM_OP_bindlex) {
                if (ins->operands[0].lex.outers > 0) {
                    *no_inline_reason = "target has bind to outer lexical";
                    return 0;
                }
            }

            /* Check we don't have too many args for inlining to work out. */
            else if (opcode == MVM_OP_sp_getarg_o ||
                    opcode == MVM_OP_sp_getarg_i ||
                    opcode == MVM_OP_sp_getarg_n ||
                    opcode == MVM_OP_sp_getarg_s) {
                if (ins->operands[1].lit_i16 >= MAX_ARGS_FOR_OPT) {
                    *no_inline_reason = "too many arguments to inline";
                    return 0;
                }
            }

            /* Ext-ops need special care in inter-comp-unit inlines. */
            if (opcode == (MVMuint16)-1) {
                MVMCompUnit *target_cu = inliner->sf->body.cu;
                MVMCompUnit *source_cu = target_sf->body.cu;
                if (source_cu != target_cu)
                    demand_extop(tc, target_cu, source_cu, ins->info);
            }

            ins = ins->next;
        }
        bb = bb->linear_next;
    }

    return 1;
}

/* Sees if it will be possible to inline the target code ref, given we could
 * already identify a spesh candidate. Returns NULL if no inlining is possible
 * or a graph ready to be merged if it will be possible. */
MVMSpeshGraph * MVM_spesh_inline_try_get_graph(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                                               MVMStaticFrame *target_sf,
                                               MVMSpeshCandidate *cand,
                                               MVMSpeshIns *invoke_ins,
                                               char **no_inline_reason) {
    MVMSpeshGraph *ig;

    /* Check bytecode size is within the inline limit. */
    if (cand->bytecode_size > MVM_SPESH_MAX_INLINE_SIZE &&
            target_sf->body.bytecode_size > MVM_SPESH_MAX_INLINE_SIZE) {
        *no_inline_reason = "bytecode is too large to inline";
        return NULL;
    }

    /* Check the target is suitable for inlining. */
    if (!is_static_frame_inlineable(tc, inliner, target_sf, no_inline_reason))
        return NULL;

    /* Build graph from the already-specialized bytecode and check if we can
     * inline the graph. */
    ig = MVM_spesh_graph_create_from_cand(tc, target_sf, cand, 0);
    if (is_graph_inlineable(tc, inliner, target_sf, invoke_ins, ig, no_inline_reason)) {
        /* We can inline it. Do facts discovery, which also sets usage counts.
         * We also need to bump counts for any inline's code_ref_reg to make
         * sure it stays available for deopt. */
        MVMuint32 i;
        MVM_spesh_facts_discover(tc, ig, NULL, 1);
        for (i = 0; i < ig->num_inlines; i++) {
            /* We can't be very precise about this, because we don't know the
             * SSA version in effect. So bump usages of all version of the
             * register as a conservative solution. */
            MVMuint16 reg = ig->inlines[i].code_ref_reg;
            MVMuint32 j;
            for (j = 0; j < ig->fact_counts[reg]; j++)
                MVM_spesh_usages_add_unconditional_deopt_usage(tc, ig, &(ig->facts[reg][j]));
        }
        return ig;
    }
    else {
        /* Not possible to inline. Clear it up. */
        MVM_free(ig->spesh_slots);
        MVM_spesh_graph_destroy(tc, ig);
        return NULL;
    }
}

/* Tries to get a spesh graph for a particular unspecialized candidate. */
MVMSpeshGraph * MVM_spesh_inline_try_get_graph_from_unspecialized(MVMThreadContext *tc,
        MVMSpeshGraph *inliner, MVMStaticFrame *target_sf, MVMSpeshIns *invoke_ins,
        MVMSpeshCallInfo *call_info, char **no_inline_reason) {
    MVMSpeshGraph *ig;

    /* Cannot inline with flattening args. */
    if (call_info->cs->has_flattening) {
        *no_inline_reason = "callsite has flattening args";
        return NULL;
    }

    /* Check the target is suitable for inlining. */
    if (!is_static_frame_inlineable(tc, inliner, target_sf, no_inline_reason))
        return NULL;

    /* Build the spesh graph from bytecode, transform args, do facts discovery
     * (setting usage counts) and optimize. We do this before checking if it
     * is inlineable as we can get rid of many :noinline ops (especially in
     * the args specialization). */
    ig = MVM_spesh_graph_create(tc, target_sf, 0, 1);
    MVM_spesh_args_from_callinfo(tc, ig, call_info);
    MVM_spesh_facts_discover(tc, ig, NULL, 0);
    MVM_spesh_optimize(tc, ig, NULL);

    /* See if it's inlineable; clean up if not. */
    if (is_graph_inlineable(tc, inliner, target_sf, invoke_ins, ig, no_inline_reason)) {
        return ig;
    }
    else {
        MVM_free(ig->spesh_slots);
        MVM_spesh_graph_destroy(tc, ig);
        return NULL;
    }
}

/* Finds the deopt index of the return. */
static MVMint32 return_deopt_idx(MVMThreadContext *tc, MVMSpeshIns *invoke_ins) {
    MVMSpeshAnn *ann = invoke_ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_DEOPT_ALL_INS)
            return ann->data.deopt_idx;
        ann = ann->next;
    }
    MVM_oops(tc, "Spesh inline: return_deopt_idx failed");
}

/* The following routines fix references to per-compilation-unit things
 * that would be broken by inlining. */
static void fix_callsite(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                         MVMSpeshGraph *inlinee, MVMSpeshOperand *to_fix) {
    to_fix->callsite_idx = MVM_cu_callsite_add(tc, inliner->sf->body.cu,
        inlinee->sf->body.cu->body.callsites[to_fix->callsite_idx]);
}
static void fix_coderef(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                        MVMSpeshGraph *inlinee, MVMSpeshIns *to_fix) {
    MVM_oops(tc, "Spesh inline: fix_coderef NYI");
}
static void fix_const_str(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                    MVMSpeshGraph *inlinee, MVMSpeshIns *to_fix) {
    MVMCompUnit *cu = inlinee->sf->body.cu;
    MVMuint16 sslot;
    MVMSpeshOperand dest = to_fix->operands[0];
    MVMuint16 str_idx = to_fix->operands[1].lit_str_idx;

    sslot = MVM_spesh_add_spesh_slot_try_reuse(tc, inlinee, (MVMCollectable *)cu);

    /*fprintf(stderr, "fixed up string get to use spesh slot %u\n", sslot);*/

    to_fix->info = MVM_op_get_op(MVM_OP_sp_getstringfrom);
    to_fix->operands = MVM_spesh_alloc(tc, inliner, 3 * sizeof(MVMSpeshOperand));
    to_fix->operands[0] = dest;
    to_fix->operands[1].lit_i16 = sslot;
    to_fix->operands[2].lit_str_idx = str_idx;
}
static void fix_str(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                    MVMSpeshGraph *inlinee, MVMSpeshOperand *to_fix) {
    to_fix->lit_str_idx = MVM_cu_string_add(tc, inliner->sf->body.cu,
        MVM_cu_string(tc, inlinee->sf->body.cu, to_fix->lit_str_idx));
}
static void fix_wval(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                     MVMSpeshGraph *inlinee, MVMSpeshIns *to_fix) {
    /* We have three different ways to make a wval refer to the right
     * object after an inline:
     * - If a simple change of the dep parameter lets us refer to the
     *   same SC in the inliner's dependency list, just change that.
     * - If the object had already been deserialized, we can just
     *   stash it away in a spesh slot.
     * - If the object hadn't been deserialized yet, an indirect lookup
     *   via a spesh slot holding the actual SC is possible.
     *
     * The last option is needed because deserializing objects causes
     * allocations, which lead to GC, which is not supported inside
     * the spesh process.
     */
    MVMCompUnit *targetcu = inliner->sf->body.cu;
    MVMCompUnit *sourcecu = inlinee->sf->body.cu;
    MVMint16     dep      = to_fix->operands[1].lit_i16;
    MVMint64     idx      = to_fix->info->opcode == MVM_OP_wval
        ? to_fix->operands[2].lit_i16
        : to_fix->operands[2].lit_i64;
    if (dep >= 0 && dep < sourcecu->body.num_scs) {
        MVMSerializationContext *sc = MVM_sc_get_sc(tc, sourcecu, dep);
        if (sc) {
            MVMuint16 otherdep;
            for (otherdep = 0; otherdep < targetcu->body.num_scs; otherdep++) {
                MVMSerializationContext *othersc = MVM_sc_get_sc(tc, targetcu, otherdep);
                if (sc == othersc) {
                    to_fix->operands[1].lit_i16 = otherdep;
                    return;
                }
            }
            if (MVM_sc_is_object_immediately_available(tc, sc, idx)) {
                MVMint16 ss  = MVM_spesh_add_spesh_slot_try_reuse(tc, inliner, (MVMCollectable *)MVM_sc_get_object(tc, sc, idx));
                to_fix->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
                to_fix->operands[1].lit_i16 = ss;
            }
            else {
                MVMint16 ss  = MVM_spesh_add_spesh_slot_try_reuse(tc, inliner, (MVMCollectable *)sc);
                to_fix->info = MVM_op_get_op(MVM_OP_sp_getwvalfrom);
                to_fix->operands[1].lit_i16 = ss;
                to_fix->operands[2].lit_i64 = idx;
            }
        }
        else {
            MVM_oops(tc,
                "Spesh inline: SC not yet resolved; lookup failed");
        }
    }
    else {
        MVM_oops(tc,
            "Spesh inline: invalid SC index %d found", dep);
    }
}

/* Resizes the handlers table, making a copy if needed. */
static void resize_handlers_table(MVMThreadContext *tc, MVMSpeshGraph *inliner, MVMuint32 new_handler_count) {
    if (inliner->handlers == inliner->sf->body.handlers) {
        /* Original handlers table; need a copy. */
        MVMFrameHandler *new_handlers = MVM_malloc(new_handler_count * sizeof(MVMFrameHandler));
        if (inliner->handlers)
            memcpy(new_handlers, inliner->handlers,
                inliner->num_handlers * sizeof(MVMFrameHandler));
        inliner->handlers = new_handlers;
    }
    else {
        /* Probably already did some inlines into this frame; resize. */
        inliner->handlers = MVM_realloc(inliner->handlers,
            new_handler_count * sizeof(MVMFrameHandler));
    }
}

/* Rewrites a lexical lookup to an outer to be done via. a register holding
 * the outer coderef. */
static void rewrite_outer_lookup(MVMThreadContext *tc, MVMSpeshGraph *g,
                                 MVMSpeshIns *ins, MVMuint16 num_locals,
                                 MVMuint16 op, MVMSpeshOperand code_ref_reg) {
    MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
    new_operands[0] = ins->operands[0];
    new_operands[0].reg.orig += num_locals;
    new_operands[1].lit_ui16 = ins->operands[1].lex.idx;
    new_operands[2].lit_ui16 = ins->operands[1].lex.outers;
    new_operands[3] = code_ref_reg;
    ins->info = MVM_op_get_op(op);
    ins->operands = new_operands;
    MVM_spesh_usages_add_by_reg(tc, g, code_ref_reg, ins);
}

static void rewrite_hlltype(MVMThreadContext *tc, MVMSpeshGraph *inlinee, MVMSpeshIns *ins) {
    MVMObject *selected_type;
    MVMHLLConfig *hll = inlinee->sf->body.cu->body.hll_config;
    MVMSpeshOperand *old_ops = ins->operands;
    MVMuint16 sslot;

    switch (ins->info->opcode) {
        case MVM_OP_hllboxtype_i: selected_type = hll->int_box_type; break;
        case MVM_OP_hllboxtype_n: selected_type = hll->num_box_type; break;
        case MVM_OP_hllboxtype_s: selected_type = hll->str_box_type; break;
        case MVM_OP_hlllist:      selected_type = hll->slurpy_array_type; break;
        case MVM_OP_hllhash:      selected_type = hll->slurpy_hash_type; break;
        default: MVM_oops(tc, "unhandled instruction %s in rewrite_hlltype", ins->info->name);
    }

    sslot = MVM_spesh_add_spesh_slot_try_reuse(tc, inlinee, (MVMCollectable *)selected_type);
    ins->operands = MVM_spesh_alloc(tc, inlinee, 2 * sizeof(MVMSpeshOperand));
    ins->operands[0] = old_ops[0];
    ins->operands[1].lit_i16 = sslot;
    ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
}

/* Merges the inlinee's spesh graph into the inliner. */
MVMSpeshBB * merge_graph(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                 MVMSpeshGraph *inlinee, MVMStaticFrame *inlinee_sf,
                 MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins,
                 MVMSpeshOperand code_ref_reg,
                 MVMuint32 *inline_boundary_handler) {
    MVMSpeshFacts **merged_facts;
    MVMuint16      *merged_fact_counts;
    MVMint32        i, orig_inlines, total_inlines, orig_deopt_addrs;
    MVMuint32       total_handlers = inliner->num_handlers + inlinee->num_handlers + 1;
    MVMSpeshBB     *inlinee_first_bb = NULL, *inlinee_last_bb = NULL;
    MVMuint8        may_cause_deopt = 0;

    /* If the inliner and inlinee are from different compilation units, we
     * potentially have to fix up extra things. */
    MVMint32 same_comp_unit = inliner->sf->body.cu == inlinee->sf->body.cu;

    MVMint32 same_hll = same_comp_unit || inliner->sf->body.cu->body.hll_config ==
            inlinee_sf->body.cu->body.hll_config;

    /* Renumber the locals, lexicals, and basic blocks of the inlinee; also
     * re-write any indexes in annotations that need it. */
    MVMSpeshBB *bb = inlinee->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            MVMuint16    opcode = ins->info->opcode;
            MVMSpeshAnn *ann    = ins->annotations;
            while (ann) {
                switch (ann->type) {
                case MVM_SPESH_ANN_DEOPT_ONE_INS:
                case MVM_SPESH_ANN_DEOPT_ALL_INS:
                case MVM_SPESH_ANN_DEOPT_INLINE:
                case MVM_SPESH_ANN_DEOPT_OSR:
                    ann->data.deopt_idx += inliner->num_deopt_addrs;
                    break;
                case MVM_SPESH_ANN_INLINE_START:
                case MVM_SPESH_ANN_INLINE_END:
                    ann->data.inline_idx += inliner->num_inlines;
                    break;
                }
                ann = ann->next;
            }

            if (ins->info->may_cause_deopt)
                may_cause_deopt = 1;

            if (opcode == MVM_SSA_PHI) {
                for (i = 0; i < ins->info->num_operands; i++)
                    ins->operands[i].reg.orig += inliner->num_locals;
            }
            else if (opcode == MVM_OP_sp_getlex_o && ins->operands[1].lex.outers > 0) {
                rewrite_outer_lookup(tc, inliner, ins, inliner->num_locals,
                    MVM_OP_sp_getlexvia_o, code_ref_reg);
            }
            else if (opcode == MVM_OP_sp_getlex_ins && ins->operands[1].lex.outers > 0) {
                rewrite_outer_lookup(tc, inliner, ins, inliner->num_locals,
                    MVM_OP_sp_getlexvia_ins, code_ref_reg);
            }
            else if (opcode == MVM_OP_getlex && ins->operands[1].lex.outers > 0) {
                MVMuint16 outers = ins->operands[1].lex.outers;
                MVMStaticFrame *outer = inlinee_sf;
                while (outers--)
                    outer = outer->body.outer;
                if (outer->body.lexical_types[ins->operands[1].lex.idx] == MVM_reg_obj)
                    rewrite_outer_lookup(tc, inliner, ins, inliner->num_locals,
                        MVM_OP_sp_getlexvia_o, code_ref_reg);
                else
                    rewrite_outer_lookup(tc, inliner, ins, inliner->num_locals,
                        MVM_OP_sp_getlexvia_ins, code_ref_reg);
            }
            else {
                if (!same_comp_unit) {
                    if (ins->info->opcode == MVM_OP_const_s) {
                        fix_const_str(tc, inliner, inlinee, ins);
                    }
                    if (!same_hll &&
                            (opcode == MVM_OP_hllboxtype_i || opcode == MVM_OP_hllboxtype_n || opcode == MVM_OP_hllboxtype_s
                             || opcode == MVM_OP_hlllist || opcode == MVM_OP_hllhash)) {
                        rewrite_hlltype(tc, inlinee, ins);
                    }
                }

                for (i = 0; i < ins->info->num_operands; i++) {
                    MVMuint8 flags = ins->info->operands[i];
                    switch (flags & MVM_operand_rw_mask) {
                    case MVM_operand_read_reg:
                    case MVM_operand_write_reg:
                        ins->operands[i].reg.orig += inliner->num_locals;
                        break;
                    case MVM_operand_read_lex:
                    case MVM_operand_write_lex:
                        ins->operands[i].lex.idx += inliner->num_lexicals;
                        break;
                    default: {
                        MVMuint32 type = flags & MVM_operand_type_mask;
                        if (type == MVM_operand_spesh_slot) {
                            ins->operands[i].lit_i16 += inliner->num_spesh_slots;
                        }
                        else if (type == MVM_operand_coderef) {
                            if (!same_comp_unit)
                                /* NYI, but apparently this never happens?! */
                                fix_coderef(tc, inliner, inlinee, ins);
                        }
                        else if (type == MVM_operand_str) {
                            if (!same_comp_unit)
                                fix_str(tc, inliner, inlinee, &(ins->operands[i]));
                        }
                        else if (type == MVM_operand_callsite) {
                            if (!same_comp_unit)
                                fix_callsite(tc, inliner, inlinee, &(ins->operands[i]));
                        }
                        break;
                        }
                    }
                }
            }

            ins = ins->next;
        }
        bb->idx += inliner->num_bbs - 1; /* -1 as we won't include entry */
        bb->inlined = 1;
        if (!bb->linear_next)
            inlinee_last_bb = bb;
        bb = bb->linear_next;
    }

    /* If we saw that we may deopt anywhere, mark the basic blocks of the
     * inline as maybe deopting; this prevents us doing too aggressive
     * optimizations within them. */
    if (may_cause_deopt) {
        bb = inlinee->entry;
        while (bb) {
            bb->inlined_may_cause_deopt = 1;
            bb = bb->linear_next;
        }
    }

    /* Link inlinee BBs into the linear next chain. */
    bb = invoke_bb->linear_next;
    invoke_bb->linear_next = inlinee_first_bb = inlinee->entry->linear_next;
    inlinee_last_bb->linear_next = bb;

    bb = NULL;

    /* Make all of the inlinee's entry block's successors (except the linear
     * next) also be successors of the inliner's entry block; this keeps any
     * exception handlers alive in the graph. */
    while (inlinee->entry->num_succ > 1) {
        MVMSpeshBB *move = inlinee->entry->succ[0] == inlinee->entry->linear_next
            ? inlinee->entry->succ[1]
            : inlinee->entry->succ[0];
        MVM_spesh_manipulate_remove_successor(tc, inlinee->entry, move);
        MVM_spesh_manipulate_add_successor(tc, inliner, inliner->entry, move);
    }

    /* Merge facts. */
    merged_facts = MVM_spesh_alloc(tc, inliner,
        (inliner->num_locals + inlinee->num_locals) * sizeof(MVMSpeshFacts *));
    memcpy(merged_facts, inliner->facts,
        inliner->num_locals * sizeof(MVMSpeshFacts *));
    memcpy(merged_facts + inliner->num_locals, inlinee->facts,
        inlinee->num_locals * sizeof(MVMSpeshFacts *));
    inliner->facts = merged_facts;
    merged_fact_counts = MVM_spesh_alloc(tc, inliner,
        (inliner->num_locals + inlinee->num_locals) * sizeof(MVMuint16));
    memcpy(merged_fact_counts, inliner->fact_counts,
        inliner->num_locals * sizeof(MVMuint16));
    memcpy(merged_fact_counts + inliner->num_locals, inlinee->fact_counts,
        inlinee->num_locals * sizeof(MVMuint16));
    inliner->fact_counts = merged_fact_counts;

    /* Copy over spesh slots. */
    for (i = 0; i < inlinee->num_spesh_slots; i++)
        MVM_spesh_add_spesh_slot(tc, inliner, inlinee->spesh_slots[i]);

    /* If they are from separate compilation units, make another pass through
     * to fix up on wvals. Note we can't do this in the first pass as we must
     * not modify the spesh slots once we've got started with the rewrites.
     * Now we've resolved all that, we're good to map wvals elsewhere into
     * some extra spesh slots. */
    if (!same_comp_unit) {
        bb = inlinee->entry;
        while (1) {
            MVMSpeshIns *ins = bb->first_ins;
            while (ins) {
                MVMuint16 opcode = ins->info->opcode;
                if (opcode == MVM_OP_wval || opcode == MVM_OP_wval_wide)
                    fix_wval(tc, inliner, inlinee, ins);
                ins = ins->next;
            }
            if (bb == inlinee_last_bb) break;
            bb = bb->linear_next;
        }
    }

    /* Merge de-opt tables, if needed. */
    orig_deopt_addrs = inliner->num_deopt_addrs;
    if (inlinee->num_deopt_addrs) {
        assert(inlinee->deopt_addrs != inliner->deopt_addrs);
        inliner->alloc_deopt_addrs += inlinee->alloc_deopt_addrs;
        if (inliner->deopt_addrs)
            inliner->deopt_addrs = MVM_realloc(inliner->deopt_addrs,
                inliner->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        else
            inliner->deopt_addrs = MVM_malloc(inliner->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        memcpy(inliner->deopt_addrs + inliner->num_deopt_addrs * 2,
            inlinee->deopt_addrs, inlinee->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        inliner->num_deopt_addrs += inlinee->num_deopt_addrs;
    }

    /* Merge inlines table, and add us an entry too. */
    orig_inlines = inliner->num_inlines;
    total_inlines = inliner->num_inlines + inlinee->num_inlines + 1;
    inliner->inlines = inliner->num_inlines
        ? MVM_realloc(inliner->inlines, total_inlines * sizeof(MVMSpeshInline))
        : MVM_malloc(total_inlines * sizeof(MVMSpeshInline));
    if (inlinee->num_inlines)
        memcpy(inliner->inlines + inliner->num_inlines, inlinee->inlines,
            inlinee->num_inlines * sizeof(MVMSpeshInline));
    for (i = inliner->num_inlines; i < total_inlines - 1; i++) {
        inliner->inlines[i].code_ref_reg += inliner->num_locals;
        inliner->inlines[i].locals_start += inliner->num_locals;
        inliner->inlines[i].lexicals_start += inliner->num_lexicals;
        inliner->inlines[i].return_deopt_idx += orig_deopt_addrs;
    }
    inliner->inlines[total_inlines - 1].sf             = inlinee_sf;
    inliner->inlines[total_inlines - 1].code_ref_reg   = code_ref_reg.reg.orig;
    inliner->inlines[total_inlines - 1].g              = inlinee;
    inliner->inlines[total_inlines - 1].locals_start   = inliner->num_locals;
    inliner->inlines[total_inlines - 1].lexicals_start = inliner->num_lexicals;
    inliner->inlines[total_inlines - 1].num_locals     = inlinee->num_locals;
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_VOID;
        break;
    case MVM_OP_invoke_o:
        inliner->inlines[total_inlines - 1].res_reg = invoke_ins->operands[0].reg.orig;
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_OBJ;
        break;
    case MVM_OP_invoke_i:
        inliner->inlines[total_inlines - 1].res_reg = invoke_ins->operands[0].reg.orig;
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_INT;
        break;
    case MVM_OP_invoke_n:
        inliner->inlines[total_inlines - 1].res_reg = invoke_ins->operands[0].reg.orig;
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_NUM;
        break;
    case MVM_OP_invoke_s:
        inliner->inlines[total_inlines - 1].res_reg = invoke_ins->operands[0].reg.orig;
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_STR;
        break;
    default:
        MVM_oops(tc, "Spesh inline: unknown invoke instruction");
    }
    inliner->inlines[total_inlines - 1].return_deopt_idx = return_deopt_idx(tc, invoke_ins);
    inliner->inlines[total_inlines - 1].unreachable = 0;
    inliner->inlines[total_inlines - 1].deopt_named_used_bit_field =
        inlinee->deopt_named_used_bit_field;
    inliner->inlines[total_inlines - 1].may_cause_deopt = may_cause_deopt;
    inliner->num_inlines = total_inlines;

    /* Create/update per-specialization local and lexical type maps. */
    if (!inliner->local_types && inliner->num_locals) {
        MVMint32 local_types_size = inliner->num_locals * sizeof(MVMuint16);
        inliner->local_types = MVM_malloc(local_types_size);
        memcpy(inliner->local_types, inliner->sf->body.local_types, local_types_size);
    }
    inliner->local_types = MVM_realloc(inliner->local_types,
        (inliner->num_locals + inlinee->num_locals) * sizeof(MVMuint16));
    if (inlinee->num_locals)
        memcpy(inliner->local_types + inliner->num_locals,
            inlinee->local_types ? inlinee->local_types : inlinee->sf->body.local_types,
            inlinee->num_locals * sizeof(MVMuint16));
    if (!inliner->lexical_types && inliner->num_lexicals) {
        MVMint32 lexical_types_size = inliner->num_lexicals * sizeof(MVMuint16);
        inliner->lexical_types = MVM_malloc(lexical_types_size);
        memcpy(inliner->lexical_types, inliner->sf->body.lexical_types, lexical_types_size);
    }
    inliner->lexical_types = MVM_realloc(inliner->lexical_types,
        (inliner->num_lexicals + inlinee->num_lexicals) * sizeof(MVMuint16));
    if (inlinee->num_lexicals)
        memcpy(inliner->lexical_types + inliner->num_lexicals,
            inlinee->lexical_types ? inlinee->lexical_types : inlinee->sf->body.lexical_types,
            inlinee->num_lexicals * sizeof(MVMuint16));

    /* Merge unreachable handlers array if needed. */
    if (inliner->unreachable_handlers || inlinee->unreachable_handlers) {
        MVMint8 *new_uh = MVM_spesh_alloc(tc, inliner, total_handlers);
        if (inlinee->unreachable_handlers)
            memcpy(new_uh, inlinee->unreachable_handlers,
                inlinee->num_handlers);
        new_uh[inlinee->num_handlers] = 0;
        if (inliner->unreachable_handlers)
            memcpy(new_uh + inlinee->num_handlers + 1, inliner->unreachable_handlers, inliner->num_handlers);
        inliner->unreachable_handlers = new_uh;
    }

    /* Merge handlers from inlinee. */
    resize_handlers_table(tc, inliner, total_handlers);

    if (inliner->num_handlers > 0)
        memmove(inliner->handlers + inlinee->num_handlers + 1, inliner->handlers,
            inliner->num_handlers * sizeof(MVMFrameHandler));

    if (inlinee->num_handlers > 0) {
        memcpy(inliner->handlers, inlinee->handlers,
            inlinee->num_handlers * sizeof(MVMFrameHandler));

        for (i = 0; i < inlinee->num_handlers; i++) {
            inliner->handlers[i].block_reg += inliner->num_locals;
            inliner->handlers[i].label_reg += inliner->num_locals;
            if (inliner->handlers[i].inlinee == -1)
                inliner->handlers[i].inlinee = total_inlines - 1;
            else
                inliner->handlers[i].inlinee += orig_inlines;
        }
    }

    /* Adjust indexes in inliner's frame handler annotations */
    bb = inliner->entry;
    while (bb) {
        if (bb == inlinee_first_bb) /* No need to adjust inlinee's annotations */
            bb = inlinee_last_bb->linear_next;
        if (bb) {
            MVMSpeshIns *ins = bb->first_ins;
            while (ins) {
                MVMSpeshAnn *ann = ins->annotations;
                while (ann) {
                    switch (ann->type) {
                        case MVM_SPESH_ANN_FH_START:
                        case MVM_SPESH_ANN_FH_END:
                        case MVM_SPESH_ANN_FH_GOTO:
                            ann->data.frame_handler_index += inlinee->num_handlers + 1;
                    }
                    ann = ann->next;
                }
                ins = ins->next;
            }
            bb = bb->linear_next;
        }
    }

    /* Insert inline boundary entry into the handlers table. */
    *inline_boundary_handler = inlinee->num_handlers;
    inliner->handlers[*inline_boundary_handler].category_mask = MVM_EX_INLINE_BOUNDARY;
    inliner->handlers[*inline_boundary_handler].action = 0;
    inliner->handlers[*inline_boundary_handler].inlinee = total_inlines - 1;

    /* Update total locals, lexicals, basic blocks, and handlers of the
     * inliner. */
    inliner->num_bbs      += inlinee->num_bbs - 1;
    inliner->num_locals   += inlinee->num_locals;
    inliner->num_lexicals += inlinee->num_lexicals;
    inliner->num_handlers += inlinee->num_handlers + 1;

    return inlinee_last_bb;
}

/* Tweak the successor of a BB, also updating the target BBs pred. */
static void tweak_succ(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                       MVMSpeshBB *prev_pred, MVMSpeshBB *new_succ, MVMuint32 missing_ok) {
    if (bb->num_succ == 0) {
        /* It had no successors, so we'll add one. */
        bb->succ = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB *));
        bb->num_succ = 1;
        bb->succ[0] = new_succ;
    }
    else {
        /* Otherwise, we can assume that the first successor is the one to
         * update; others will be there as a result of control handlers, but
         * these are always added last. */
        bb->succ[0] = new_succ;
    }
    if (new_succ->num_pred == 0) {
        new_succ->pred = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB *));
        new_succ->num_pred = 1;
        new_succ->pred[0] = bb;
    }
    else {
        MVMint32 found = 0;
        MVMint32 i;
        for (i = 0; i < new_succ->num_pred; i++)
            if (new_succ->pred[i] == prev_pred) {
                new_succ->pred[i] = bb;
                found = 1;
                break;
            }
        if (!found && !missing_ok)
            MVM_oops(tc,
                "Spesh inline: could not find appropriate pred to update\n");
    }
}

/* Finds return instructions and re-writes them into gotos, doing any needed
 * boxing or unboxing. */
static void return_to_set(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *return_ins, MVMSpeshOperand target) {
    MVMSpeshOperand *operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    operands[0]               = target;
    operands[1]               = return_ins->operands[0];
    return_ins->info          = MVM_op_get_op(MVM_OP_set);
    return_ins->operands      = operands;
    MVM_spesh_get_facts(tc, g, target)->writer = return_ins;
}

static void return_to_box(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *return_bb,
                   MVMSpeshIns *return_ins, MVMSpeshOperand target,
                   MVMuint16 box_type_op, MVMuint16 box_op) {
    MVMSpeshOperand type_temp     = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);

    /* Create and insert boxing instruction after current return instruction. */
    MVMSpeshIns      *box_ins     = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    MVMSpeshOperand *box_operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
    box_ins->info                 = MVM_op_get_op(box_op);
    box_ins->operands             = box_operands;
    box_operands[0]               = target;
    box_operands[1]               = return_ins->operands[0];
    box_operands[2]               = type_temp;
    MVM_spesh_manipulate_insert_ins(tc, return_bb, return_ins, box_ins);
    MVM_spesh_get_facts(tc, g, target)->writer = box_ins;
    MVM_spesh_usages_add_by_reg(tc, g, box_operands[1], box_ins);
    MVM_spesh_usages_add_by_reg(tc, g, box_operands[2], box_ins);

    /* Now turn return instruction node into lookup of appropriate box
     * type. */
    MVM_spesh_usages_delete_by_reg(tc, g, return_ins->operands[0], return_ins);
    return_ins->info        = MVM_op_get_op(box_type_op);
    return_ins->operands[0] = type_temp;
    MVM_spesh_get_facts(tc, g, type_temp)->writer = return_ins;
    MVM_spesh_manipulate_release_temp_reg(tc, g, type_temp);
}

static void rewrite_int_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, g, return_bb, return_ins);
        break;
    case MVM_OP_invoke_i:
        return_to_set(tc, g, return_ins, invoke_ins->operands[0]);
        break;
    case MVM_OP_invoke_o:
        return_to_box(tc, g, return_bb, return_ins, invoke_ins->operands[0],
            MVM_OP_hllboxtype_i, MVM_OP_box_i);
        break;
    default:
        MVM_oops(tc,
            "Spesh inline: unhandled case of return_i");
    }
}
static void rewrite_num_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, g, return_bb, return_ins);
        break;
    case MVM_OP_invoke_n:
        return_to_set(tc, g, return_ins, invoke_ins->operands[0]);
        break;
    case MVM_OP_invoke_o:
        return_to_box(tc, g, return_bb, return_ins, invoke_ins->operands[0],
            MVM_OP_hllboxtype_n, MVM_OP_box_n);
        break;
    default:
        MVM_oops(tc,
            "Spesh inline: unhandled case of return_n");
    }
}

static void rewrite_str_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, g, return_bb, return_ins);
        break;
    case MVM_OP_invoke_s:
        return_to_set(tc, g, return_ins, invoke_ins->operands[0]);
        break;
    case MVM_OP_invoke_o:
        return_to_box(tc, g, return_bb, return_ins, invoke_ins->operands[0],
            MVM_OP_hllboxtype_s, MVM_OP_box_s);
        break;
    default:
        MVM_oops(tc,
            "Spesh inline: unhandled case of return_s");
    }
}

static void rewrite_obj_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, g, return_bb, return_ins);
        break;
    case MVM_OP_invoke_o:
        return_to_set(tc, g, return_ins, invoke_ins->operands[0]);
        break;
    default:
        MVM_oops(tc,
            "Spesh inline: unhandled case of return_o");
    }
}

static void rewrite_returns(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                     MVMSpeshGraph *inlinee, MVMSpeshBB *invoke_bb,
                     MVMSpeshIns *invoke_ins, MVMSpeshBB *inlinee_last_bb) {
    /* Locate return instructions. */
    MVMSpeshBB *bb = inlinee->entry;
    MVMuint32 saw_return = 0;
    MVMSpeshFacts *prop_facts_from = NULL;
    MVMSpeshFacts *prop_facts_to = NULL;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            MVMuint16 opcode = ins->info->opcode;
            switch (opcode) {
            case MVM_OP_return:
                if (invoke_ins->info->opcode == MVM_OP_invoke_v) {
                    MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                        invoke_bb->succ[0]);
                    tweak_succ(tc, inliner, bb, invoke_bb, invoke_bb->succ[0], saw_return);
                    MVM_spesh_manipulate_delete_ins(tc, inliner, bb, ins);
                }
                else {
                    MVM_oops(tc,
                        "Spesh inline: return_v/invoke_[!v] mismatch");
                }
                saw_return = 1;
                break;
            case MVM_OP_return_i:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                tweak_succ(tc, inliner, bb, invoke_bb, invoke_bb->succ[0], saw_return);
                rewrite_int_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                saw_return = 1;
                break;
            case MVM_OP_return_n:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                tweak_succ(tc, inliner, bb, invoke_bb, invoke_bb->succ[0], saw_return);
                rewrite_num_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                saw_return = 1;
                break;
            case MVM_OP_return_s:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                tweak_succ(tc, inliner, bb, invoke_bb, invoke_bb->succ[0], saw_return);
                rewrite_str_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                break;
            case MVM_OP_return_o:
                if (!saw_return && invoke_ins->info->opcode == MVM_OP_invoke_o) {
                    prop_facts_from = MVM_spesh_get_facts(tc, inliner, ins->operands[0]);
                    prop_facts_to = MVM_spesh_get_facts(tc, inliner, invoke_ins->operands[0]);
                }
                else {
                    prop_facts_from = prop_facts_to = NULL;
                }
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                tweak_succ(tc, inliner, bb, invoke_bb, invoke_bb->succ[0], saw_return);
                rewrite_obj_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                saw_return = 1;
                break;
            }
            ins = ins->next;
        }
        if (bb == inlinee_last_bb) break;
        bb = bb->linear_next;
    }
    if (prop_facts_from && prop_facts_to)
        MVM_spesh_copy_facts_resolved(tc, inliner, prop_facts_to, prop_facts_from);
}

/* Re-writes argument passing and parameter taking instructions to simple
 * register set operations. */
static void rewrite_args(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                  MVMSpeshGraph *inlinee, MVMSpeshBB *invoke_bb,
                  MVMSpeshCallInfo *call_info, MVMSpeshBB *inlinee_last_bb) {
    /* Look for param-taking instructions. Track what arg instructions we
     * use in the process. */
    MVMSpeshBB *bb = inlinee->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            MVMuint16    opcode = ins->info->opcode;
            MVMSpeshIns *next   = ins->next;
            switch (opcode) {
            case MVM_OP_sp_getarg_o:
            case MVM_OP_sp_getarg_i:
            case MVM_OP_sp_getarg_n:
            case MVM_OP_sp_getarg_s: {
                MVMuint16    idx     = ins->operands[1].lit_i16;
                MVMSpeshIns *arg_ins = call_info->arg_ins[idx];
                switch (arg_ins->info->opcode) {
                case MVM_OP_arg_i:
                case MVM_OP_arg_n:
                case MVM_OP_arg_s:
                case MVM_OP_arg_o:
                    /* Receiver just becomes a set instruction; delete the
                     * argument passing instruction. */
                    ins->info = MVM_op_get_op(MVM_OP_set);
                    ins->operands[1] = arg_ins->operands[1];
                    MVM_spesh_usages_add_by_reg(tc, inliner, ins->operands[1], ins);
                    MVM_spesh_manipulate_delete_ins(tc, inliner,
                        call_info->prepargs_bb, arg_ins);
                    break;
                case MVM_OP_argconst_i:
                    arg_ins->info        = MVM_op_get_op(MVM_OP_const_i64);
                    arg_ins->operands[0] = ins->operands[0];
                    MVM_spesh_manipulate_delete_ins(tc, inliner, bb, ins);
                    MVM_spesh_usages_add_by_reg(tc, inliner, ins->operands[0], arg_ins);
                    break;
                case MVM_OP_argconst_n:
                    arg_ins->info        = MVM_op_get_op(MVM_OP_const_n64);
                    arg_ins->operands[0] = ins->operands[0];
                    MVM_spesh_manipulate_delete_ins(tc, inliner, bb, ins);
                    MVM_spesh_usages_add_by_reg(tc, inliner, ins->operands[0], arg_ins);
                    break;
                case MVM_OP_argconst_s:
                    arg_ins->info        = MVM_op_get_op(MVM_OP_const_s);
                    arg_ins->operands[0] = ins->operands[0];
                    MVM_spesh_manipulate_delete_ins(tc, inliner, bb, ins);
                    MVM_spesh_usages_add_by_reg(tc, inliner, ins->operands[0], arg_ins);
                    break;
                default:
                    MVM_oops(tc,
                        "Spesh inline: unhandled arg instruction %d",
                        arg_ins->info->opcode);
                }
                break;
            }
            }
            ins = next;
        }
        if (bb == inlinee_last_bb) break;
        bb = bb->linear_next;
    }

    {
    MVMSpeshIns *arg_ins = call_info->prepargs_ins->next;
    /* If there's some args that are not fetched by our inlinee,
     * we have to kick them out, as arg_* ops are only valid between
     * a prepargs and invoke_* op. */
    while (arg_ins) {
        MVMuint16    opcode = arg_ins->info->opcode;
        MVMSpeshIns *next   = arg_ins->next;
        switch (opcode) {
            case MVM_OP_arg_i:
            case MVM_OP_arg_n:
            case MVM_OP_arg_s:
            case MVM_OP_arg_o:
            case MVM_OP_argconst_i:
            case MVM_OP_argconst_n:
            case MVM_OP_argconst_s:
                MVM_spesh_manipulate_delete_ins(tc, inliner, call_info->prepargs_bb, arg_ins);
                break;
            case MVM_OP_set:
                break;
            case MVM_OP_invoke_i:
            case MVM_OP_invoke_n:
            case MVM_OP_invoke_s:
            case MVM_OP_invoke_o:
            case MVM_OP_invoke_v:
            default:
                next = NULL;
        }
        arg_ins = next;
    }
    }

    /* Delete the prepargs instruction. */
    MVM_spesh_manipulate_delete_ins(tc, inliner, invoke_bb, call_info->prepargs_ins);
}

/* Gets the first instruction from a spesh graph, looking through multiple
 * basic blocks to find it. */
static MVMSpeshIns * find_first_instruction(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *first_bb = g->entry->linear_next;
    while (first_bb) {
        MVMSpeshIns *first_ins = first_bb->first_ins;
        if (first_ins)
            return first_ins;
        first_bb = first_bb->linear_next;
    }
    MVM_oops(tc, "Unexpectedly empty specialization graph");
}

/* Annotates first and last instruction in post-processed inlinee with start
 * and end inline annotations. */
static void annotate_inline_start_end(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                               MVMSpeshGraph *inlinee, MVMint32 idx,
                               MVMSpeshBB *inlinee_last_bb,
                               MVMuint32 inline_boundary_handler) {
    /* Annotate first instruction as an inline start. */
    MVMSpeshAnn *start_ann     = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshAnn));
    MVMSpeshAnn *end_ann       = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshAnn));
    MVMSpeshIns *first_ins     = find_first_instruction(tc, inlinee);
    start_ann->next            = first_ins->annotations;
    start_ann->type            = MVM_SPESH_ANN_INLINE_START;
    start_ann->data.inline_idx = idx;
    first_ins->annotations = start_ann;

    /* Insert annotation for handler boundary indicator fixup. */
    start_ann = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshAnn));
    start_ann->next = first_ins->annotations;
    start_ann->type = MVM_SPESH_ANN_FH_START;
    start_ann->data.frame_handler_index = inline_boundary_handler;
    first_ins->annotations = start_ann;

    /* Now look for last instruction and annotate it. */
    end_ann->next             = inlinee_last_bb->last_ins->annotations;
    end_ann->type             = MVM_SPESH_ANN_INLINE_END;
    end_ann->data.inline_idx  = idx;
    inlinee_last_bb->last_ins->annotations = end_ann;

    /* Insert annotation for handler boundary fixup; we add the end
     * one that is needed and also a dummy goto one to keep things
     * that want all three happy. */
    end_ann = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshAnn));
    end_ann->next = inlinee_last_bb->last_ins->annotations;
    end_ann->type = MVM_SPESH_ANN_FH_END;
    end_ann->data.frame_handler_index = inline_boundary_handler;
    inlinee_last_bb->last_ins->annotations = end_ann;
    end_ann = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshAnn));
    end_ann->next = inlinee_last_bb->last_ins->annotations;
    end_ann->type = MVM_SPESH_ANN_FH_GOTO;
    end_ann->data.frame_handler_index = inline_boundary_handler;
    inlinee_last_bb->last_ins->annotations = end_ann;

    return;
}

/* Drives the overall inlining process. */
void MVM_spesh_inline(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                      MVMSpeshCallInfo *call_info, MVMSpeshBB *invoke_bb,
                      MVMSpeshIns *invoke_ins, MVMSpeshGraph *inlinee,
                      MVMStaticFrame *inlinee_sf, MVMSpeshOperand code_ref_reg,
                      MVMuint32 proxy_deopt_idx) {
    MVMSpeshIns *first_ins;

    /* Merge inlinee's graph into the inliner. */
    MVMuint32 inline_boundary_handler;
    MVMSpeshBB *inlinee_last_bb = merge_graph(tc, inliner, inlinee, inlinee_sf,
        invoke_bb, invoke_ins, code_ref_reg, &inline_boundary_handler);

    /* If we're profiling, note it's an inline. */
    first_ins = find_first_instruction(tc, inlinee);
    if (first_ins->info->opcode == MVM_OP_prof_enterspesh) {
        MVMSpeshIns *profenter         = first_ins;
        profenter->info                = MVM_op_get_op(MVM_OP_prof_enterinline);
        profenter->operands            = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshOperand));
        profenter->operands[0].lit_i16 = MVM_spesh_add_spesh_slot(tc, inliner,
            (MVMCollectable *)inlinee->sf);
    }

    /* Re-write returns to a set and goto. */
    rewrite_returns(tc, inliner, inlinee, invoke_bb, invoke_ins, inlinee_last_bb);

    /* Re-write the argument passing instructions to poke values into the
     * appropriate slots. */
    rewrite_args(tc, inliner, inlinee, invoke_bb, call_info, inlinee_last_bb);

    /* Annotate first and last instruction with inline table annotations; also
     * add annotations for fixing up the handlers table inline boundary
     * indicators. */
    annotate_inline_start_end(tc, inliner, inlinee, inliner->num_inlines - 1,
        inlinee_last_bb, inline_boundary_handler);

    /* Finally, turn the invoke instruction into a goto. */
    MVM_spesh_usages_delete_by_reg(tc, inliner,
        invoke_ins->operands[invoke_ins->info->opcode == MVM_OP_invoke_v ? 0 : 1],
        invoke_ins);
    invoke_ins->info = MVM_op_get_op(MVM_OP_goto);
    invoke_ins->operands[0].ins_bb = inlinee->entry->linear_next;
    tweak_succ(tc, inliner, invoke_bb, inlinee->entry, inlinee->entry->linear_next, 0);

    /* If this inline may cause deopt, then we take the deopt index at the
     * calling point and use it as a proxy for the deopts that may happen in
     * the inline. Otherwise, we might incorrectly fail to preserve values
     * for the sake of deopt. */
    if (inliner->inlines[inliner->num_inlines - 1].may_cause_deopt)
        MVM_spesh_usages_retain_deopt_index(tc, inliner, proxy_deopt_idx);
}
