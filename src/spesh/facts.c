#include "moar.h"

/* The code in this file walks the spesh graph, recording facts we discover
 * about each version of each local variable, and propagating the info as it
 * can. */

/* Copies facts from one var to another. */
static void copy_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 to_orig,
                       MVMuint16 to_i, MVMuint16 from_orig, MVMuint16 from_i) {
    MVMSpeshFacts *tfacts = &g->facts[to_orig][to_i];
    MVMSpeshFacts *ffacts = &g->facts[from_orig][from_i];
    tfacts->flags         = ffacts->flags;
    tfacts->type          = ffacts->type;
    tfacts->decont_type   = ffacts->decont_type;
    tfacts->value         = ffacts->value;
    tfacts->log_guards    = ffacts->log_guards;
    tfacts->num_log_guards = ffacts->num_log_guards;
}

/* Called when one set of facts depend on another, allowing any log guard
 * that is to thank to be marked used as needed later on. */
void MVM_spesh_facts_depend(MVMThreadContext *tc, MVMSpeshGraph *g,
                            MVMSpeshFacts *target, MVMSpeshFacts *source) {
    target->log_guards = source->log_guards;
    target->num_log_guards = source->num_log_guards;
}

/* Handles object-creating instructions. */
static void create_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 obj_orig,
                         MVMuint16 obj_i, MVMuint16 type_orig, MVMuint16 type_i) {
    MVMSpeshFacts *type_facts = &(g->facts[type_orig][type_i]);
    MVMSpeshFacts *obj_facts  = &(g->facts[obj_orig][obj_i]);

    /* The type is carried. */
    if (type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        obj_facts->type   = type_facts->type;
        obj_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
        MVM_spesh_facts_depend(tc, g, obj_facts, type_facts);
    }

    /* We know it's a concrete object. */
    obj_facts->flags |= MVM_SPESH_FACT_CONCRETE;
}

static void create_facts_with_type(MVMThreadContext *tc, MVMSpeshGraph *g,
                                   MVMuint16 obj_orig, MVMuint16 obj_i,
                                   MVMObject *type) {
    MVMSpeshFacts *obj_facts  = &(g->facts[obj_orig][obj_i]);

    /* The type is carried. */
    obj_facts->type   = type;
    obj_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;

    /* We know it's a concrete object. */
    obj_facts->flags |= MVM_SPESH_FACT_CONCRETE;
}

/* Adds facts from knowing the exact value being put into an object local. */
static void object_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 tgt_orig,
                         MVMuint16 tgt_i, MVMObject *obj) {
    /* Ensure it's non-null. */
    if (!obj)
        return;

    /* Set the value itself. */
    g->facts[tgt_orig][tgt_i].value.o  = obj;
    g->facts[tgt_orig][tgt_i].flags   |= MVM_SPESH_FACT_KNOWN_VALUE;

    /* We also know the type. */
    g->facts[tgt_orig][tgt_i].type   = STABLE(obj)->WHAT;
    g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_KNOWN_TYPE;

    /* Set concreteness flags. */
    if (IS_CONCRETE(obj))
        g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_CONCRETE;
    else
        g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_TYPEOBJ;
}
void MVM_spesh_facts_object_facts(MVMThreadContext *tc, MVMSpeshGraph *g,
                                  MVMSpeshOperand tgt, MVMObject *obj) {
    object_facts(tc, g, tgt.reg.orig, tgt.reg.i, obj);
}

