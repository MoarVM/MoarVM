#include "moar.h"

/* This is where the main optimization work on a spesh graph takes place,
 * using facts discovered during analysis. */

/* Obtains facts for an operand. */
MVMSpeshFacts * MVM_spesh_get_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o) {
    return &g->facts[o.reg.orig][o.reg.i];
}

/* Obtains a string constant. */
MVMString * MVM_spesh_get_string(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o) {
    return g->sf->body.cu->body.strings[o.lit_str_idx];
}

/* Copy facts between two register operands. */
static void copy_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand to,
                       MVMSpeshOperand from) {
    MVMSpeshFacts *tfacts = MVM_spesh_get_facts(tc, g, to);
    MVMSpeshFacts *ffacts = MVM_spesh_get_facts(tc, g, from);
    tfacts->flags         = ffacts->flags;
    tfacts->type          = ffacts->type;
    tfacts->decont_type   = ffacts->decont_type;
    tfacts->value         = ffacts->value;
}

/* Adds a value into a spesh slot and returns its index. */
MVMint16 MVM_spesh_add_spesh_slot(MVMThreadContext *tc, MVMSpeshGraph *g, MVMCollectable *c) {
    if (g->num_spesh_slots >= g->alloc_spesh_slots) {
        g->alloc_spesh_slots += 8;
        if (g->spesh_slots)
            g->spesh_slots = realloc(g->spesh_slots,
                g->alloc_spesh_slots * sizeof(MVMCollectable *));
        else
            g->spesh_slots = malloc(g->alloc_spesh_slots * sizeof(MVMCollectable *));
    }
    g->spesh_slots[g->num_spesh_slots] = c;
    return g->num_spesh_slots++;
}

/* Performs optimization on a method lookup. If we know the type that we'll
 * be dispatching on, resolve it right off. If not, add a cache. */
static void optimize_method_lookup(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    /* See if we can resolve the method right off due to knowing the type. */
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMint32 resolved = 0;
    if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        /* Try to resolve. */
        MVMString *name = MVM_spesh_get_string(tc, g, ins->operands[2]);
        MVMObject *meth = MVM_6model_find_method_cache_only(tc, obj_facts->type, name);
        if (!MVM_is_null(tc, meth)) {
            /* Could compile-time resolve the method. Add it in a spesh slot
             * and tweak instruction to grab it from there. */
            MVMint16 ss = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)meth);
            MVM_spesh_get_facts(tc, g, ins->operands[1])->usages--;
            ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
            ins->operands[1].lit_i16 = ss;
            resolved = 1;
        }
    }

    /* If not, add space to cache a single type/method pair, to save hash
     * lookups in the (common) monomorphic case, and rewrite to caching
     * version of the instruction. */
    if (!resolved) {
        MVMSpeshOperand *orig_o = ins->operands;
        ins->info = MVM_op_get_op(MVM_OP_sp_findmeth);
        ins->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
        memcpy(ins->operands, orig_o, 3 * sizeof(MVMSpeshOperand));
        ins->operands[3].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, NULL);
        MVM_spesh_add_spesh_slot(tc, g, NULL);
    }
}

/* Sees if we can resolve an istype at compile time. */
static void optimize_istype(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts  = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMSpeshFacts *type_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);
    MVMSpeshFacts *result_facts;

    if (type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE &&
         obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        MVMint32 result;
        if (!MVM_6model_try_cache_type_check(tc, obj_facts->type, type_facts->type, &result))
            return;
        ins->info = MVM_op_get_op(MVM_OP_const_i64);
        result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        ins->operands[1].lit_i64 = result;
        result_facts->value.i64  = result;
        obj_facts->usages--;
        type_facts->usages--;
    }
}

