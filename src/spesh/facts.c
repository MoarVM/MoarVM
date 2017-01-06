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

/* Checks if there's a possible aliasing operation that could cause the
 * facts about the contents of a container to be invalid by the instruction
 * under consideration. Assumes the instruction is a decont with argument 1
 * being the thing to decontainerize. */
MVMint32 MVM_spesh_facts_decont_blocked_by_alias(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    /* No facts or no writer means we can't know it's safe; block. */
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (!facts || !facts->writer)
        return 1;

    /* Walk backwards over instructions. */
    while (ins->prev) {
        ins = ins->prev;

        /* If we found the writer without anything blocking, we're good. */
        if (ins == facts->writer)
            return 0;

        /* If there's an operation that may alias, blocked. */
        switch (ins->info->opcode) {
            case MVM_OP_bindattr_o:
            case MVM_OP_bindattrs_o:
            case MVM_OP_assign:
            case MVM_OP_assignunchecked:
            case MVM_OP_assign_i:
            case MVM_OP_assign_n:
            case MVM_OP_assign_s:
            case MVM_OP_sp_bind_o:
            case MVM_OP_sp_p6obind_o:
                return 1;
        }
    }

    /* We didn't find the writer in this basic block, meaning an invocation
     * may have made it unsafe. Block. */
    return 1;
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
    if (!MVM_spesh_facts_decont_blocked_by_alias(tc, g, ins)) {
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
            case MVM_REPR_ID_MVMArray:
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
/* Allocates space for keeping track of guards inserted from logging, and
 * their usage. */
static void allocate_log_guard_table(MVMThreadContext *tc, MVMSpeshGraph *g) {
    g->log_guards = MVM_spesh_alloc(tc, g, g->num_log_slots * sizeof(MVMSpeshLogGuard));
}

/* Check for stability of what was logged, and if it looks sane then add facts
 * and turn the log instruction into a  */
static void log_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMObject     *stable_value = NULL;
    MVMObject     *stable_cont  = NULL;
    MVMSpeshFacts *facts;

    /* See if all the recorded facts match up; a NULL means there was a code
     * path that never reached making a log entry. */
    MVMuint16 log_start = ins->operands[1].lit_i16 * MVM_SPESH_LOG_RUNS;
    MVMuint16 i;
    for (i = log_start; i < log_start + MVM_SPESH_LOG_RUNS; i++) {
        MVMObject *consider = (MVMObject *)g->log_slots[i];
        if (consider) {
            if (!stable_value) {
                stable_value = consider;
            }
            else if (STABLE(stable_value) != STABLE(consider)
                    || IS_CONCRETE(stable_value) != IS_CONCRETE(consider)) {
                stable_value = NULL;
                break;
            }
        }
    }
    if (!stable_value)
        return;

    /* If the value is a container type, need to look inside of it. */
    if (STABLE(stable_value)->container_spec && IS_CONCRETE(stable_value)) {
        MVMContainerSpec const *contspec = STABLE(stable_value)->container_spec;
        if (!contspec->fetch_never_invokes)
            return;
        stable_cont  = stable_value;
        stable_value = NULL;
        for (i = log_start; i < log_start + MVM_SPESH_LOG_RUNS; i++) {
            MVMRegister r;
            contspec->fetch(tc, stable_cont, &r);
            if (r.o) {
                if (!stable_value) {
                    stable_value = r.o;
                }
                else if (STABLE(stable_value) != STABLE(r.o)
                        || IS_CONCRETE(stable_value) != IS_CONCRETE(r.o)) {
                    stable_value = NULL;
                    break;
                }
            }
        }
        if (!stable_value)
            return;
    }

    /* Produce a guard op and set facts. */
    if (stable_cont) {
        MVMSpeshOperand reg  = ins->operands[0];
        MVMContainerSpec *cs = (MVMContainerSpec *) STABLE(stable_cont)->container_spec;
        facts                = &g->facts[reg.reg.orig][reg.reg.i];
        facts->type          = STABLE(stable_cont)->WHAT;
        facts->flags        |= (MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE |
                               MVM_SPESH_FACT_KNOWN_DECONT_TYPE);
        facts->decont_type   = STABLE(stable_value)->WHAT;

        /* If this is a native container, we get away with testing
         * against the STABLE only, as the NativeRef REPR has all
         * interesting values in its REPRData. */
        if (cs->can_store(tc, stable_cont) &&
                (MVM_6model_container_iscont_i(tc, stable_cont) ||
                MVM_6model_container_iscont_n(tc, stable_cont) ||
                MVM_6model_container_iscont_s(tc, stable_cont))) {
            facts         = &g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i];
            /*facts->type   = STABLE(stable_value)->WHAT;*/
            facts->flags |= MVM_SPESH_FACT_RW_CONT;

            ins->info = MVM_op_get_op(MVM_OP_sp_guardconc);

            ins->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
            ins->operands[0] = reg;
            ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)STABLE(stable_cont));
        } else {
            if (cs->can_store(tc, stable_cont)) {
                /* We could do stability testing on rw-ness too, but it's quite
                 * unlikely we'll have codepaths with a mix of readable and
                 * writable containers. */
                facts->flags |= MVM_SPESH_FACT_RW_CONT;
                if (IS_CONCRETE(stable_value)) {
                    facts->flags |= MVM_SPESH_FACT_DECONT_CONCRETE;
                    ins->info = MVM_op_get_op(MVM_OP_sp_guardrwconc);
                }
                else {
                    facts->flags |= MVM_SPESH_FACT_DECONT_TYPEOBJ;
                    ins->info = MVM_op_get_op(MVM_OP_sp_guardrwtype);
                }
            }
            else {
                if (IS_CONCRETE(stable_value)) {
                    facts->flags |= MVM_SPESH_FACT_DECONT_CONCRETE;
                    ins->info = MVM_op_get_op(MVM_OP_sp_guardcontconc);
                }
                else {
                    facts->flags |= MVM_SPESH_FACT_DECONT_TYPEOBJ;
                    ins->info = MVM_op_get_op(MVM_OP_sp_guardconttype);
                }
            }
            ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0] = reg;
            ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)STABLE(stable_cont));
            ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)STABLE(stable_value));
        }
    }
    else {
        facts         = &g->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i];
        facts->type   = STABLE(stable_value)->WHAT;
        facts->flags |= (MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_DECONTED);
        if (IS_CONCRETE(stable_value)) {
            facts->flags |= MVM_SPESH_FACT_CONCRETE;
            ins->info = MVM_op_get_op(MVM_OP_sp_guardconc);
        }
        else {
            facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
            ins->info = MVM_op_get_op(MVM_OP_sp_guardtype);
        }
        ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)STABLE(stable_value));
    }

    /* Add entry in log guards table, and mark facts as depending on it. */
    g->log_guards[g->num_log_guards].ins = ins;
    g->log_guards[g->num_log_guards].bb  = bb;
    facts->flags     |= MVM_SPESH_FACT_FROM_LOG_GUARD;
    facts->log_guard  = g->num_log_guards;
    g->num_log_guards++;
}