/* Propagates information relating to decontainerization. */
static void decont_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
                         MVMuint16 out_orig, MVMuint16 out_i, MVMuint16 in_orig,
                         MVMuint16 in_i) {
    MVMSpeshFacts *out_facts = &(g->facts[out_orig][out_i]);
    MVMSpeshFacts *in_facts  = &(g->facts[in_orig][in_i]);

    /* If we know the original is decontainerized already, just copy its
     * info. */
    MVMint32 in_flags = in_facts->flags;
    if ((in_flags & MVM_SPESH_FACT_TYPEOBJ) ||
            ((in_flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
            !in_facts->type->st->container_spec)) {
        copy_facts(tc, g, out_orig, out_i, in_orig, in_i);
        return;
    }

    /* We may also know the original was containerized, and have some facts
     * about its contents. */
    if (in_flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE) {
        out_facts->type = in_facts->decont_type;
        out_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
    }
    if (in_flags & MVM_SPESH_FACT_DECONT_CONCRETE)
        out_facts->flags |= MVM_SPESH_FACT_CONCRETE;
    else if (in_flags & MVM_SPESH_FACT_DECONT_TYPEOBJ)
        out_facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
    if (in_flags & (MVM_SPESH_FACT_KNOWN_DECONT_TYPE |
                    MVM_SPESH_FACT_DECONT_CONCRETE |
                    MVM_SPESH_FACT_DECONT_TYPEOBJ))
        MVM_spesh_facts_depend(tc, g, out_facts, in_facts);
}

/* Looks up a wval and adds information based on it. */
static void wval_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 tgt_orig,
                       MVMuint16 tgt_i, MVMuint16 dep, MVMint64 idx) {
    MVMCompUnit *cu = g->sf->body.cu;
    if (dep < cu->body.num_scs) {
        MVMSerializationContext *sc = MVM_sc_get_sc(tc, cu, dep);
        if (sc)
            object_facts(tc, g, tgt_orig, tgt_i, MVM_sc_try_get_object(tc, sc, idx));
    }
}
static void wvalfrom_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 tgt_orig,
                           MVMuint16 tgt_i, MVMuint16 sslot, MVMint64 idx) {
    MVMSerializationContext *sc = (MVMSerializationContext *)g->spesh_slots[sslot];
    if (MVM_sc_is_object_immediately_available(tc, sc, idx)) {
        MVM_sc_get_object(tc, sc, idx);
        object_facts(tc, g, tgt_orig, tgt_i, MVM_sc_try_get_object(tc, sc, idx));
    }
}

/* Let's figure out what exact type of iter we'll get from an iter op */
static void iter_facts(MVMThreadContext *tc, MVMSpeshGraph *g,
                       MVMuint16 out_orig, MVMuint16 out_i,
                       MVMuint16 in_orig, MVMuint16 in_i) {
    MVMSpeshFacts *out_facts = &(g->facts[out_orig][out_i]);
    MVMSpeshFacts *in_facts  = &(g->facts[in_orig][in_i]);

    if (in_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        switch (REPR(in_facts->type)->ID) {
            case MVM_REPR_ID_VMArray:
                out_facts->type = g->sf->body.cu->body.hll_config->array_iterator_type;
                out_facts->flags |= MVM_SPESH_FACT_ARRAY_ITER;
                break;
            case MVM_REPR_ID_MVMHash:
            case MVM_REPR_ID_MVMContext:
                out_facts->type = g->sf->body.cu->body.hll_config->hash_iterator_type;
                out_facts->flags |= MVM_SPESH_FACT_HASH_ITER;
                break;
            default:
                return;
        }
        out_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE;
    }

}