/* Sees if we can resolve an isconcrete at compile time. */
static void optimize_isconcrete(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (obj_facts->flags & (MVM_SPESH_FACT_CONCRETE | MVM_SPESH_FACT_TYPEOBJ)) {
        MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        ins->info                   = MVM_op_get_op(MVM_OP_const_i64);
        result_facts->flags        |= MVM_SPESH_FACT_KNOWN_VALUE;
        ins->operands[0].lit_i64    = !!(obj_facts->flags & MVM_SPESH_FACT_CONCRETE);
        result_facts->value.i64     = !!(obj_facts->flags & MVM_SPESH_FACT_CONCRETE);
        obj_facts->usages--;
    }
}

/* iffy ops that operate on a known value register can turn into goto
 * or be dropped. */
static void optimize_iffy(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins, MVMSpeshBB *bb) {
    MVMSpeshFacts *flag_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    MVMuint8 negated_op;
    MVMuint8 truthvalue;

    switch (ins->info->opcode) {
        case MVM_OP_if_i:
        case MVM_OP_if_s:
        case MVM_OP_if_n:
        case MVM_OP_if_o:
        case MVM_OP_ifnonnull:
            negated_op = 0;
            break;
        case MVM_OP_unless_i:
        case MVM_OP_unless_s:
        case MVM_OP_unless_n:
        case MVM_OP_unless_o:
            negated_op = 1;
            break;
        default:
            return;
    }

    if (flag_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        switch (ins->info->opcode) {
            case MVM_OP_if_i:
            case MVM_OP_unless_i:
                truthvalue = flag_facts->value.i64;
                break;
            case MVM_OP_if_o:
            case MVM_OP_unless_o: {
                MVMObject *objval = flag_facts->value.o;
                MVMBoolificationSpec *bs = objval->st->boolification_spec;
                MVMRegister resultreg;
                switch (bs == NULL ? MVM_BOOL_MODE_NOT_TYPE_OBJECT : bs->mode) {
                    case MVM_BOOL_MODE_UNBOX_INT:
                    case MVM_BOOL_MODE_UNBOX_NUM:
                    case MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY:
                    case MVM_BOOL_MODE_UNBOX_STR_NOT_EMPTY_OR_ZERO:
                    case MVM_BOOL_MODE_BIGINT:
                    case MVM_BOOL_MODE_ITER:
                    case MVM_BOOL_MODE_HAS_ELEMS:
                    case MVM_BOOL_MODE_NOT_TYPE_OBJECT:
                        MVM_coerce_istrue(tc, objval, &resultreg, NULL, NULL, 0);
                        truthvalue = resultreg.i64;
                        break;
                    case MVM_BOOL_MODE_CALL_METHOD:
                    default:
                        return;
                }
                break;
            }
            case MVM_OP_if_n:
            case MVM_OP_unless_n:
                truthvalue = flag_facts->value.n64 != 0.0;
                break;
            default:
                return;
        }
    } else {
        return;
    }

    if (truthvalue != negated_op) {
        /* this conditional can be turned into an unconditional jump */
        ins->info = MVM_op_get_op(MVM_OP_goto);
        ins->operands[0] = ins->operands[1];

        /* since we have an unconditional jump now, we can remove the successor
         * that's in the linear_next */
        MVM_spesh_manipulate_remove_successor(tc, bb, bb->linear_next);
    } else {
        /* this conditional can be dropped completely */
        MVM_spesh_manipulate_remove_successor(tc, bb, ins->operands[1].ins_bb);
        MVM_spesh_manipulate_delete_ins(tc, bb, ins);
    }
}

/* Optimizes a hllize instruction away if the type is known and already in the
 * right HLL, by turning it into a set. */
static void optimize_hllize(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && obj_facts->type) {
        if (STABLE(obj_facts->type)->hll_owner == g->sf->body.cu->body.hll_config) {
            ins->info = MVM_op_get_op(MVM_OP_set);
            copy_facts(tc, g, ins->operands[0], ins->operands[1]);
        }
    }
}

/* Turns a decont into a set, if we know it's not needed. Also make sure we
 * propagate any needed information. */
