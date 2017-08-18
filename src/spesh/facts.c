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
    tfacts->log_guard     = ffacts->log_guard;
}

/* Called when one set of facts depend on another, allowing any log guard
 * that is to thank to be marked used as needed later on. */
void MVM_spesh_facts_depend(MVMThreadContext *tc, MVMSpeshGraph *g,
                            MVMSpeshFacts *target, MVMSpeshFacts *source) {
    if (source->flags & MVM_SPESH_FACT_FROM_LOG_GUARD) {
        target->flags     |= MVM_SPESH_FACT_FROM_LOG_GUARD;
        target->log_guard  = source->log_guard;
    }
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

    /* If we know the type object, then we can check to see if
     * it's a container type. */
    if (type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        MVMObject *type = type_facts->type;
        if (type && !STABLE(type)->container_spec)
            obj_facts->flags |= MVM_SPESH_FACT_DECONTED;
    }
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

    /* If we know the type object, then we can check to see if
     * it's a container type. */
    if (type && !STABLE(type)->container_spec)
        obj_facts->flags |= MVM_SPESH_FACT_DECONTED;
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

    /* Set concreteness and decontainerized flags. */
    if (IS_CONCRETE(obj)) {
        g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_CONCRETE;
        if (!STABLE(obj)->container_spec)
            g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_DECONTED;
    }
    else {
        g->facts[tgt_orig][tgt_i].flags |= MVM_SPESH_FACT_TYPEOBJ | MVM_SPESH_FACT_DECONTED;
    }
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
    if (in_flags & MVM_SPESH_FACT_DECONTED)
        copy_facts(tc, g, out_orig, out_i, in_orig, in_i);

    /* We know the result is decontainerized. */
    out_facts->flags |= MVM_SPESH_FACT_DECONTED;

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

/* Considers logged types and, if they are stable, adds facts and a guard. */
static void log_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                      MVMSpeshIns *ins, MVMSpeshPlanned *p,
                      MVMSpeshAnn *deopt_one_ann, MVMSpeshAnn *logged_ann) {
    /* See if we have stable type information. For now, we need consistent
     * types, since a mis-match will force a deopt. In the future we may be
     * able to do Basic Block Versioning inspired tricks, like producing two
     * different code paths ahead when there are a small number of options. */
    MVMObject *agg_type = NULL;
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
                    /* If it's inconsistent with the aggregated type, then
                     * bail out; too unstable. Otherwise, take it as the
                     * aggregated type and tot up type object vs. concrete (we
                     * assess facts about that at the end) */
                    MVMObject *cur_type = ts->by_offset[j].types[k].type;
                    if (agg_type) {
                        if (agg_type != cur_type)
                            return;
                    }
                    else {
                        agg_type = cur_type;
                    }
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

        /* Add facts and choose guard op. */
        MVMSpeshFacts *facts = &g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i];
        facts->type = agg_type;
        facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
        if (agg_concrete && !agg_type_object) {
            facts->flags |= MVM_SPESH_FACT_CONCRETE;
            if (!agg_type->st->container_spec)
                facts->flags |= MVM_SPESH_FACT_DECONTED;
            guard_op = MVM_OP_sp_guardconc;
        }
        else if (agg_type_object && !agg_concrete) {
            facts->flags |= MVM_SPESH_FACT_TYPEOBJ | MVM_SPESH_FACT_DECONTED;
            guard_op = MVM_OP_sp_guardtype;
        }
        else {
            if (!agg_type->st->container_spec)
                facts->flags |= MVM_SPESH_FACT_DECONTED;
            guard_op = MVM_OP_sp_guard;
        }

        /* Insert guard instruction. */
        guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        guard->info = MVM_op_get_op(guard_op);
        guard->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
        guard->operands[0] = ins->operands[0];
        guard->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
            (MVMCollectable *)agg_type->st);
        guard->operands[2].lit_ui32 = g->deopt_addrs[2 * deopt_one_ann->data.deopt_idx];
        if (ins->next)
            MVM_spesh_manipulate_insert_ins(tc, bb, ins, guard);
        else
            MVM_spesh_manipulate_insert_ins(tc, bb->linear_next, NULL, guard);

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
        facts->flags |= MVM_SPESH_FACT_FROM_LOG_GUARD;
        facts->log_guard = g->num_log_guards;
        g->num_log_guards++;
    }
}