/* constant ops on literals give us a specialize-time-known value */
static void literal_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *tgt_facts = &g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i];
    switch (ins->info->opcode) {
        case MVM_OP_const_i64:
            tgt_facts->value.i = ins->operands[1].lit_i64;
            break;
        case MVM_OP_const_i32:
            tgt_facts->value.i = ins->operands[1].lit_i32;
            break;
        case MVM_OP_const_i16:
            tgt_facts->value.i = ins->operands[1].lit_i16;
            break;
        case MVM_OP_const_i8:
            tgt_facts->value.i = ins->operands[1].lit_i8;
            break;
        case MVM_OP_const_n32:
            tgt_facts->value.n = ins->operands[1].lit_n32;
            break;
        case MVM_OP_const_n64:
            tgt_facts->value.n = ins->operands[1].lit_n64;
            break;
        case MVM_OP_const_i64_32:
            tgt_facts->value.i = ins->operands[1].lit_i32;
            break;
        case MVM_OP_const_i64_16:
            tgt_facts->value.i = ins->operands[1].lit_i16;
            break;
        case MVM_OP_const_s:
            tgt_facts->value.s = MVM_cu_string(tc, g->sf->body.cu,
                ins->operands[1].lit_str_idx);
            break;
        default:
            return;
    }
    tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
}
static void getstringfrom_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMCompUnit *dep = (MVMCompUnit *)g->spesh_slots[ins->operands[1].lit_i16];
    MVMuint32 idx = ins->operands[2].lit_ui32;
    MVMString *str = MVM_cu_string(tc, dep, idx);
    MVMSpeshFacts *tgt_facts = &g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i];
    tgt_facts->value.s = str;
    tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
}
static void trunc_i16_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *src_facts = &g->facts[ins->operands[1].reg.orig][ins->operands[1].reg.i];
    if (src_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        MVMSpeshFacts *tgt_facts = &g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i];
        tgt_facts->value.i = (MVMint16)src_facts->value.i;
        tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
    }
}
static void coerce_iu_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *src_facts = &g->facts[ins->operands[1].reg.orig][ins->operands[1].reg.i];
    if (src_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        MVMSpeshFacts *tgt_facts = &g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i];
        tgt_facts->value.i = (MVMint16)src_facts->value.i;
        tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
    }
}

/* Discover facts from extops. */
static void discover_extop(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMExtOpRecord *extops     = g->sf->body.cu->body.extops;
    MVMuint16       num_extops = g->sf->body.cu->body.num_extops;
    MVMuint16       i;
    for (i = 0; i < num_extops; i++) {
        if (extops[i].info == ins->info) {
            /* Found op; call its discovery function, if any. */
            if (extops[i].discover)
                extops[i].discover(tc, g, ins);
            return;
        }
    }
}

static void sp_guard_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                      MVMSpeshIns *ins) {
    MVMuint16 opcode = ins->info->opcode;

    /* The sslot is always the second-to-last parameter, except for
     * justconc and justtype, which don't have a spesh slot. */
    MVMuint16 sslot = ins->operands[ins->info->num_operands - 2].lit_i16;

    MVMSpeshFacts *facts    = &g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i];

    /* We do not copy facts here, because it caused some trouble.
     * Oftentimes, there were more flags set in the target register's
     * facts before this, and got cleared by this copy operation.
     * Also, it caused even the empty perl6 program to fail with a
     * long stack trace about not finding an exception handler. */
    /*copy_facts(tc, g,*/
        /*ins->operands[0].reg.orig, ins->operands[0].reg.i,*/
        /*ins->operands[1].reg.orig, ins->operands[1].reg.i);*/

    if (opcode == MVM_OP_sp_guard
            || opcode == MVM_OP_sp_guardconc
            || opcode == MVM_OP_sp_guardtype) {
        facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
        facts->type   = ((MVMSTable *)g->spesh_slots[sslot])->WHAT;
    }
    if (opcode == MVM_OP_sp_guardconc || opcode == MVM_OP_sp_guardjustconc) {
        facts->flags |= MVM_SPESH_FACT_CONCRETE;
    }
    if (opcode == MVM_OP_sp_guardtype || opcode == MVM_OP_sp_guardjusttype) {
        facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
    }
    if (opcode == MVM_OP_sp_guardobj) {
        facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        facts->value.o = (MVMObject *)g->spesh_slots[sslot];
    }
}