static void optimize_decont(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (obj_facts->flags & (MVM_SPESH_FACT_DECONTED | MVM_SPESH_FACT_TYPEOBJ)) {
        ins->info = MVM_op_get_op(MVM_OP_set);
        copy_facts(tc, g, ins->operands[0], ins->operands[1]);
    }
    else {
        MVMSpeshFacts *res_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE) {
            res_facts->type   = obj_facts->decont_type;
            res_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
        }
        if (obj_facts->flags & MVM_SPESH_FACT_DECONT_CONCRETE)
            res_facts->flags |= MVM_SPESH_FACT_CONCRETE;
        else if (obj_facts->flags & MVM_SPESH_FACT_DECONT_TYPEOBJ)
            res_facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
    }
}

/* Optimize away assertparamcheck if we know it will pass. */
static void optimize_assertparamcheck(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE && facts->value.i64) {
        facts->usages--;
        MVM_spesh_manipulate_delete_ins(tc, bb, ins);
    }
}

/* If we know the type of a significant operand, we might try to specialize by
 * representation. */
static void optimize_repr_op(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                             MVMSpeshIns *ins, MVMint32 type_operand) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[type_operand]);
    if (facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && facts->type)
        if (REPR(facts->type)->spesh)
            REPR(facts->type)->spesh(tc, STABLE(facts->type), g, bb, ins);
}

/* Optimizes away a lexical lookup when we know the value won't change from
 * the logged one. */
static void optimize_getlex_known(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                  MVMSpeshIns *ins) {
    /* Ensure we have a log instruction following this one. */
    if (ins->next && ins->next->info->opcode == MVM_OP_sp_log) {
        /* Locate logged object. */
        MVMuint16       log_slot = ins->next->operands[1].lit_i16 * MVM_SPESH_LOG_RUNS;
        MVMCollectable *log_obj  = g->log_slots[log_slot];
        if (log_obj) {
            /* Place in a spesh slot. */
            MVMuint16 ss = MVM_spesh_add_spesh_slot(tc, g, log_obj);

            /* Delete logging instructin. */
            MVM_spesh_manipulate_delete_ins(tc, bb, ins->next);

            /* Transform lookup instruction into spesh slot read. */
            MVM_spesh_get_facts(tc, g, ins->operands[1])->usages--;
            ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
            ins->operands[1].lit_i16 = ss;
        }
    }
}