/* Visits the blocks in dominator tree order, recursively. */
static void add_bb_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                         MVMint32 cur_deopt_idx) {
    MVMint32 i, is_phi;

    /* Look for instructions that provide or propagate facts. */
    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        /* See if there's a deopt annotation, and sync cur_deopt_idx. */
        MVMSpeshAnn *ann = ins->annotations;
        while (ann) {
            if (ann->type == MVM_SPESH_ANN_DEOPT_ONE_INS ||
                    ann->type == MVM_SPESH_ANN_DEOPT_ALL_INS) {
                cur_deopt_idx = ann->data.deopt_idx;
                break;
            }
            ann = ann->next;
        }

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
             * to be specified. */
            if ((is_phi && i == 0)
                || (!is_phi && (ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_write_reg)) {
                MVMSpeshFacts *facts = &(g->facts[ins->operands[i].reg.orig][ins->operands[i].reg.i]);
                facts->deopt_idx = cur_deopt_idx;
                facts->writer    = ins;
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
        case MVM_OP_sp_log: {
            MVMuint16 po = ins->prev
                ? ins->prev->info->opcode
                : bb->pred[0]->last_ins->info->opcode;
            if (po != MVM_OP_getlexstatic_o && po != MVM_OP_getlexperinvtype_o)
                log_facts(tc, g, bb, ins);
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
        add_bb_facts(tc, g, bb->children[i], cur_deopt_idx);
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
void MVM_spesh_facts_discover(MVMThreadContext *tc, MVMSpeshGraph *g) {
    allocate_log_guard_table(tc, g);
    add_bb_facts(tc, g, g->entry, -1);
    tweak_block_handler_usage(tc, g);
}