/* Considers logged types and, if they are stable, adds facts and a guard. */
static void log_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                      MVMSpeshIns *ins, MVMSpeshPlanned *p,
                      MVMSpeshAnn *deopt_one_ann, MVMSpeshAnn *logged_ann) {
    /* See if we have stable type information. For now, we need consistent
     * types, since a mis-match will force a deopt. In the future we may be
     * able to do Basic Block Versioning inspired tricks, like producing two
     * different code paths ahead when there are a small number of options. */
    MVMObject *agg_type = NULL;
    MVMuint32 agg_type_count = 0;
    MVMuint32 agg_type_object = 0;
    MVMuint32 agg_concrete = 0;
    MVMuint32 i;
    for (i = 0; i < p->num_type_stats; i++) {
        MVMSpeshStatsByType *ts = p->type_stats[i];
        MVMuint32 j;
        for (j = 0; j < ts->num_by_offset; j++) {
            if (ts->by_offset[j].bytecode_offset == logged_ann->data.bytecode_offset) {
                /* Go over the logged types. */
                MVMuint32 num_types = ts->by_offset[j].num_types;
                MVMuint32 k;
                for (k = 0; k < num_types; k++) {
                    /* If it's inconsistent with the aggregated type so far,
                     * then first check if the type we're now seeing is either
                     * massively more popular or massively less popular. If
                     * massively less, disregard this one. If massively more,
                     * disregard the previous one. Otherwise, tot up the type
                     * object vs. concrete. */
                    MVMObject *cur_type = ts->by_offset[j].types[k].type;
                    MVMuint32 count = ts->by_offset[j].types[k].count;
                    if (agg_type) {
                        if (agg_type != cur_type) {
                            if (count > 100 * agg_type_count) {
                                /* This one is hugely more popular. */
                                agg_type = cur_type;
                                agg_type_count = 0;
                                agg_concrete = 0;
                                agg_type_object = 0;
                            }
                            else if (agg_type_count > 100 * count) {
                                /* This one is hugely less popular. */
                                continue;
                            }
                            else {
                                /* Unstable types. */
                                return;
                            }
                        }
                    }
                    else {
                        agg_type = cur_type;
                    }
                    agg_type_count += count;
                    if (ts->by_offset[j].types[k].type_concrete)
                        agg_concrete++;
                    else
                        agg_type_object++;
                }

                /* No need to consider searching after this offset. */
                break;
            }
        }
    }
    if (agg_type) {
        MVMSpeshIns *guard;
        MVMSpeshAnn *ann;
        MVMuint16 guard_op;

        /* Generate a new version. We'll use this version for the original,
         * unguarded, value, which we know is the instruction right before
         * the guard, so that makes things rather simple. Thus the facts we
         * will set go on the original register */
        MVMSpeshOperand guard_reg = ins->operands[0];
        MVMSpeshOperand preguard_reg = MVM_spesh_manipulate_new_version(tc, g,
                ins->operands[0].reg.orig);
        MVMSpeshFacts *pre_facts = &g->facts[preguard_reg.reg.orig][preguard_reg.reg.i];
        MVMSpeshFacts *facts = &g->facts[guard_reg.reg.orig][guard_reg.reg.i];
        ins->operands[0] = preguard_reg;
        pre_facts->writer = ins;

        /* Add facts and choose guard op. */
        facts->type = agg_type;
        facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
        if (agg_concrete && !agg_type_object) {
            facts->flags |= MVM_SPESH_FACT_CONCRETE;
            guard_op = MVM_OP_sp_guardconc;
        }
        else if (agg_type_object && !agg_concrete) {
            facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
            guard_op = MVM_OP_sp_guardtype;
        }
        else {
            guard_op = MVM_OP_sp_guard;
        }

        /* Insert guard instruction. */
        guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        guard->info = MVM_op_get_op(guard_op);
        guard->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
        guard->operands[0] = guard_reg;
        guard->operands[1] = preguard_reg;
        guard->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
            (MVMCollectable *)agg_type->st);
        guard->operands[3].lit_ui32 = deopt_one_ann->data.deopt_idx;
        if (ins->next)
            MVM_spesh_manipulate_insert_ins(tc, bb, ins, guard);
        else
            MVM_spesh_manipulate_insert_ins(tc, bb->linear_next, NULL, guard);
        facts->writer = guard;
        MVM_spesh_usages_add_by_reg(tc, g, preguard_reg, guard);

        /* Move deopt annotation to the guard instruction. */
        ann = ins->annotations;
        if (ann == deopt_one_ann) {
            ins->annotations = ann->next;
        }
        else {
            while (ann) {
                if (ann->next == deopt_one_ann) {
                    ann->next = deopt_one_ann->next;
                    break;
                }
                ann = ann->next;
            }
        }
        deopt_one_ann->next = NULL;
        guard->annotations = deopt_one_ann;

        /* Copy deopt usages to the preguard register. */
        {
            MVMSpeshDeoptUseEntry *due = facts->usage.deopt_users;
            while (due) {
                MVM_spesh_usages_add_deopt_usage(tc, g, pre_facts, due->deopt_idx);
                due = due->next;
            }
        }

        /* Add entry in log guards table, and mark facts as depending on it. */
        if (g->num_log_guards % 16 == 0) {
            MVMSpeshLogGuard *orig_log_guards = g->log_guards;
            g->log_guards = MVM_spesh_alloc(tc, g,
                (g->num_log_guards + 16) * sizeof(MVMSpeshLogGuard));
            if (orig_log_guards)
                memcpy(g->log_guards, orig_log_guards,
                    g->num_log_guards * sizeof(MVMSpeshLogGuard));
        }
        g->log_guards[g->num_log_guards].ins = guard;
        g->log_guards[g->num_log_guards].bb = ins->next ? bb : bb->linear_next;
        facts->log_guards = MVM_spesh_alloc(tc, g, sizeof(MVMint32));
        facts->log_guards[0] = g->num_log_guards;
        facts->num_log_guards++;
        g->num_log_guards++;
    }
}