/* Visits the blocks in dominator tree order, recursively. */
static void optimize_bb(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb) {
    MVMint32 i;

    /* Look for instructions that are interesting to optimize. */
    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        switch (ins->info->opcode) {
        case MVM_OP_set:
            copy_facts(tc, g, ins->operands[0], ins->operands[1]);
            break;
        case MVM_OP_if_i:
        case MVM_OP_unless_i:
        case MVM_OP_if_n:
        case MVM_OP_unless_n:
        case MVM_OP_if_o:
        case MVM_OP_unless_o:
            optimize_iffy(tc, g, ins, bb);
            break;
        case MVM_OP_findmeth:
            optimize_method_lookup(tc, g, ins);
            break;
        case MVM_OP_create:
            optimize_repr_op(tc, g, bb, ins, 1);
            break;
        /*case MVM_OP_isconcrete:
            optimize_isconcrete(tc, g, ins);
            break;*/
        case MVM_OP_istype:
            optimize_istype(tc, g, ins);
            break;
        case MVM_OP_bindattr_i:
        case MVM_OP_bindattr_n:
        case MVM_OP_bindattr_s:
        case MVM_OP_bindattr_o:
            optimize_repr_op(tc, g, bb, ins, 0);
            break;
        case MVM_OP_getattr_i:
        case MVM_OP_getattr_n:
        case MVM_OP_getattr_s:
        case MVM_OP_getattr_o:
            optimize_repr_op(tc, g, bb, ins, 1);
            break;
        case MVM_OP_box_i:
        case MVM_OP_box_n:
        case MVM_OP_box_s:
            optimize_repr_op(tc, g, bb, ins, 2);
            break;
        case MVM_OP_unbox_i:
        case MVM_OP_unbox_n:
        case MVM_OP_unbox_s:
            optimize_repr_op(tc, g, bb, ins, 1);
            break;
        case MVM_OP_elems:
            optimize_repr_op(tc, g, bb, ins, 1);
            break;
        case MVM_OP_hllize:
            optimize_hllize(tc, g, ins);
            break;
        case MVM_OP_decont:
            optimize_decont(tc, g, ins);
            break;
        case MVM_OP_assertparamcheck:
            optimize_assertparamcheck(tc, g, bb, ins);
            break;
        case MVM_OP_getlexstatic_o:
            optimize_getlex_known(tc, g, bb, ins);
            break;
        case MVM_OP_sp_log:
            /* Left-over log instruction that didn't become a guard; just
             * delete it. */
            MVM_spesh_manipulate_delete_ins(tc, bb, ins);
            break;
        }
        ins = ins->next;
    }

    /* Visit children. */
    for (i = 0; i < bb->num_children; i++)
        optimize_bb(tc, g, bb->children[i]);

    /* Now walk backwards through the instructions, eliminating any that are
     * pure and unused. */
    ins = bb->last_ins;
    while (ins) {
        MVMSpeshIns *prev = ins->prev;
        if (ins->info->opcode == MVM_SSA_PHI) {
            MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
            if (facts->usages == 0) {
                /* Propagate non-usage. */
                for (i = 1; i < ins->info->num_operands; i++)
                    MVM_spesh_get_facts(tc, g, ins->operands[i])->usages--;

                /* Remove this phi. */
                MVM_spesh_manipulate_delete_ins(tc, bb, ins);
            }
        }
        else if (ins->info->pure) {
            /* Sanity check to make sure it's a write reg as first operand. */
            if ((ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
                MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                if (facts->usages == 0) {
                    /* Propagate non-usage. */
                    for (i = 1; i < ins->info->num_operands; i++)
                        if ((ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg)
                            MVM_spesh_get_facts(tc, g, ins->operands[i])->usages--;

                    /* Remove this instruction. */
                    MVM_spesh_manipulate_delete_ins(tc, bb, ins);
                }
            }
        }
        ins = prev;
    }
}

/* Eliminates any unreachable basic blocks (that is, dead code). Not having
 * to consider them any further simplifies all that follows. */
static void eliminate_dead(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Iterate to fixed point. */
    MVMint8  *seen     = malloc(g->num_bbs);
    MVMint32  orig_bbs = g->num_bbs;
    MVMint8   death    = 1;
    while (death) {
        /* First pass: mark every basic block that is the entry point or the
         * successor of some other block. */
        MVMSpeshBB *cur_bb = g->entry;
        memset(seen, 0, g->num_bbs);
        seen[0] = 1;
        while (cur_bb) {
            MVMuint16 i;
            for (i = 0; i < cur_bb->num_succ; i++)
                seen[cur_bb->succ[i]->idx] = 1;
            cur_bb = cur_bb->linear_next;
        }

        /* Second pass: eliminate dead BBs from consideration. */
        death = 0;
        cur_bb = g->entry;
        while (cur_bb->linear_next) {
            if (!seen[cur_bb->linear_next->idx]) {
                cur_bb->linear_next = cur_bb->linear_next->linear_next;
                g->num_bbs--;
                death = 1;
            }
            cur_bb = cur_bb->linear_next;
        }
    }
    free(seen);

    if (g->num_bbs != orig_bbs) {
        MVMint32    new_idx  = 0;
        MVMSpeshBB *cur_bb   = g->entry;
        while (cur_bb) {
            cur_bb->idx = new_idx;
            new_idx++;
            cur_bb = cur_bb->linear_next;
        }
    }
}

/* Drives the overall optimization work taking place on a spesh graph. */
void MVM_spesh_optimize(MVMThreadContext *tc, MVMSpeshGraph *g) {
    optimize_bb(tc, g, g->entry);
    eliminate_dead(tc, g);
}