/* Visits the blocks in dominator tree order, recursively. */
static void add_bb_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                         MVMSpeshPlanned *p, MVMint32 cur_deopt_idx) {
    MVMint32 i, is_phi;

    /* Look for instructions that provide or propagate facts. */
    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        /* See if there's deopt and logged annotations. Sync cur_deopt_idx
         * and, for logged+deopt-one, add logged facts and guards. */
        MVMSpeshAnn *ann = ins->annotations;
        MVMSpeshAnn *ann_deopt_one = NULL;
        MVMSpeshAnn *ann_logged = NULL;
        MVMint32 is_deopt_ins = 0;
        while (ann) {
            switch (ann->type) {
                case MVM_SPESH_ANN_DEOPT_ONE_INS:
                    ann_deopt_one = ann;
                    cur_deopt_idx = ann->data.deopt_idx;
                    is_deopt_ins = 1;
                    break;
                case MVM_SPESH_ANN_DEOPT_ALL_INS:
                    cur_deopt_idx = ann->data.deopt_idx;
                    break;
                case MVM_SPESH_ANN_LOGGED:
                    ann_logged = ann;
            }
            ann = ann->next;
        }
        if (ann_deopt_one && ann_logged)
            log_facts(tc, g, bb, ins, p, ann_deopt_one, ann_logged);

        /* Look through operands for reads and writes. */
        is_phi = ins->info->opcode == MVM_SSA_PHI;
        for (i = 0; i < ins->info->num_operands; i++) {
            /* Reads need usage tracking; if the read is after a deopt point
             * relative to the write then give it an extra usage bump. */
            if ((is_phi && i > 0)
                || (!is_phi && (ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg)) {
                MVMSpeshFacts *facts = &(g->facts[ins->operands[i].reg.orig][ins->operands[i].reg.i]);
                facts->usages += facts->deopt_idx == cur_deopt_idx ? 1 : 2;
            }

            /* Writes need the current deopt index and the writing instruction
             * to be specified. A write that's on a deopt instruction bumps
             * the usage too. */
            if ((is_phi && i == 0)
                || (!is_phi && (ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_write_reg)) {
                MVMSpeshFacts *facts = &(g->facts[ins->operands[i].reg.orig][ins->operands[i].reg.i]);
                facts->deopt_idx = cur_deopt_idx;
                facts->writer    = ins;
                if (is_deopt_ins)
                    facts->usages++;
            }
        }

        /* Look for ops that are fact-interesting. */
        switch (ins->info->opcode) {
        case MVM_OP_inc_i:
        case MVM_OP_inc_u:
        case MVM_OP_dec_i:
        case MVM_OP_dec_u:
            /* These all read as well as write a value, so bump usages. */
            g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i - 1].usages++;
            break;
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
        case MVM_OP_cas_o:
        case MVM_OP_atomicload_o: {
            MVMSpeshOperand result = ins->operands[0];
            g->facts[result.reg.orig][result.reg.i].flags |= MVM_SPESH_FACT_DECONTED;
            break;
        }
        default:
            if (ins->info->opcode == (MVMuint16)-1)
                discover_extop(tc, g, ins);
        }
        ins = ins->next;
    }

    /* Visit children. */
    for (i = 0; i < bb->num_children; i++)
        add_bb_facts(tc, g, bb->children[i], p, cur_deopt_idx);
}

/* Exception handlers that use a block to store the handler must not have the
 * instructions that install the block eliminated. This tweaks the usage of
 * them. */
static void tweak_block_handler_usage(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMint32 i;
    for (i = 0; i < g->sf->body.num_handlers; i++) {
        if (g->sf->body.handlers[i].action == MVM_EX_ACTION_INVOKE)
            g->facts[g->sf->body.handlers[i].block_reg][1].usages++;
    }
}

/* Kicks off fact discovery from the top of the (dominator) tree. */
void MVM_spesh_facts_discover(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshPlanned *p) {
    add_bb_facts(tc, g, g->entry, p, -1);
    tweak_block_handler_usage(tc, g);
}