/* Visits the blocks in dominator tree order, recursively. */
static void add_bb_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                         MVMSpeshPlanned *p) {
    MVMint32 i;

    /* Look for instructions that provide or propagate facts. */
    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        /* See if there's deopt and logged annotations, and if so add logged
         * facts and guards. */
        MVMSpeshAnn *ann = ins->annotations;
        MVMSpeshAnn *ann_deopt_one = NULL;
        MVMSpeshAnn *ann_logged = NULL;
        while (ann) {
            switch (ann->type) {
                case MVM_SPESH_ANN_DEOPT_ONE_INS:
                    ann_deopt_one = ann;
                    break;
                case MVM_SPESH_ANN_LOGGED:
                    ann_logged = ann;
            }
            ann = ann->next;
        }
        if (p && ann_deopt_one && ann_logged)
            log_facts(tc, g, bb, ins, p, ann_deopt_one, ann_logged);

        /* Look for ops that are fact-interesting. */
        switch (ins->info->opcode) {
        case MVM_OP_set:
            copy_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].reg.orig, ins->operands[1].reg.i);
            break;
        case MVM_OP_create:
            create_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].reg.orig, ins->operands[1].reg.i);
            break;
        case MVM_OP_sp_fastcreate:
        case MVM_OP_sp_fastbox_i:
        case MVM_OP_sp_fastbox_bi:
        case MVM_OP_sp_fastbox_i_ic:
        case MVM_OP_sp_fastbox_bi_ic:
        case MVM_OP_sp_add_I:
        case MVM_OP_sp_sub_I:
        case MVM_OP_sp_mul_I:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ((MVMSTable *)g->spesh_slots[ins->operands[2].lit_i16])->WHAT);
            break;
        case MVM_OP_clone:
            copy_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].reg.orig, ins->operands[1].reg.i);
            break;
        case MVM_OP_box_s:
        case MVM_OP_box_i:
        case MVM_OP_box_n: {
                MVMSpeshFacts *target_facts = &(g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i]);
                create_facts(tc, g,
                    ins->operands[0].reg.orig, ins->operands[0].reg.i,
                    ins->operands[2].reg.orig, ins->operands[2].reg.i);
                target_facts->flags |= MVM_SPESH_FACT_KNOWN_BOX_SRC;
                break;
            }
        case MVM_OP_coerce_sI: {
                create_facts(tc, g,
                    ins->operands[0].reg.orig, ins->operands[0].reg.i,
                    ins->operands[2].reg.orig, ins->operands[2].reg.i);
                break;
            }
        case MVM_OP_add_I:
        case MVM_OP_sub_I:
        case MVM_OP_mul_I:
        case MVM_OP_div_I:
        case MVM_OP_mod_I:
            create_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[3].reg.orig, ins->operands[3].reg.i);
            break;
        case MVM_OP_neg_I:
        case MVM_OP_abs_I:
            create_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[2].reg.orig, ins->operands[2].reg.i);
            break;
        case MVM_OP_nativecallcast:
            create_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[2].reg.orig, ins->operands[2].reg.i);
            break;
        case MVM_OP_trunc_u16:
        case MVM_OP_trunc_i16:
            trunc_i16_facts(tc, g, ins);
            break;
        case MVM_OP_coerce_ui:
        case MVM_OP_coerce_iu:
            trunc_i16_facts(tc, g, ins);
            break;
        case MVM_OP_bootint:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTInt);
            break;
        case MVM_OP_bootnum:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTNum);
            break;
        case MVM_OP_bootstr:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTStr);
            break;
        case MVM_OP_bootarray:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTArray);
            break;
        case MVM_OP_bootintarray:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTIntArray);
            break;
        case MVM_OP_bootnumarray:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTNumArray);
            break;
        case MVM_OP_bootstrarray:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTStrArray);
            break;
        case MVM_OP_boothash:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTHash);
            break;
        case MVM_OP_hllboolfor: {
            MVMSpeshOperand *name_op = &ins->operands[1];
            MVMSpeshFacts *namefacts = &g->facts[name_op->reg.orig][name_op->reg.i];
            if (namefacts->writer && namefacts->writer->info->opcode == MVM_OP_const_s) {
                MVMString *hllname = MVM_cu_string(tc, g->sf->body.cu, ins->operands[1].lit_str_idx);
                MVMHLLConfig *hll = MVM_hll_get_config_for(tc, hllname);
                if (hll->true_value && hll->false_value && STABLE(hll->true_value)->WHAT == STABLE(hll->false_value)->WHAT)
                    create_facts_with_type(tc, g,
                        ins->operands[0].reg.orig, ins->operands[0].reg.i,
                        STABLE(hll->true_value)->WHAT);
            }
            break;
        }
        case MVM_OP_hllbool: {
            MVMHLLConfig *hll = g->sf->body.cu->body.hll_config;
            if (hll->true_value && hll->false_value && STABLE(hll->true_value)->WHAT == STABLE(hll->false_value)->WHAT)
                create_facts_with_type(tc, g,
                    ins->operands[0].reg.orig, ins->operands[0].reg.i,
                    STABLE(hll->true_value)->WHAT);
            break;
        }
        case MVM_OP_hllboxtype_i:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->int_box_type);
            break;
        case MVM_OP_hllboxtype_n:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->num_box_type);
            break;
        case MVM_OP_hllboxtype_s:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->str_box_type);
            break;
        case MVM_OP_hlllist:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->slurpy_array_type);
            break;
        case MVM_OP_hllhash:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->slurpy_hash_type);
            break;
        case MVM_OP_decont:
            decont_facts(tc, g, ins,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].reg.orig, ins->operands[1].reg.i);
            break;
        case MVM_OP_wval:
            wval_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].lit_i16, ins->operands[2].lit_i16);
            break;
        case MVM_OP_wval_wide:
            wval_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].lit_i16, ins->operands[2].lit_i64);
            break;
        case MVM_OP_sp_getwvalfrom:
            wvalfrom_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].lit_i16, ins->operands[2].lit_i64);
            break;
        case MVM_OP_sp_getspeshslot:
            object_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                (MVMObject *)g->spesh_slots[ins->operands[1].lit_i16]);
            break;
        case MVM_OP_iter:
            iter_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[1].reg.orig, ins->operands[1].reg.i);
            break;
        case MVM_OP_newexception:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                tc->instance->boot_types.BOOTException);
            break;
        case MVM_OP_getlexref_i:
        case MVM_OP_getlexref_i32:
        case MVM_OP_getlexref_i16:
        case MVM_OP_getlexref_i8:
        case MVM_OP_getlexref_u32:
        case MVM_OP_getlexref_u16:
        case MVM_OP_getlexref_u8:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->int_lex_ref);
            break;
        case MVM_OP_getlexref_n:
        case MVM_OP_getlexref_n32:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->num_lex_ref);
            break;
        case MVM_OP_getlexref_s:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->str_lex_ref);
            break;
        case MVM_OP_getattrref_i:
        case MVM_OP_getattrsref_i:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->int_attr_ref);
            break;
        case MVM_OP_getattrref_n:
        case MVM_OP_getattrsref_n:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->num_attr_ref);
            break;
        case MVM_OP_getattrref_s:
        case MVM_OP_getattrsref_s:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->str_attr_ref);
            break;
        case MVM_OP_atposref_i:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->int_pos_ref);
            break;
        case MVM_OP_atposref_n:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->num_pos_ref);
            break;
        case MVM_OP_atposref_s:
            create_facts_with_type(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                g->sf->body.cu->body.hll_config->str_pos_ref);
            break;

        case MVM_OP_const_i64:
        case MVM_OP_const_i32:
        case MVM_OP_const_i16:
        case MVM_OP_const_i8:
        case MVM_OP_const_n64:
        case MVM_OP_const_n32:
        case MVM_OP_const_i64_32:
        case MVM_OP_const_i64_16:
        case MVM_OP_const_s:
            literal_facts(tc, g, ins);
            break;
        case MVM_OP_sp_getstringfrom:
            getstringfrom_facts(tc, g, ins);
            break;
        case MVM_OP_encode:
            create_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[3].reg.orig, ins->operands[3].reg.i);
            break;
        case MVM_OP_encoderep:
            create_facts(tc, g,
                ins->operands[0].reg.orig, ins->operands[0].reg.i,
                ins->operands[4].reg.orig, ins->operands[4].reg.i);
            break;
        case MVM_OP_setdispatcher:
        case MVM_OP_setdispatcherfor:
            g->sets_dispatcher = 1;
            break;
        case MVM_OP_nextdispatcherfor:
            g->sets_nextdispatcher = 1;
            break;
        case MVM_OP_sp_guard:
        case MVM_OP_sp_guardconc:
        case MVM_OP_sp_guardtype:
        case MVM_OP_sp_guardobj:
        case MVM_OP_sp_guardjustconc:
        case MVM_OP_sp_guardjusttype:
            sp_guard_facts(tc, g, bb, ins);
            break;
        default:
            if (ins->info->opcode == (MVMuint16)-1)
                discover_extop(tc, g, ins);
        }
        ins = ins->next;
    }

    /* Visit children. */
    for (i = 0; i < bb->num_children; i++)
        add_bb_facts(tc, g, bb->children[i], p);
}

/* Exception handlers that use a block to store the handler must not have the
 * instructions that install the block eliminated. This tweaks the usage of
 * them. */
static void tweak_block_handler_usage(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMuint32 i;
    for (i = 0; i < g->sf->body.num_handlers; i++) {
        if (g->sf->body.handlers[i].action == MVM_EX_ACTION_INVOKE) {
            MVMSpeshOperand operand;
            operand.reg.orig = g->sf->body.handlers[i].block_reg;
            operand.reg.i = 1;
            MVM_spesh_usages_add_for_handler_by_reg(tc, g, operand);
        }
    }
}

/* Kicks off fact discovery from the top of the (dominator) tree. */
void MVM_spesh_facts_discover(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshPlanned *p,
        MVMuint32 is_specialized) {
    /* Set up normal usage information. */
    MVM_spesh_usages_create_usage(tc, g);
    tweak_block_handler_usage(tc, g);

    /* We do an initial dead instruction pass before then computing the deopt
     * usages. This dead instrution elimination pass acts also as a PHI prune,
     * since we have numerous dead PHI instructions that can cause bogus
     * deopt retentions, as well as increase the amount of work that the
     * deopt usage algorithm has to do. Note that we don't do this for an
     * already specialized inlinee, since information was already discarded. */
    if (!is_specialized) {
        MVM_spesh_eliminate_dead_ins(tc, g);
        MVM_spesh_usages_create_deopt_usage(tc, g);
    }

    /* Finally, collect facts. */
    add_bb_facts(tc, g, g->entry, p);
}
