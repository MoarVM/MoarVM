#include "moar.h"

/* This is where the main optimization work on a spesh graph takes place,
 * using facts discovered during analysis. */

/* Writes to stderr about each inline that we perform. */
#define MVM_LOG_INLINES 0

/* Obtains facts for an operand, just directly accessing them without
 * inferring any kind of usage. */
static MVMSpeshFacts * get_facts_direct(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o) {
    return &g->facts[o.reg.orig][o.reg.i];
}

/* Obtains facts for an operand, indicating they are being used. */
MVMSpeshFacts * MVM_spesh_get_and_use_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o) {
    MVMSpeshFacts *facts = get_facts_direct(tc, g, o);
    MVM_spesh_use_facts(tc, g, facts);
    return facts;
}

/* Obtains facts for an operand, but doesn't (yet) indicate usefulness. */
MVMSpeshFacts * MVM_spesh_get_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o) {
    return get_facts_direct(tc, g, o);
}

/* Mark facts for an operand as being relied upon. */
void MVM_spesh_use_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts) {
    if (facts->flags & MVM_SPESH_FACT_FROM_LOG_GUARD)
        g->log_guards[facts->log_guard].used = 1;
    if (facts->flags & MVM_SPESH_FACT_MERGED_WITH_LOG_GUARD) {
        MVMSpeshIns *thePHI = facts->writer;
        MVMuint32 op_i;

        for (op_i = 1; op_i < thePHI->info->num_operands; op_i++) {
            MVM_spesh_get_and_use_facts(tc, g, thePHI->operands[op_i]);
        }
    }
}

/* Obtains a string constant. */
MVMString * MVM_spesh_get_string(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o) {
    return MVM_cu_string(tc, g->sf->body.cu, o.lit_str_idx);
}

/* Copy facts between two register operands. */
static void copy_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand to,
                       MVMSpeshOperand from) {
    MVMSpeshFacts *tfacts = get_facts_direct(tc, g, to);
    MVMSpeshFacts *ffacts = get_facts_direct(tc, g, from);
    tfacts->flags         = ffacts->flags;
    tfacts->type          = ffacts->type;
    tfacts->decont_type   = ffacts->decont_type;
    tfacts->value         = ffacts->value;
    tfacts->log_guard     = ffacts->log_guard;
}

/* Adds a value into a spesh slot and returns its index.
 * If a spesh slot already holds this value, return that instead. */
MVMint16 MVM_spesh_add_spesh_slot_try_reuse(MVMThreadContext *tc, MVMSpeshGraph *g, MVMCollectable *c) {
    MVMint16 prev_slot;
    for (prev_slot = 0; prev_slot < g->num_spesh_slots; prev_slot++) {
        if (g->spesh_slots[prev_slot] == c)
            return prev_slot;
    }
    return MVM_spesh_add_spesh_slot(tc, g, c);
}

/* Adds a value into a spesh slot and returns its index. */
MVMint16 MVM_spesh_add_spesh_slot(MVMThreadContext *tc, MVMSpeshGraph *g, MVMCollectable *c) {
    if (g->num_spesh_slots >= g->alloc_spesh_slots) {
        g->alloc_spesh_slots += 8;
        if (g->spesh_slots)
            g->spesh_slots = MVM_realloc(g->spesh_slots,
                g->alloc_spesh_slots * sizeof(MVMCollectable *));
        else
            g->spesh_slots = MVM_malloc(g->alloc_spesh_slots * sizeof(MVMCollectable *));
    }
    g->spesh_slots[g->num_spesh_slots] = c;
    return g->num_spesh_slots++;
}

static void optimize_repr_op(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                             MVMSpeshIns *ins, MVMint32 type_operand);

static void optimize_findmeth_s_perhaps_constant(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *name_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);

    if (name_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        if (name_facts->writer && name_facts->writer->info->opcode == MVM_OP_const_s) {
            name_facts->usages--;
            ins->info = MVM_op_get_op(MVM_OP_findmeth);
            ins->operands[2].lit_i64 = 0;
            ins->operands[2].lit_str_idx = name_facts->writer->operands[1].lit_str_idx;
            MVM_spesh_use_facts(tc, g, name_facts);
        }
    }
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
        MVMObject *meth = MVM_spesh_try_find_method(tc, obj_facts->type, name);
        if (!MVM_is_null(tc, meth)) {
            /* Could compile-time resolve the method. Add it in a spesh slot. */
            MVMint16 ss = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)meth);

            /* Tweak facts for the target, given we know the method. */
            MVMSpeshFacts *meth_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[0]);
            meth_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
            meth_facts->value.o = meth;

            /* Update the instruction to grab the spesh slot. */
            ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
            ins->operands[1].lit_i16 = ss;

            resolved = 1;

            MVM_spesh_use_facts(tc, g, obj_facts);
            obj_facts->usages--;
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
        ins->info = MVM_op_get_op(MVM_OP_const_i64_16);
        result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        ins->operands[1].lit_i16 = result;
        result_facts->value.i  = result;

        obj_facts->usages--;
        type_facts->usages--;
        MVM_spesh_facts_depend(tc, g, result_facts, obj_facts);
        MVM_spesh_use_facts(tc, g, obj_facts);
        MVM_spesh_facts_depend(tc, g, result_facts, type_facts);
        MVM_spesh_use_facts(tc, g, type_facts);
    }
}

static void optimize_is_reprid(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMuint32 wanted_repr_id;
    MVMuint64 result_value;

    if (!(obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE)) {
        return;
    }

    switch (ins->info->opcode) {
        case MVM_OP_islist: wanted_repr_id = MVM_REPR_ID_VMArray; break;
        case MVM_OP_ishash: wanted_repr_id = MVM_REPR_ID_MVMHash; break;
        case MVM_OP_isint:  wanted_repr_id = MVM_REPR_ID_P6int; break;
        case MVM_OP_isnum:  wanted_repr_id = MVM_REPR_ID_P6num; break;
        case MVM_OP_isstr:  wanted_repr_id = MVM_REPR_ID_P6str; break;
        default:            return;
    }

    MVM_spesh_use_facts(tc, g, obj_facts);

    result_value = REPR(obj_facts->type)->ID == wanted_repr_id;

    if (result_value == 0) {
        MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        ins->info = MVM_op_get_op(MVM_OP_const_i64_16);
        ins->operands[1].lit_i16 = 0;
        result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        result_facts->value.i = 0;
        MVM_spesh_facts_depend(tc, g, result_facts, obj_facts);
    } else {
        ins->info = MVM_op_get_op(MVM_OP_isnonnull);
    }
}

static void optimize_gethow(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMObject       *how_obj = NULL;
    if (obj_facts->flags & (MVM_SPESH_FACT_KNOWN_TYPE))
        how_obj = MVM_spesh_try_get_how(tc, obj_facts->type);
    /* There may be other valid ways to get the facts (known value?) */
    if (how_obj) {
        MVMSpeshFacts *how_facts;
        /* Transform gethow lookup to spesh slot lookup */
        MVMint16 spesh_slot = MVM_spesh_add_spesh_slot_try_reuse(tc, g, (MVMCollectable*)how_obj);
        MVM_spesh_get_facts(tc, g, ins->operands[1])->usages--;
        ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
        ins->operands[1].lit_i16 = spesh_slot;
        /* Store facts about the value in the write operand */
        how_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        how_facts->flags  |= (MVM_SPESH_FACT_KNOWN_VALUE | MVM_SPESH_FACT_KNOWN_TYPE);
        how_facts->value.o = how_obj;
        how_facts->type    = STABLE(how_obj)->WHAT;
    }
}


/* Sees if we can resolve an isconcrete at compile time. */
static void optimize_isconcrete(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (obj_facts->flags & (MVM_SPESH_FACT_CONCRETE | MVM_SPESH_FACT_TYPEOBJ)) {
        MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        ins->info                   = MVM_op_get_op(MVM_OP_const_i64_16);
        result_facts->flags        |= MVM_SPESH_FACT_KNOWN_VALUE;
        result_facts->value.i       = obj_facts->flags & MVM_SPESH_FACT_CONCRETE ? 1 : 0;
        ins->operands[1].lit_i16    = result_facts->value.i;

        MVM_spesh_use_facts(tc, g, obj_facts);
        MVM_spesh_facts_depend(tc, g, result_facts, obj_facts);

        obj_facts->usages--;
    }
}

static void optimize_exception_ops(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMuint16 op = ins->info->opcode;

    if (op == MVM_OP_newexception) {
        MVMSpeshOperand target   = ins->operands[0];
        MVMObject      *type     = tc->instance->boot_types.BOOTException;
        MVMSTable      *st       = STABLE(type);
        ins->info                = MVM_op_get_op(MVM_OP_sp_fastcreate);
        ins->operands            = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
        ins->operands[0]         = target;
        ins->operands[1].lit_i16 = st->size;
        ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
    } else {
        /*
        MVMSpeshFacts *target_facts;
        */

        /* XXX This currently still causes problems. */
        return;

        /*
        switch (op) {
        case MVM_OP_bindexmessage:
        case MVM_OP_bindexpayload: {
            MVMSpeshOperand target   = ins->operands[0];
            MVMSpeshOperand value    = ins->operands[1];
            target_facts             = MVM_spesh_get_facts(tc, g, target);

            if (!(target_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE)
                || !(REPR(target_facts->type)->ID == MVM_REPR_ID_MVMException))
                break;

            ins->info                = MVM_op_get_op(op == MVM_OP_bindexmessage ? MVM_OP_sp_bind_s : MVM_OP_sp_bind_o);
            ins->operands            = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0]         = target;
            ins->operands[1].lit_i16 = op == MVM_OP_bindexmessage ? offsetof(MVMException, body.message)
                                                                  : offsetof(MVMException, body.payload);
            ins->operands[2]         = value;
            break;
        }
        case MVM_OP_bindexcategory: {
            MVMSpeshOperand target   = ins->operands[0];
            MVMSpeshOperand category = ins->operands[1];
            target_facts             = MVM_spesh_get_facts(tc, g, target);

            if (!(target_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE)
                || !(REPR(target_facts->type)->ID == MVM_REPR_ID_MVMException))
                break;

            ins->info                = MVM_op_get_op(MVM_OP_sp_bind_i32);
            ins->operands            = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0]         = target;
            ins->operands[1].lit_i16 = offsetof(MVMException, body.category);
            ins->operands[2]         = category;
            break;
        }
        case MVM_OP_getexmessage:
        case MVM_OP_getexpayload: {
            MVMSpeshOperand destination = ins->operands[0];
            MVMSpeshOperand target      = ins->operands[1];
            target_facts                = MVM_spesh_get_facts(tc, g, target);

            if (!(target_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE)
                || !(REPR(target_facts->type)->ID == MVM_REPR_ID_MVMException))
                break;

            ins->info                = MVM_op_get_op(op == MVM_OP_getexmessage ? MVM_OP_sp_get_s : MVM_OP_sp_get_o);
            ins->operands            = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0]         = destination;
            ins->operands[1]         = target;
            ins->operands[2].lit_i16 = op == MVM_OP_getexmessage ? offsetof(MVMException, body.message)
                                                                 : offsetof(MVMException, body.payload);
            break;
        }
        case MVM_OP_getexcategory: {
            MVMSpeshOperand destination = ins->operands[0];
            MVMSpeshOperand target      = ins->operands[1];
            target_facts                = MVM_spesh_get_facts(tc, g, target);

            if (!(target_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE)
                || !(REPR(target_facts->type)->ID == MVM_REPR_ID_MVMException))
                break;

            ins->info                = MVM_op_get_op(MVM_OP_sp_get_i32);
            ins->operands            = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0]         = destination;
            ins->operands[1]         = target;
            ins->operands[2].lit_i16 = offsetof(MVMException, body.category);
            break;
        }
        }
        */
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
                truthvalue = flag_facts->value.i;
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
                truthvalue = flag_facts->value.n != 0.0;
                break;
            default:
                return;
        }

        MVM_spesh_use_facts(tc, g, flag_facts);
        flag_facts->usages--;

        truthvalue = truthvalue ? 1 : 0;
        if (truthvalue != negated_op) {
            /* This conditional can be turned into an unconditional jump. */
            ins->info = MVM_op_get_op(MVM_OP_goto);
            ins->operands[0] = ins->operands[1];

            /* Since we have an unconditional jump now, we can remove the successor
             * that's in the linear_next. */
            MVM_spesh_manipulate_remove_successor(tc, bb, bb->linear_next);
        } else {
            /* This conditional can be dropped completely. */
            MVM_spesh_manipulate_remove_successor(tc, bb, ins->operands[1].ins_bb);
            MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
        }

        /* Since the CFG has changed, we may have some dead basic blocks; do
         * an elimination pass. */
        MVM_spesh_eliminate_dead_bbs(tc, g, 1);

        return;
    }

    /* Sometimes our code-gen ends up boxing an integer and immediately
     * calling if_o or unless_o on it. If we if_i/unless_i/... instead,
     * we can get rid of the unboxing and perhaps the boxing as well. */
    if ((ins->info->opcode == MVM_OP_if_o || ins->info->opcode == MVM_OP_unless_o)
            && flag_facts->flags & MVM_SPESH_FACT_KNOWN_BOX_SRC && flag_facts->writer) {
        /* We may have to go through several layers of set instructions to find
         * the proper writer. */
        MVMSpeshIns *cur = flag_facts->writer;
        while (cur && cur->info->opcode == MVM_OP_set) {
            cur = MVM_spesh_get_facts(tc, g, cur->operands[1])->writer;
        }

        if (cur) {
            MVMSpeshIns *safety_cur;
            MVMuint8 orig_operand_type = cur->info->operands[1] & MVM_operand_type_mask;
            MVMuint8 succ = 0;

            /* Now we have to be extra careful. Any operation that writes to
             * our "unboxed flag" register (in any register version) will be
             * trouble. Also, we'd have to take more care with PHI nodes,
             * which we'll just consider immediate failure for now. */

            safety_cur = ins;
            while (safety_cur) {
                if (safety_cur == cur) {
                    /* If we've made it to here without finding anything
                     * dangerous, we can consider this optimization
                     * a winner. */
                    break;
                }
                if (safety_cur->info->opcode == MVM_SSA_PHI) {
                    /* Oh dear god in heaven! A PHI! */
                    safety_cur = NULL;
                    break;
                }
                if (((safety_cur->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg)
                    && (safety_cur->operands[0].reg.orig == cur->operands[1].reg.orig)) {
                    /* Someone's clobbering our register between the boxing and
                     * our attempt to unbox it. We shall give up.
                     * Maybe in the future we can be clever/sneaky and use
                     * some other register for bridging the gap? */
                    safety_cur = NULL;
                    break;
                }
                safety_cur = safety_cur->prev;
            }

            if (safety_cur) {
                switch (orig_operand_type) {
                    case MVM_operand_int64:
                        ins->info = MVM_op_get_op(negated_op ? MVM_OP_unless_i : MVM_OP_if_i);
                        succ = 1;
                        break;
                    case MVM_operand_num64:
                        ins->info = MVM_op_get_op(negated_op ? MVM_OP_unless_n : MVM_OP_if_n);
                        succ = 1;
                        break;
                    case MVM_operand_str:
                        ins->info = MVM_op_get_op(negated_op ? MVM_OP_unless_s : MVM_OP_if_s);
                        succ = 1;
                        break;
                }

                if (succ) {
                    ins->operands[0] = cur->operands[1];
                    flag_facts->usages--;
                    MVM_spesh_get_and_use_facts(tc, g, cur->operands[1])->usages++;
                    optimize_iffy(tc, g, ins, bb);
                    return;
                }
            }
        }
    }

    /* If we know the type, we can devirtualize if_o and unless_o. */
    if ((ins->info->opcode == MVM_OP_if_o || ins->info->opcode == MVM_OP_unless_o) &&
            flag_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && flag_facts->type) {
        /* Go by boolification mode to pick a new instruction, if any. */
        MVMObject *type            = flag_facts->type;
        MVMBoolificationSpec *bs   = type->st->boolification_spec;
        MVMSpeshOperand  temp      = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
        MVMSpeshIns     *new_ins   = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));
        MVMuint8 guaranteed_concrete = flag_facts->flags & MVM_SPESH_FACT_CONCRETE;
        MVMuint8 mode = bs == NULL ? MVM_BOOL_MODE_NOT_TYPE_OBJECT : bs->mode;
        switch (mode) {
            case MVM_BOOL_MODE_ITER:
                if (!guaranteed_concrete)
                    return;
                if (flag_facts->flags & MVM_SPESH_FACT_ARRAY_ITER) {
                    new_ins->info = MVM_op_get_op(MVM_OP_sp_boolify_iter_arr);
                } else if (flag_facts->flags & MVM_SPESH_FACT_HASH_ITER) {
                    new_ins->info = MVM_op_get_op(MVM_OP_sp_boolify_iter_hash);
                } else {
                    new_ins->info = MVM_op_get_op(MVM_OP_sp_boolify_iter);
                }
                break;
            case MVM_BOOL_MODE_UNBOX_INT:
                if (!guaranteed_concrete)
                    return;
                new_ins->info = MVM_op_get_op(MVM_OP_unbox_i);
                break;
            /* We need to change the register type for our temporary register for this.
            case MVM_BOOL_MODE_UNBOX_NUM:
                new_ins->info = MVM_op_get_op(MVM_OP_unbox_i);
                break;
                */
            case MVM_BOOL_MODE_BIGINT:
                if (!guaranteed_concrete)
                    return;
                new_ins->info = MVM_op_get_op(MVM_OP_bool_I);
                break;
            case MVM_BOOL_MODE_HAS_ELEMS:
                if (!guaranteed_concrete)
                    return;
                new_ins->info = MVM_op_get_op(MVM_OP_elems);
                break;
            case MVM_BOOL_MODE_NOT_TYPE_OBJECT:
                new_ins->info = MVM_op_get_op(MVM_OP_isconcrete);
                break;
            default:
                return;
        }

        /* If we get here, we're inserting a new instruction that will do a
         * cheaper test and put the result into the temporary register. */
        new_ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand) * 2);
        new_ins->operands[0] = temp;
        new_ins->operands[1] = ins->operands[0];
        MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, new_ins);

        /* Tweak the new instruction into an if_i/unless_i on the temp. */
        ins->info = MVM_op_get_op(negated_op ? MVM_OP_unless_i : MVM_OP_if_i);
        ins->operands[0] = temp;
        MVM_spesh_get_facts(tc, g, temp)->usages++;
        MVM_spesh_use_facts(tc, g, flag_facts);
        MVM_spesh_manipulate_release_temp_reg(tc, g, temp);

        /* If the boolification mode was "not type object" then we might know
         * that from the facts, and may even be able to elimiante this
         * conditional altogether. */
        if (mode == MVM_BOOL_MODE_NOT_TYPE_OBJECT) {
            optimize_isconcrete(tc, g, new_ins);
            optimize_iffy(tc, g, ins, bb);
        }
    }
}

/* A not_i on a known value can be turned into a constant. */
static void optimize_not_i(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins, MVMSpeshBB *bb) {
    MVMSpeshFacts *src_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (src_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        /* Do the not_i. */
        MVMint64 value = src_facts->value.i;
        MVMint16 result = value ? 0 : 1;

        /* Turn the op into a constant and set result facts. */
        MVMSpeshFacts *dest_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        dest_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        dest_facts->value.i = result;
        ins->info = MVM_op_get_op(MVM_OP_const_i64_16);
        ins->operands[1].lit_i16 = result;

        /* This op no longer uses the source value. */
        src_facts->usages--;

        /* Need to depend on the source facts. */
        MVM_spesh_use_facts(tc, g, src_facts);
        MVM_spesh_facts_depend(tc, g, dest_facts, src_facts);
    }
}

/* objprimspec can be done at spesh-time if we know the type of something.
 * Another thing is, that if we rely on the type being known, we'll be assured
 * we'll have a guard that promises the object in question to be non-null. */
static void optimize_objprimspec(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);

    if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && obj_facts->type) {
        MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        ins->info                   = MVM_op_get_op(MVM_OP_const_i64_16);
        result_facts->flags        |= MVM_SPESH_FACT_KNOWN_VALUE;
        result_facts->value.i       = REPR(obj_facts->type)->get_storage_spec(tc, STABLE(obj_facts->type))->boxed_primitive;
        ins->operands[1].lit_i16    = result_facts->value.i;

        MVM_spesh_use_facts(tc, g, obj_facts);
        obj_facts->usages--;
    }
}

/* Optimizes a hllize instruction away if the type is known and already in the
 * right HLL, by turning it into a set. */
static void optimize_hllize(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && obj_facts->type) {
        if (STABLE(obj_facts->type)->hll_owner == g->sf->body.cu->body.hll_config) {
            ins->info = MVM_op_get_op(MVM_OP_set);

            MVM_spesh_use_facts(tc, g, obj_facts);

            copy_facts(tc, g, ins->operands[0], ins->operands[1]);
        }
    }
}

/* Turns a decont into a set, if we know it's not needed. Also make sure we
 * propagate any needed information. */
static void optimize_decont(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (obj_facts->flags & (MVM_SPESH_FACT_DECONTED | MVM_SPESH_FACT_TYPEOBJ)) {
        /* Know that we don't need to decont. */
        ins->info = MVM_op_get_op(MVM_OP_set);
        MVM_spesh_use_facts(tc, g, obj_facts);
        copy_facts(tc, g, ins->operands[0], ins->operands[1]);
        MVM_spesh_manipulate_remove_handler_successors(tc, bb);
    }
    else {
        /* Propagate facts if we know what this deconts to. */
        MVMSpeshFacts *res_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        int set_facts = 0;
        if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE) {
            res_facts->type   = obj_facts->decont_type;
            res_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
            set_facts = 1;
        }
        if (obj_facts->flags & MVM_SPESH_FACT_DECONT_CONCRETE) {
            res_facts->flags |= MVM_SPESH_FACT_CONCRETE;
            set_facts = 1;
        }
        else if (obj_facts->flags & MVM_SPESH_FACT_DECONT_TYPEOBJ) {
            res_facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
            set_facts = 1;
        }

        /* If it's a known type... */
        if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && obj_facts->type) {
            /* Can try to specialize the fetch. */
            MVMSTable *stable = STABLE(obj_facts->type);
            MVMContainerSpec const *contspec = stable->container_spec;
            if (contspec && contspec->fetch_never_invokes && contspec->spesh) {
                contspec->spesh(tc, stable, g, bb, ins);
                MVM_spesh_use_facts(tc, g, obj_facts);
            }

            /* If we didn't yet set facts, and the incoming type is a native
             * reference, then we can set facts based on knowing what it will
             * decont/box to. */
           if (!set_facts && stable->REPR->ID == MVM_REPR_ID_NativeRef) {
                MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)stable->REPR_data;
                MVMHLLConfig *hll = stable->hll_owner;
                MVMObject *out_type = NULL;
                if (!hll)
                    hll = g->sf->body.cu->body.hll_config;
                switch (repr_data->primitive_type) {
                    case MVM_STORAGE_SPEC_BP_INT:
                        out_type = hll->int_box_type;
                        break;
                    case MVM_STORAGE_SPEC_BP_NUM:
                        out_type = hll->num_box_type;
                        break;
                    case MVM_STORAGE_SPEC_BP_STR:
                        out_type = hll->str_box_type;
                        break;
                }
                if (out_type) {
                    res_facts->type = out_type;
                    res_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE;
                    set_facts = 1;
                }
            }
        }

        /* Depend on incoming facts if we used them. */
        if (set_facts)
            MVM_spesh_facts_depend(tc, g, res_facts, obj_facts);

        /* If the op is still a decont, then turn it into sp_decont, which
         * will at least not write log entries. */
        if (ins->info->opcode == MVM_OP_decont)
            ins->info = MVM_op_get_op(MVM_OP_sp_decont);
    }
}

/* Checks like iscont, iscont_[ins] and isrwcont can be done at spesh time. */
static void optimize_container_check(MVMThreadContext *tc, MVMSpeshGraph *g,
                                     MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    MVMint32 known_result = -1;
    if (ins->info->opcode == MVM_OP_isrwcont) {
        if (facts->flags & MVM_SPESH_FACT_RW_CONT)
            known_result = 1;
    }
    else {
        if (facts->flags & MVM_SPESH_FACT_TYPEOBJ) {
            /* Type object can never be a container. */
            known_result = 0;
        }
        else if ((facts->flags & MVM_SPESH_FACT_CONCRETE) &&
                (facts->flags & MVM_SPESH_FACT_KNOWN_TYPE)) {
            /* Know the type and know it's concrete. */
            MVMContainerSpec const *cs = facts->type->st->container_spec;
            if (!cs) {
                /* No container spec, so can be sure it's not a container. */
                known_result = 0;
            }
            else if (ins->info->opcode == MVM_OP_iscont) {
                /* General is container check, so answer is yes. */
                known_result = 1;
            }
            else {
                if (REPR(facts->type)->ID == MVM_REPR_ID_NativeRef) {
                    /* Which native ref primitive? */
                    switch (((MVMNativeRefREPRData *)STABLE(facts->type)->REPR_data)->primitive_type) {
                        case MVM_STORAGE_SPEC_BP_INT:
                            known_result = ins->info->opcode == MVM_OP_iscont_i;
                            break;
                        case MVM_STORAGE_SPEC_BP_NUM:
                            known_result = ins->info->opcode == MVM_OP_iscont_n;
                            break;
                        case MVM_STORAGE_SPEC_BP_STR:
                            known_result = ins->info->opcode == MVM_OP_iscont_s;
                            break;
                    }
                }
                else {
                    /* Need a native ref but don't have one, so certain no. */
                    known_result = 0;
                }
            }
        }
    }
    if (known_result != -1) {
        ins->info                   = MVM_op_get_op(MVM_OP_const_i64_16);
        result_facts->flags        |= MVM_SPESH_FACT_KNOWN_VALUE;
        result_facts->value.i       = known_result;
        ins->operands[1].lit_i16    = known_result;
        MVM_spesh_use_facts(tc, g, facts);
        facts->usages--;
    }
}

/* Optimize away assertparamcheck if we know it will pass. */
static void optimize_assertparamcheck(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE && facts->value.i) {
        MVM_spesh_use_facts(tc, g, facts);
        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
    }
}

static void optimize_can_op(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    /* This used to cause problems, Spesh: failed to fix up handlers (-1, 110, 110) */
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMString *method_name;
    MVMint64 can_result;

    if (ins->info->opcode == MVM_OP_can_s) {
        MVMSpeshFacts *name_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);
        if (!(name_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE)) {
            return;
        }
        method_name = name_facts->value.s;

        name_facts->usages--;
        ins->info = MVM_op_get_op(MVM_OP_can);
        ins->operands[2].lit_str_idx = name_facts->writer->operands[1].lit_str_idx;
    } else {
        method_name = MVM_spesh_get_string(tc, g, ins->operands[2]);
    }

    if (!(obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) || !obj_facts->type) {
        return;
    }

    if (MVM_is_null(tc, obj_facts->type))
        can_result = 0; /* VMNull can't have any methods. */
    else
        can_result = MVM_spesh_try_can_method(tc, obj_facts->type, method_name);

    if (can_result == -1) {
        return;
    } else {
        MVMSpeshFacts *result_facts;

        if (ins->info->opcode == MVM_OP_can_s)
            MVM_spesh_get_facts(tc, g, ins->operands[2])->usages--;

        result_facts                = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        ins->info                   = MVM_op_get_op(MVM_OP_const_i64_16);
        result_facts->flags        |= MVM_SPESH_FACT_KNOWN_VALUE;
        ins->operands[1].lit_i16    = can_result;
        result_facts->value.i       = can_result;

        obj_facts->usages--;
        MVM_spesh_use_facts(tc, g, obj_facts);
    }
}

/* If we have a const_i and a coerce_in, we can emit a const_n instead. */
static void optimize_coerce(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);

    if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        MVMnum64 result = facts->value.i;

        MVM_spesh_use_facts(tc, g, facts);
        facts->usages--;

        ins->info = MVM_op_get_op(MVM_OP_const_n64);
        ins->operands[1].lit_n64 = result;

        result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        result_facts->value.n = result;
    }
}

/* If we know the type of a significant operand, we might try to specialize by
 * representation. */
static void optimize_repr_op(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                             MVMSpeshIns *ins, MVMint32 type_operand) {
    /* Immediately mark guards as used, as the JIT would like to devirtualize
     * repr ops later and we don't want guards to be thrown out before that */
    MVMSpeshFacts *facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[type_operand]);
    if (facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && facts->type)
        if (REPR(facts->type)->spesh) {
            REPR(facts->type)->spesh(tc, STABLE(facts->type), g, bb, ins);
            MVM_spesh_use_facts(tc, g, facts);
        }
}

/* smrt_strify and smrt_numify can turn into unboxes, but at least
 * for smrt_numify it's "complicated". Also, later when we know how
 * to put new invocations into spesh'd code, we could make direct
 * invoke calls to the .Str and .Num methods. */
static void optimize_smart_coerce(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);

    MVMuint16 is_strify = ins->info->opcode == MVM_OP_smrt_strify;

    if (facts->flags & (MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE) && facts->type) {
        const MVMStorageSpec *ss;
        MVMint64 can_result;

        ss = REPR(facts->type)->get_storage_spec(tc, STABLE(facts->type));

        if (is_strify && ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR) {
            MVM_spesh_use_facts(tc, g, facts);

            ins->info = MVM_op_get_op(MVM_OP_unbox_s);
            /* And now that we have a repr op, we can try to optimize
             * it even further. */
            optimize_repr_op(tc, g, bb, ins, 1);

            return;
        }
        can_result = MVM_spesh_try_can_method(tc, facts->type,
                is_strify ? tc->instance->str_consts.Str : tc->instance->str_consts.Num);

        if (can_result == -1) {
            /* Couldn't safely figure out if the type has a Str method or not. */
            return;
        } else if (can_result == 0) {
            MVM_spesh_use_facts(tc, g, facts);
            /* We can't .Str this object, so we'll duplicate the "guessing"
             * logic from smrt_strify here to remove indirection. */
            if (is_strify && REPR(facts->type)->ID == MVM_REPR_ID_MVMException) {
                MVMSpeshOperand *operands  = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ) * 3);
                MVMSpeshOperand *old_opers = ins->operands;

                ins->info = MVM_op_get_op(MVM_OP_sp_get_s);

                ins->operands = operands;

                operands[0] = old_opers[0];
                operands[1] = old_opers[1];
                operands[2].lit_i16 = offsetof( MVMException, body.message );
            } else if(ss->can_box & (MVM_STORAGE_SPEC_CAN_BOX_NUM | MVM_STORAGE_SPEC_CAN_BOX_INT)) {
                MVMuint16 register_type =
                    ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_INT ? MVM_reg_int64 : MVM_reg_num64;

                MVMSpeshIns     *new_ins   = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));
                MVMSpeshOperand *operands  = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ) * 2);
                MVMSpeshOperand  temp      = MVM_spesh_manipulate_get_temp_reg(tc, g, register_type);
                MVMSpeshOperand  orig_dst  = ins->operands[0];

                ins->info = MVM_op_get_op(register_type == MVM_reg_num64 ? MVM_OP_unbox_n : MVM_OP_unbox_i);
                ins->operands[0] = temp;

                if (is_strify)
                    new_ins->info = MVM_op_get_op(register_type == MVM_reg_num64 ? MVM_OP_coerce_ns : MVM_OP_coerce_is);
                else
                    new_ins->info = MVM_op_get_op(register_type == MVM_reg_num64 ? MVM_OP_set : MVM_OP_coerce_in);
                new_ins->operands = operands;
                operands[0] = orig_dst;
                operands[1] = temp;

                /* We can directly "eliminate" a set instruction here. */
                if (new_ins->info->opcode != MVM_OP_set) {
                    MVM_spesh_manipulate_insert_ins(tc, bb, ins, new_ins);

                    MVM_spesh_get_facts(tc, g, temp)->usages++;
                } else {
                    ins->operands[0] = orig_dst;
                }

                /* Finally, let's try to optimize the unboxing REPROp. */
                optimize_repr_op(tc, g, bb, ins, 1);

                /* And as a last clean-up step, we release the temporary register. */
                MVM_spesh_manipulate_release_temp_reg(tc, g, temp);

                return;
            } else if (!is_strify && (REPR(facts->type)->ID == MVM_REPR_ID_VMArray ||
                                     (REPR(facts->type)->ID == MVM_REPR_ID_MVMHash))) {
                /* A smrt_numify on an array or hash can be replaced by an
                 * elems operation, that can then be optimized by our
                 * versatile and dilligent friend optimize_repr_op. */

                MVMSpeshIns     *new_ins   = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));
                MVMSpeshOperand *operands  = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ) * 2);
                MVMSpeshOperand  temp      = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
                MVMSpeshOperand  orig_dst  = ins->operands[0];

                ins->info = MVM_op_get_op(MVM_OP_elems);
                ins->operands[0] = temp;

                new_ins->info = MVM_op_get_op(MVM_OP_coerce_in);
                new_ins->operands = operands;
                operands[0] = orig_dst;
                operands[1] = temp;

                MVM_spesh_manipulate_insert_ins(tc, bb, ins, new_ins);

                optimize_repr_op(tc, g, bb, ins, 1);

                MVM_spesh_get_facts(tc, g, temp)->usages++;
                MVM_spesh_manipulate_release_temp_reg(tc, g, temp);
                return;
            }
        } else if (can_result == 1) {
            /* When we know how to generate additional callsites, we could
             * make an invocation to .Str or .Num here and perhaps have it
             * in-lined. */
        }
    }
}

/* boolification has a major indirection, which we can spesh away.
 * Afterwards, we may be able to spesh even further, so we defer
 * to other optimization methods. */
static void optimize_istrue_isfalse(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMuint8 negated_op;
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (ins->info->opcode == MVM_OP_istrue) {
        negated_op = 0;
    } else if (ins->info->opcode == MVM_OP_isfalse) {
        negated_op = 1;
    } else {
        return;
    }

    /* Let's try to figure out the boolification spec. */
    if (facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        MVMBoolificationSpec *bs = STABLE(facts->type)->boolification_spec;
        MVMSpeshOperand  orig    = ins->operands[0];
        MVMSpeshOperand  temp;

        if (negated_op)
           temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);

        switch (bs == NULL ? MVM_BOOL_MODE_NOT_TYPE_OBJECT : bs->mode) {
            case MVM_BOOL_MODE_UNBOX_INT:
                /* This optimization can only handle values known to be concrete. */
                if (!(facts->flags & MVM_SPESH_FACT_CONCRETE)) {
                    return;
                }
                /* We can just unbox the int and pretend it's a bool. */
                ins->info = MVM_op_get_op(MVM_OP_unbox_i);
                if (negated_op)
                    ins->operands[0] = temp;
                /* And then we might be able to optimize this even further. */
                optimize_repr_op(tc, g, bb, ins, 1);
                break;
            case MVM_BOOL_MODE_NOT_TYPE_OBJECT:
                /* This is the same as isconcrete. */
                ins->info = MVM_op_get_op(MVM_OP_isconcrete);
                if (negated_op)
                    ins->operands[0] = temp;
                /* And now defer another bit of optimization */
                optimize_isconcrete(tc, g, ins);
                break;
            /* TODO implement MODE_UNBOX_NUM and the string ones */
            default:
                return;
        }
        /* Now we can take care of the negation. */
        if (negated_op) {
            /* Insert a not_i instruction that negates temp. This not_i is
             * subject to further optimization in the case that temp has a
             * known value set on it. */
            MVMSpeshIns     *new_ins   = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));
            MVMSpeshOperand *operands  = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ) * 2);
            new_ins->info = MVM_op_get_op(MVM_OP_not_i);
            new_ins->operands = operands;
            operands[0] = orig;
            operands[1] = temp;
            MVM_spesh_manipulate_insert_ins(tc, bb, ins, new_ins);
            MVM_spesh_get_facts(tc, g, temp)->usages++;
            MVM_spesh_manipulate_release_temp_reg(tc, g, temp);
        }

        MVM_spesh_use_facts(tc, g, facts);
    }
}

/* Turns a getlex instruction into getlex_o or getlex_ins depending on type;
 * these get rid of some branching as well as don't log. */
static void optimize_getlex(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMuint16 *lexical_types;
    MVMuint16 i;
    MVMStaticFrame *sf = g->sf;
    for (i = 0; i < ins->operands[1].lex.outers; i++)
        sf = sf->body.outer;
    lexical_types = sf == g->sf && g->lexical_types
        ? g->lexical_types
        : sf->body.lexical_types;
    ins->info = MVM_op_get_op(lexical_types[ins->operands[1].lex.idx] == MVM_reg_obj
        ? MVM_OP_sp_getlex_o
        : MVM_OP_sp_getlex_ins);
}

/* Transforms a late-bound lexical lookup into a constant. */
static void lex_to_constant(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
                            MVMObject *log_obj) {
    MVMSpeshFacts *facts;

    /* Place in a spesh slot. */
    MVMuint16 ss = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
        (MVMCollectable *)log_obj);

    /* Transform lookup instruction into spesh slot read. */
    MVM_spesh_get_facts(tc, g, ins->operands[1])->usages--;
    ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
    ins->operands[1].lit_i16 = ss;

    /* Set up facts. */
    facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    facts->flags  |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_KNOWN_VALUE;
    facts->type    = STABLE(log_obj)->WHAT;
    facts->value.o = log_obj;
    if (IS_CONCRETE(log_obj)) {
        facts->flags |= MVM_SPESH_FACT_CONCRETE;
        if (!STABLE(log_obj)->container_spec)
            facts->flags |= MVM_SPESH_FACT_DECONTED;
    }
    else {
        facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
    }
}

/* Optimizes away a lexical lookup when we know the value won't change from
 * the logged one. */
static void optimize_getlex_known(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                  MVMSpeshIns *ins) {
    /* Try to find logged offset. */
    MVMSpeshAnn *ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_LOGGED)
            break;
        ann = ann->next;
    }
    if (ann) {
        /* See if we can find a logged static value. */
        MVMSpeshStats *ss = g->sf->body.spesh->body.spesh_stats;
        MVMuint32 n = ss->num_static_values;
        MVMuint32 i;
        for (i = 0; i < n; i++) {
            if (ss->static_values[i].bytecode_offset == ann->data.bytecode_offset) {
                MVMObject *log_obj = ss->static_values[i].value;
                if (log_obj)
                    lex_to_constant(tc, g, ins, log_obj);
                return;
            }
        }
    }
}

/* Optimizes away a lexical lookup when we know the value won't change for a
 * given invocant type (this relies on us being in a typed specialization). */
static void optimize_getlex_per_invocant(MVMThreadContext *tc, MVMSpeshGraph *g,
                                         MVMSpeshBB *bb, MVMSpeshIns *ins,
                                         MVMSpeshPlanned *p) {
    MVMSpeshAnn *ann;

    /* Can only do this when we've specialized on the first argument type. */
    if (!g->specialized_on_invocant)
        return;

    /* Try to find logged offset. */
    ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_LOGGED)
            break;
        ann = ann->next;
    }
    if (ann) {
        MVMuint32 i;
        for (i = 0; i < p->num_type_stats; i++) {
            MVMSpeshStatsByType *ts = p->type_stats[i];
            MVMuint32 j;
            for (j = 0; j < ts->num_by_offset; j++) {
                if (ts->by_offset[j].bytecode_offset == ann->data.bytecode_offset) {
                    if (ts->by_offset[j].num_types) {
                        MVMObject *log_obj = ts->by_offset[j].types[0].type;
                        if (log_obj && !ts->by_offset[j].types[0].type_concrete)
                            lex_to_constant(tc, g, ins, log_obj);
                        return;
                    }
                    break;
                }
            }
        }
    }
}

/* Determines if there's a matching spesh candidate for a callee and a given
 * set of argument info. */
static MVMint32 try_find_spesh_candidate(MVMThreadContext *tc, MVMStaticFrame *sf,
                                         MVMSpeshCallInfo *arg_info,
                                         MVMSpeshStatsType *type_tuple) {
    MVMSpeshArgGuard *ag = sf->body.spesh->body.spesh_arg_guard;
    return type_tuple
        ? MVM_spesh_arg_guard_run_types(tc, ag, arg_info->cs, type_tuple)
        : MVM_spesh_arg_guard_run_callinfo(tc, ag, arg_info);
}

/* Given an invoke instruction, find its logging bytecode offset. Returns 0
 * if not found. */
MVMuint32 find_invoke_offset(MVMThreadContext *tc, MVMSpeshIns *ins) {
    MVMSpeshAnn *ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_LOGGED)
            return ann->data.bytecode_offset;
        ann = ann->next;
    }
    return 0;
}

/* Given an instruction, finds the deopt target on it. Panics if there is not
 * one there. */
MVMuint32 find_deopt_target(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMuint32 deopt_target;
    MVMSpeshAnn *deopt_ann = ins->annotations;
    while (deopt_ann) {
        if (deopt_ann->type == MVM_SPESH_ANN_DEOPT_ONE_INS)
            return g->deopt_addrs[2 * deopt_ann->data.deopt_idx];
        deopt_ann = deopt_ann->next;
    }
    MVM_panic(1, "Spesh: unexpectedly missing deopt annotation on prepargs");
}

/* Given a callsite instruction, finds the type tuples there and checks if
 * there is a relatively stable one. */
static MVMSpeshStatsType * find_invokee_type_tuple(MVMThreadContext *tc, MVMSpeshGraph *g,
                                                   MVMSpeshBB *bb, MVMSpeshIns *ins,
                                                   MVMSpeshPlanned *p, MVMCallsite *expect_cs) {
    MVMuint32 i;
    MVMSpeshStatsType *best_result = NULL;
    MVMuint32 best_result_hits = 0;
    MVMuint32 total_hits = 0;
    size_t tt_size = expect_cs->flag_count * sizeof(MVMSpeshStatsType);

    /* First try to find logging bytecode offset. */
    MVMuint32 invoke_offset = find_invoke_offset(tc, ins);
    if (!invoke_offset)
        return NULL;

    /* Now look for the best type tuple. */
    for (i = 0; i < p->num_type_stats; i++) {
        MVMSpeshStatsByType *ts = p->type_stats[i];
        MVMuint32 j;
        for (j = 0; j < ts->num_by_offset; j++) {
            if (ts->by_offset[j].bytecode_offset == invoke_offset) {
                MVMSpeshStatsByOffset *by_offset = &(ts->by_offset[j]);
                MVMuint32 k;
                for (k = 0; k < by_offset->num_type_tuples; k++) {
                    MVMSpeshStatsTypeTupleCount *tt = &(by_offset->type_tuples[k]);

                    /* Callsite should always match but skip if not. */
                    if (tt->cs != expect_cs)
                        continue;

                    /* Add hits to total we've seen. */
                    total_hits += tt->count;

                    /* If it's the same as the best so far, add hits. */
                    if (best_result && memcmp(best_result, tt->arg_types, tt_size) == 0) {
                        best_result_hits += tt->count;
                    }

                    /* Otherwise, if it beats the best result in hits, use. */
                    else if (tt->count > best_result_hits) {
                        best_result = tt->arg_types;
                        best_result_hits = tt->count;
                    }
                }
            }
        }
    }

    /* If the type tuple is used consistently enough, return it. */
    return total_hits && (100 * best_result_hits) / total_hits >= MVM_SPESH_CALLSITE_STABLE_PERCENT
        ? best_result
        : NULL;
}

/* Inserts an argument type guard as suggested by a logged type tuple. */
static void insert_arg_type_guard(MVMThreadContext *tc, MVMSpeshGraph *g,
                                  MVMSpeshStatsType *type_info,
                                  MVMSpeshCallInfo *arg_info, MVMuint32 arg_idx) {
    /* Insert guard before prepargs (this means they stack up in order). */
    MVMuint32 deopt_target = find_deopt_target(tc, g, arg_info->prepargs_ins);
    MVMSpeshIns *guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(type_info->type_concrete
        ? MVM_OP_sp_guardconc
        : MVM_OP_sp_guardtype);
    guard->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
    guard->operands[0] = arg_info->arg_ins[arg_idx]->operands[1];
    guard->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
        (MVMCollectable *)type_info->type->st);
    guard->operands[2].lit_ui32 = deopt_target;
    MVM_spesh_manipulate_insert_ins(tc, arg_info->prepargs_bb,
        arg_info->prepargs_ins->prev, guard);

    /* Also give the instruction a deopt annotation. */
    MVM_spesh_graph_add_deopt_annotation(tc, g, guard, deopt_target,
        MVM_SPESH_ANN_DEOPT_ONE_INS);
}

/* Inserts an argument decont type guard as suggested by a logged type tuple. */
static void insert_arg_decont_type_guard(MVMThreadContext *tc, MVMSpeshGraph *g,
                                         MVMSpeshStatsType *type_info,
                                         MVMSpeshCallInfo *arg_info, MVMuint32 arg_idx) {
    MVMSpeshIns *decont, *guard;
    MVMuint32 deopt_target;

    /* We need a temporary register to decont into. */
    MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);

    /* Insert the decont, then try to optimize it into something cheaper. */
    decont = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    decont->info = MVM_op_get_op(MVM_OP_decont);
    decont->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    decont->operands[0] = temp;
    decont->operands[1] = arg_info->arg_ins[arg_idx]->operands[1];
    MVM_spesh_manipulate_insert_ins(tc, arg_info->prepargs_bb,
        arg_info->prepargs_ins->prev, decont);
    MVM_spesh_get_facts(tc, g, temp)->usages++;
    optimize_decont(tc, g, arg_info->prepargs_bb, decont);

    /* Guard the decontainerized value. */
    deopt_target = find_deopt_target(tc, g, arg_info->prepargs_ins);
    guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(type_info->decont_type_concrete
        ? MVM_OP_sp_guardconc
        : MVM_OP_sp_guardtype);
    guard->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
    guard->operands[0] = temp;
    guard->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
        (MVMCollectable *)type_info->decont_type->st);
    guard->operands[2].lit_ui32 = deopt_target;
    MVM_spesh_manipulate_insert_ins(tc, arg_info->prepargs_bb,
        arg_info->prepargs_ins->prev, guard);

    /* Also give the instruction a deopt annotation. */
    MVM_spesh_graph_add_deopt_annotation(tc, g, guard, deopt_target,
        MVM_SPESH_ANN_DEOPT_ONE_INS);

    /* Release the temp register. */
    MVM_spesh_manipulate_release_temp_reg(tc, g, temp);
}

/* Look through the call info and the type tuple, see what guards we are
 * missing, and insert them. */
static void check_and_tweak_arg_guards(MVMThreadContext *tc, MVMSpeshGraph *g,
                                       MVMSpeshStatsType *type_tuple,
                                       MVMSpeshCallInfo *arg_info) {
    MVMuint32 n = arg_info->cs->flag_count;
    MVMuint32 arg_idx = 0;
    MVMuint32 i;
    for (i = 0; i < n; i++, arg_idx++) {
        if (arg_info->cs->arg_flags[i] & MVM_CALLSITE_ARG_NAMED)
            arg_idx++;
        if (arg_info->cs->arg_flags[i] & MVM_CALLSITE_ARG_OBJ) {
            MVMObject *t_type = type_tuple[i].type;
            MVMObject *t_decont_type = type_tuple[i].decont_type;
            if (t_type) {
                /* Add a guard unless the facts already match. */
                MVMSpeshFacts *arg_facts = arg_info->arg_facts[arg_idx];
                MVMuint32 need_guard = !arg_facts ||
                    !(arg_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) ||
                    arg_facts->type != t_type ||
                    type_tuple[i].type_concrete
                        && !(arg_facts->flags & MVM_SPESH_FACT_CONCRETE) ||
                    !type_tuple[i].type_concrete
                        && !(arg_facts->flags & MVM_SPESH_FACT_TYPEOBJ);
                if (need_guard)
                    insert_arg_type_guard(tc, g, &type_tuple[i], arg_info, arg_idx);
            }
            if (t_decont_type)
                insert_arg_decont_type_guard(tc, g, &type_tuple[i], arg_info, arg_idx);
        }
    }
}

/* Sees if any static frames were logged for this invoke instruction, and
 * if so checks if there was a stable one. A static frame chosen by multi
 * dispatch will for now never count as stable, as we don't have a good way
 * to handle this situation yet and trying results in deopts. */
MVMStaticFrame * find_invokee_static_frame(MVMThreadContext *tc, MVMSpeshPlanned *p,
                                           MVMSpeshIns *ins) {
    MVMuint32 i;
    MVMStaticFrame *best_result = NULL;
    MVMuint32 best_result_hits = 0;
    MVMuint32 best_result_was_multi_hits = 0;
    MVMuint32 total_hits = 0;

    /* First try to find logging bytecode offset. */
    MVMuint32 invoke_offset = find_invoke_offset(tc, ins);
    if (!invoke_offset)
        return NULL;

    /* Now look for a stable invokee. */
    for (i = 0; i < p->num_type_stats; i++) {
        MVMSpeshStatsByType *ts = p->type_stats[i];
        MVMuint32 j;
        for (j = 0; j < ts->num_by_offset; j++) {
            if (ts->by_offset[j].bytecode_offset == invoke_offset) {
                MVMSpeshStatsByOffset *by_offset = &(ts->by_offset[j]);
                MVMuint32 k;
                for (k = 0; k < by_offset->num_invokes; k++) {
                    MVMSpeshStatsInvokeCount *ic = &(by_offset->invokes[k]);

                    /* Add hits to total we've seen. */
                    total_hits += ic->count;

                    /* If it's the same as the best so far, add hits. */
                    if (best_result && ic->sf == best_result) {
                        best_result_hits += ic->count;
                        best_result_was_multi_hits += ic->was_multi_count;
                    }

                    /* Otherwise, if it beats the best result in hits, use. */
                    else if (ic->count > best_result_hits) {
                        best_result = ic->sf;
                        best_result_hits = ic->count;
                        best_result_was_multi_hits = ic->was_multi_count;
                    }
                }
            }
        }
    }

    /* If the chosen frame was a multi, give up. */
    if (best_result_was_multi_hits)
        return NULL;

    /* If the static frame is consistent enough, return it. */
    return total_hits && (100 * best_result_hits) / total_hits >= MVM_SPESH_CALLSITE_STABLE_PERCENT
        ? best_result
        : NULL;
}

/* Inserts resolution of the invokee to an MVMCode and the guard on the
 * invocation, and then tweaks the invoke instruction to use the resolved
 * code object (for the case it is further optimized into a fast invoke). */
static void tweak_for_target_sf(MVMThreadContext *tc, MVMSpeshGraph *g,
                                MVMStaticFrame *target_sf, MVMSpeshIns *ins,
                                MVMSpeshCallInfo *arg_info, MVMSpeshOperand temp) {
    MVMSpeshIns *guard, *resolve;
    MVMuint32 deopt_target;

    /* Work out which operand of the invoke instruction has the invokee. */
    MVMuint32 inv_code_index = ins->info->opcode == MVM_OP_invoke_v ? 0 : 1;

    /* Insert instruction to resolve any code wrapper into the MVMCode before
     * prepargs. */
    resolve = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    resolve->info = MVM_op_get_op(MVM_OP_sp_resolvecode);
    resolve->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    resolve->operands[0] = temp;
    resolve->operands[1] = ins->operands[inv_code_index];
    MVM_spesh_manipulate_insert_ins(tc, arg_info->prepargs_bb,
        arg_info->prepargs_ins->prev, resolve);

    /* Insert guard instruction before the prepargs. */
    deopt_target = find_deopt_target(tc, g, arg_info->prepargs_ins);
    guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(MVM_OP_sp_guardsf);
    guard->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
    guard->operands[0] = temp;
    guard->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
        (MVMCollectable *)target_sf);
    guard->operands[2].lit_ui32 = deopt_target;
    MVM_spesh_manipulate_insert_ins(tc, arg_info->prepargs_bb,
        arg_info->prepargs_ins->prev, guard);

    /* Also give the guard instruction a deopt annotation. */
    MVM_spesh_graph_add_deopt_annotation(tc, g, guard, deopt_target,
        MVM_SPESH_ANN_DEOPT_ONE_INS);

    /* Make the invoke instruction call the resolved result. */
    ins->operands[inv_code_index] = temp;

    /* Bump temp usage (one for the guard, one for the invoke). */
    MVM_spesh_get_facts(tc, g, temp)->usages += 2;
}

/* Drives optimization of a call. */
static void optimize_call(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                          MVMSpeshIns *ins, MVMSpeshPlanned *p, MVMint32 callee_idx,
                          MVMSpeshCallInfo *arg_info) {
    MVMSpeshStatsType *stable_type_tuple;
    MVMObject *target = NULL;
    MVMuint32 num_arg_slots;
    MVMSpeshOperand code_temp;

    /* Check we know what we're going to be invoking. */
    MVMSpeshFacts *callee_facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[callee_idx]);
    MVMObject *code = NULL;
    MVMStaticFrame *target_sf = NULL;
    MVMint32 have_code_temp = 0;
    if (callee_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        /* Already know the target code object based on existing guards or
         * a static value. */
        code = callee_facts->value.o;
    }
    else {
        /* See if there is a stable static frame at the callsite. If so, add
         * the resolution and guard instruction. Note that we must keep the
         * temporary alive throughout the whole guard and invocation sequence,
         * as an inline may use it during deopt to find the code ref. */
        target_sf = find_invokee_static_frame(tc, p, ins);
        if (target_sf) {
            code_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
            have_code_temp = 1;
            tweak_for_target_sf(tc, g, target_sf, ins, arg_info, code_temp);
        }
    }
    if (!code && !target_sf)
        return;

    /* See if there's a stable type tuple at this callsite. If so, see if we
     * are missing any guards required, and try to insert them if so. Only do
     * this if the callsite isn't too big for arg_info. */
    num_arg_slots = arg_info->cs->num_pos +
        2 * (arg_info->cs->flag_count - arg_info->cs->num_pos);
    stable_type_tuple = num_arg_slots <= MAX_ARGS_FOR_OPT
        ? find_invokee_type_tuple(tc, g, bb, ins, p, arg_info->cs)
        : NULL;
    if (stable_type_tuple)
        check_and_tweak_arg_guards(tc, g, stable_type_tuple, arg_info);

    /* If we don't have a target static frame from speculation, check on what
     * we're going to be invoking and see if we can further resolve it. */
    if (!target_sf) {
        if (REPR(code)->ID == MVM_REPR_ID_MVMCode) {
            /* Already have a code object we know we'll call. */
            target = code;
        }
        else if (IS_CONCRETE(code) && STABLE(code)->invocation_spec) {
            /* What kind of invocation will it be? */
            MVMInvocationSpec *is = STABLE(code)->invocation_spec;
            if (!MVM_is_null(tc, is->md_class_handle)) {
                /* Multi-dispatch. Check if this is a dispatch where we can
                 * use the cache directly. */
                MVMRegister dest;
                REPR(code)->attr_funcs.get_attribute(tc,
                    STABLE(code), code, OBJECT_BODY(code),
                    is->md_class_handle, is->md_valid_attr_name,
                    is->md_valid_hint, &dest, MVM_reg_int64);
                if (dest.i64) {
                    /* Yes. Try to obtain the cache. */
                    REPR(code)->attr_funcs.get_attribute(tc,
                        STABLE(code), code, OBJECT_BODY(code),
                        is->md_class_handle, is->md_cache_attr_name,
                        is->md_cache_hint, &dest, MVM_reg_obj);
                    if (!MVM_is_null(tc, dest.o)) {
                        MVMObject *found = MVM_multi_cache_find_spesh(tc, dest.o,
                            arg_info, stable_type_tuple);
                        if (found) {
                            /* Found it. Is it a code object already, or do we
                             * have futher unpacking to do? */
                            if (REPR(found)->ID == MVM_REPR_ID_MVMCode) {
                                target = found;
                            }
                            else if (STABLE(found)->invocation_spec) {
                                MVMInvocationSpec *m_is = STABLE(found)->invocation_spec;
                                if (!MVM_is_null(tc, m_is->class_handle)) {
                                    REPR(found)->attr_funcs.get_attribute(tc,
                                        STABLE(found), found, OBJECT_BODY(found),
                                        is->class_handle, is->attr_name,
                                        is->hint, &dest, MVM_reg_obj);
                                    if (REPR(dest.o)->ID == MVM_REPR_ID_MVMCode)
                                        target = dest.o;
                                }
                            }
                        }
                    }
                }
                else if (!MVM_is_null(tc, is->class_handle)) {
                    /* This type of code object supports multi-dispatch,
                     * but we actually have a single dispatch routine. */
                    MVMRegister dest;
                    REPR(code)->attr_funcs.get_attribute(tc,
                        STABLE(code), code, OBJECT_BODY(code),
                        is->class_handle, is->attr_name,
                        is->hint, &dest, MVM_reg_obj);
                    if (REPR(dest.o)->ID == MVM_REPR_ID_MVMCode)
                        target = dest.o;
                }
            }
            else if (!MVM_is_null(tc, is->class_handle)) {
                /* Single dispatch; retrieve the code object. */
                MVMRegister dest;
                REPR(code)->attr_funcs.get_attribute(tc,
                    STABLE(code), code, OBJECT_BODY(code),
                    is->class_handle, is->attr_name,
                    is->hint, &dest, MVM_reg_obj);
                if (REPR(dest.o)->ID == MVM_REPR_ID_MVMCode)
                    target = dest.o;
            }
        }
        if (!target || !IS_CONCRETE(target))
            return;

        /* If we resolved to something better than the code object, then add
         * the resolved item in a spesh slot and insert a lookup. */
        if (target != code && !((MVMCode *)target)->body.is_compiler_stub) {
            MVMSpeshIns *pa_ins = arg_info->prepargs_ins;
            MVMSpeshIns *ss_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
            ss_ins->info        = MVM_op_get_op(MVM_OP_sp_getspeshslot);
            ss_ins->operands    = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
            ss_ins->operands[0] = ins->operands[callee_idx];
            ss_ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                (MVMCollectable *)target);
            /* Basically, we're inserting between arg* and invoke_*.
             * Since invoke_* directly uses the code in the register,
             * the register must have held the code during the arg*
             * instructions as well, because none of {prepargs, arg*}
             * can manipulate the register that holds the code.
             *
             * To make a long story very short, I think it should be
             * safe to move the sp_getspeshslot to /before/ the
             * prepargs instruction. And this is very convenient for
             * me, as it allows me to treat set of prepargs, arg*,
             * invoke, as a /single node/, and this greatly simplifies
             * invoke JIT compilation */

            MVM_spesh_manipulate_insert_ins(tc, bb, pa_ins->prev, ss_ins);
            /* XXX TODO: Do this differently so we can eliminate the original
             * lookup of the enclosing code object also. */
        }

        /* Extract the target static frame from the target code object; we
         * will work in terms of that from here on. */
        target_sf = ((MVMCode *)target)->body.sf;
    }

    /* See if we can point the call at a particular specialization. */
    if (target_sf->body.instrumentation_level == tc->instance->instrumentation_level) {
        MVMint32 spesh_cand = try_find_spesh_candidate(tc, target_sf, arg_info,
            stable_type_tuple);
        if (spesh_cand >= 0) {
            /* Yes. Will we be able to inline? */
            char *no_inline_reason = NULL;
            MVMSpeshGraph *inline_graph = MVM_spesh_inline_try_get_graph(tc, g,
                target_sf, target_sf->body.spesh->body.spesh_candidates[spesh_cand],
                ins, &no_inline_reason);
#if MVM_LOG_INLINES
            {
                char *c_name_i = MVM_string_utf8_encode_C_string(tc, target_sf->body.name);
                char *c_cuid_i = MVM_string_utf8_encode_C_string(tc, target_sf->body.cuuid);
                char *c_name_t = MVM_string_utf8_encode_C_string(tc, g->sf->body.name);
                char *c_cuid_t = MVM_string_utf8_encode_C_string(tc, g->sf->body.cuuid);
                if (inline_graph) {
                    fprintf(stderr, "Can inline %s (%s) into %s (%s)\n",
                        c_name_i, c_cuid_i, c_name_t, c_cuid_t);
                }
                else {
                    fprintf(stderr, "Can NOT inline %s (%s) into %s (%s): %s\n",
                        c_name_i, c_cuid_i, c_name_t, c_cuid_t, no_inline_reason);
                }
                MVM_free(c_name_i);
                MVM_free(c_cuid_i);
                MVM_free(c_name_t);
                MVM_free(c_cuid_t);
            }
#endif
            if (inline_graph) {
                /* Yes, have inline graph, so go ahead and do it. Make sure we
                 * keep the code ref reg alive by giving it a usage count as
                 * it will be referenced from the deopt table. */
                MVMSpeshOperand code_ref_reg = ins->info->opcode == MVM_OP_invoke_v
                        ? ins->operands[0]
                        : ins->operands[1];
                MVM_spesh_facts_discover(tc, inline_graph, p);
                MVM_spesh_get_facts(tc, g, code_ref_reg)->usages++;
                MVM_spesh_inline(tc, g, arg_info, bb, ins, inline_graph, target_sf,
                    code_ref_reg);
            }
            else {
                /* Can't inline, so just identify candidate. */
                MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                if (ins->info->opcode == MVM_OP_invoke_v) {
                    new_operands[0]         = ins->operands[0];
                    new_operands[1].lit_i16 = spesh_cand;
                    ins->operands           = new_operands;
                    ins->info               = MVM_op_get_op(MVM_OP_sp_fastinvoke_v);
                }
                else {
                    new_operands[0]         = ins->operands[0];
                    new_operands[1]         = ins->operands[1];
                    new_operands[2].lit_i16 = spesh_cand;
                    ins->operands           = new_operands;
                    switch (ins->info->opcode) {
                    case MVM_OP_invoke_i:
                        ins->info = MVM_op_get_op(MVM_OP_sp_fastinvoke_i);
                        break;
                    case MVM_OP_invoke_n:
                        ins->info = MVM_op_get_op(MVM_OP_sp_fastinvoke_n);
                        break;
                    case MVM_OP_invoke_s:
                        ins->info = MVM_op_get_op(MVM_OP_sp_fastinvoke_s);
                        break;
                    case MVM_OP_invoke_o:
                        ins->info = MVM_op_get_op(MVM_OP_sp_fastinvoke_o);
                        break;
                    default:
                        MVM_oops(tc, "Spesh: unhandled invoke instruction");
                    }
                }
            }
        }
    }

    /* If we have a speculated target static frame, then it's now safe to
     * release the code temporary (no need to keep it). */
    if (have_code_temp)
        MVM_spesh_manipulate_release_temp_reg(tc, g, code_temp);
}

static void optimize_coverage_log(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    char *cache        = (char *)ins->operands[3].lit_i64;
    MVMint32 cache_idx = ins->operands[2].lit_i32;

    if (cache[cache_idx] != 0) {
        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
    }
}

/* Optimizes an extension op. */
static void optimize_extop(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMExtOpRecord *extops     = g->sf->body.cu->body.extops;
    MVMuint16       num_extops = g->sf->body.cu->body.num_extops;
    MVMuint16       i;
    for (i = 0; i < num_extops; i++) {
        if (extops[i].info == ins->info) {
            /* Found op; call its spesh function, if any. */
            if (extops[i].spesh)
                extops[i].spesh(tc, g, bb, ins);
            return;
        }
    }
}

static void optimize_uniprop_ops(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *arg1_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    if (arg1_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        if (ins->info->opcode == MVM_OP_unipropcode) {
            result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
            result_facts->value.i = (MVMint64)MVM_unicode_name_to_property_code(tc, arg1_facts->value.s);
            ins->info = MVM_op_get_op(MVM_OP_const_i64);
            ins->operands[1].lit_i64 = result_facts->value.i;
            arg1_facts->usages--;
        } else if (ins->info->opcode == MVM_OP_unipvalcode) {
            MVMSpeshFacts *arg2_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);

            if (arg2_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
                result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                result_facts->value.i = (MVMint64)MVM_unicode_name_to_property_value_code(tc, arg1_facts->value.i, arg2_facts->value.s);
                ins->info = MVM_op_get_op(MVM_OP_const_i64);
                ins->operands[1].lit_i64 = result_facts->value.i;
                arg1_facts->usages--;
                arg2_facts->usages--;
            }
        }
}
}

/* If something is only kept alive because we log its allocation, kick out
 * the allocation logging and let the op that creates it die. */
static void optimize_prof_allocated(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *logee_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    if (logee_facts->usages == 1) {
        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
        logee_facts->usages = 0;
        /* This check should always succeed, but just in case ... */
        if (logee_facts->writer)
            MVM_spesh_manipulate_delete_ins(tc, g, bb, logee_facts->writer);
    }
}

/* Tries to optimize a throwcat instruction. Note that within a given frame
 * (we don't consider inlines here) the throwcat instructions all have the
 * same semantics. */
static void optimize_throwcat(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    /* First, see if we have any goto handlers for this category. */
    MVMint32 *handlers_found = MVM_malloc(g->num_handlers * sizeof(MVMint32));
    MVMint32  num_found      = 0;
    MVMuint32 category       = (MVMuint32)ins->operands[1].lit_i64;
    MVMint32  i;
    for (i = 0; i < g->num_handlers; i++)
        if (g->handlers[i].action == MVM_EX_ACTION_GOTO)
            if (g->handlers[i].category_mask & category)
                handlers_found[num_found++] = i;

    /* If we found any appropriate handlers, we'll now do a scan through the
     * graph to see if we're in the scope of any of them. Note we can't keep
     * track of this in optimize_bb as it walks the dominance children, but
     * we need a linear view. */
    if (num_found) {
        MVMint32    *in_handlers = MVM_calloc(g->num_handlers, sizeof(MVMint32));
        MVMSpeshBB **goto_bbs    = MVM_calloc(g->num_handlers, sizeof(MVMSpeshBB *));
        MVMSpeshBB  *search_bb   = g->entry;
        MVMint32     picked      = -1;
        while (search_bb) {
            MVMSpeshIns *search_ins = search_bb->first_ins;
            while (search_ins) {
                /* Track handlers. */
                MVMSpeshAnn *ann = search_ins->annotations;
                while (ann) {
                    switch (ann->type) {
                    case MVM_SPESH_ANN_FH_START:
                        in_handlers[ann->data.frame_handler_index] = 1;
                        break;
                    case MVM_SPESH_ANN_FH_END:
                        in_handlers[ann->data.frame_handler_index] = 0;
                        break;
                    case MVM_SPESH_ANN_FH_GOTO:
                        if (ann->data.frame_handler_index < g->num_handlers) {
                            goto_bbs[ann->data.frame_handler_index] = search_bb;
                            if (picked >= 0 && ann->data.frame_handler_index == picked)
                                goto search_over;
                        }
                        break;
                    }
                    ann = ann->next;
                }

                /* Is this instruction the one we're trying to optimize? */
                if (search_ins == ins) {
                    /* See if we're in any acceptable handler (rely on the
                     * table being pre-sorted by nesting depth here, just like
                     * normal exception handler search does). */
                    for (i = 0; i < num_found; i++) {
                        if (in_handlers[handlers_found[i]]) {
                            /* Got it! If we already found its goto target, we
                             * can finish the search. */
                            picked = handlers_found[i];
                            if (goto_bbs[picked])
                                goto search_over;
                            break;
                        }
                    }
                }

                search_ins = search_ins->next;
            }
            search_bb = search_bb->linear_next;
        }
      search_over:

        /* If we picked a handler and know where it should goto, we can do the
         * rewrite into a goto. */
        if (picked >= 0 && goto_bbs[picked]) {
            ins->info               = MVM_op_get_op(MVM_OP_goto);
            ins->operands[0].ins_bb = goto_bbs[picked];
            bb->succ[0]             = goto_bbs[picked];
        }

        MVM_free(in_handlers);
        MVM_free(goto_bbs);
    }

    MVM_free(handlers_found);
}

/* Updates rebless with rebless_sp, which will deopt from the current code. */
static void tweak_rebless(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMuint32 deopt_target = find_deopt_target(tc, g, ins);
    MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
    new_operands[0] = ins->operands[0];
    new_operands[1] = ins->operands[1];
    new_operands[2] = ins->operands[2];
    new_operands[3].lit_ui32 = deopt_target;
    ins->info = MVM_op_get_op(MVM_OP_sp_rebless);
    ins->operands = new_operands;
}

/* Replaces atomic ops with a version that needs no checking of the target's
 * container spec and concreteness, when we have the facts to hand. */
static void optimize_container_atomic(MVMThreadContext *tc, MVMSpeshGraph *g,
                                      MVMSpeshIns *ins, MVMuint16 target_reg) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[target_reg]);
    if ((facts->flags & MVM_SPESH_FACT_CONCRETE) && (facts->flags & MVM_SPESH_FACT_KNOWN_TYPE)) {
        MVMContainerSpec const *cs = facts->type->st->container_spec;
        if (!cs)
            return;
        switch (ins->info->opcode) {
            case MVM_OP_cas_o:
                if (!cs->cas)
                    return;
                ins->info = MVM_op_get_op(MVM_OP_sp_cas_o);
                break;
            case MVM_OP_atomicstore_o:
                if (!cs->atomic_store)
                    return;
                ins->info = MVM_op_get_op(MVM_OP_sp_atomicstore_o);
                break;
            case MVM_OP_atomicload_o:
                if (!cs->cas)
                    return;
                ins->info = MVM_op_get_op(MVM_OP_sp_atomicload_o);
                break;
        }
        MVM_spesh_use_facts(tc, g, facts);
    }
}

static void eliminate_phi_dead_reads(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMuint32 operand = 1;
    MVMuint32 insert_pos = 1;
    MVMuint32 num_operands = ins->info->num_operands;
    while (operand < ins->info->num_operands) {
        if (get_facts_direct(tc, g, ins->operands[operand])->dead_writer) {
            num_operands--;
        }
        else {
            ins->operands[insert_pos] = ins->operands[operand];
            insert_pos++;
        }
        operand++;
    }
    if (num_operands != ins->info->num_operands)
        ins->info = get_phi(tc, g, num_operands);
}
static void analyze_phi(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMuint32 operand;
    MVMint32 common_flags;
    MVMObject *common_type;
    MVMObject *common_decont_type;
    MVMuint32 needs_merged_with_log_guard = 0;
    MVMSpeshFacts *target_facts = get_facts_direct(tc, g, ins->operands[0]);

    eliminate_phi_dead_reads(tc, g, ins);

    common_flags       = get_facts_direct(tc, g, ins->operands[1])->flags;
    common_type        = get_facts_direct(tc, g, ins->operands[1])->type;
    common_decont_type = get_facts_direct(tc, g, ins->operands[1])->decont_type;

    needs_merged_with_log_guard = common_flags & MVM_SPESH_FACT_FROM_LOG_GUARD;

    for(operand = 2; operand < ins->info->num_operands; operand++) {
        common_flags = common_flags & get_facts_direct(tc, g, ins->operands[operand])->flags;
        common_type = common_type == get_facts_direct(tc, g, ins->operands[operand])->type && common_type ? common_type : NULL;
        common_decont_type = common_decont_type == get_facts_direct(tc, g, ins->operands[operand])->decont_type && common_decont_type ? common_decont_type : NULL;

        /* We have to be a bit more careful if one or more of the facts we're
         * merging came from a log guard, as that means we'll have to propagate
         * the information what guards have been relied upon back "outwards"
         * through the PHI node we've merged stuff with. */
        if (get_facts_direct(tc, g, ins->operands[operand])->flags & MVM_SPESH_FACT_FROM_LOG_GUARD)
            needs_merged_with_log_guard = 1;
    }

    if (common_flags) {
        /*fprintf(stderr, "at a PHI node of %d operands: ", ins->info->num_operands);*/
        if (common_flags & MVM_SPESH_FACT_KNOWN_TYPE) {
            /*fprintf(stderr, "type ");*/
            if (common_type) {
                /*fprintf(stderr, "(same type) ");*/
                target_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
                target_facts->type = common_type;
            }
            /*else fprintf(stderr, "(diverging type) ");*/
        }
        /*if (common_flags & MVM_SPESH_FACT_KNOWN_VALUE) fprintf(stderr, "value ");*/
        if (common_flags & MVM_SPESH_FACT_DECONTED) {
            /*fprintf(stderr, "deconted ");*/
            target_facts->flags |= MVM_SPESH_FACT_DECONTED;
        }
        if (common_flags & MVM_SPESH_FACT_CONCRETE) {
            /*fprintf(stderr, "concrete ");*/
            target_facts->flags |= MVM_SPESH_FACT_CONCRETE;
        }
        if (common_flags & MVM_SPESH_FACT_TYPEOBJ) {
            /*fprintf(stderr, "type_object ");*/
        }
        if (common_flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE) {
            /*fprintf(stderr, "decont_type ");*/
            if (common_decont_type) {
                /*fprintf(stderr, "(same type) ");*/
                target_facts->flags |= MVM_SPESH_FACT_KNOWN_DECONT_TYPE;
                target_facts->decont_type = common_decont_type;
            }
            /*else fprintf(stderr, "(diverging type) ");*/
        }
        if (common_flags & MVM_SPESH_FACT_DECONT_CONCRETE) {
            /*fprintf(stderr, "decont_concrete ");*/
            target_facts->flags |= MVM_SPESH_FACT_DECONT_CONCRETE;
        }
        if (common_flags & MVM_SPESH_FACT_DECONT_TYPEOBJ) {
            /*fprintf(stderr, "decont_typeobj ");*/
            target_facts->flags |= MVM_SPESH_FACT_DECONT_TYPEOBJ;
        }
        if (common_flags & MVM_SPESH_FACT_RW_CONT) {
            /*fprintf(stderr, "rw_cont ");*/
            target_facts->flags |= MVM_SPESH_FACT_RW_CONT;
        }
        /*if (common_flags & MVM_SPESH_FACT_FROM_LOG_GUARD) fprintf(stderr, "from_log_guard ");*/
        /*if (common_flags & MVM_SPESH_FACT_HASH_ITER) fprintf(stderr, "hash_iter ");*/
        /*if (common_flags & MVM_SPESH_FACT_ARRAY_ITER) fprintf(stderr, "array_iter ");*/
        /*if (common_flags & MVM_SPESH_FACT_KNOWN_BOX_SRC) fprintf(stderr, "box_source ");*/
        /*fprintf(stderr, "\n");*/

        if (needs_merged_with_log_guard) {
            target_facts->flags |= MVM_SPESH_FACT_MERGED_WITH_LOG_GUARD;
        }
    } else {
        /*fprintf(stderr, "a PHI node of %d operands had no intersecting flags\n", ins->info->num_operands);*/
    }
}
static void optimize_bb_switch(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                        MVMSpeshPlanned *p) {
    MVMSpeshCallInfo arg_info;
    /* Look for instructions that are interesting to optimize. */
    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        switch (ins->info->opcode) {
        case MVM_SSA_PHI:
            analyze_phi(tc, g, ins);
            break;
        case MVM_OP_set:
            copy_facts(tc, g, ins->operands[0], ins->operands[1]);
            break;
        case MVM_OP_istrue:
        case MVM_OP_isfalse:
            optimize_istrue_isfalse(tc, g, bb, ins);
            break;
        case MVM_OP_if_i:
        case MVM_OP_unless_i:
        case MVM_OP_if_n:
        case MVM_OP_unless_n:
        case MVM_OP_if_o:
        case MVM_OP_unless_o:
            optimize_iffy(tc, g, ins, bb);
            break;
        case MVM_OP_not_i:
            optimize_not_i(tc, g, ins, bb);
            break;
        case MVM_OP_prepargs:
            arg_info.cs = g->sf->body.cu->body.callsites[ins->operands[0].callsite_idx];
            arg_info.prepargs_ins = ins;
            arg_info.prepargs_bb  = bb;
            break;
        case MVM_OP_arg_i:
        case MVM_OP_arg_n:
        case MVM_OP_arg_s:
        case MVM_OP_arg_o: {
            MVMint16 idx = ins->operands[0].lit_i16;
            if (idx < MAX_ARGS_FOR_OPT) {
                arg_info.arg_is_const[idx] = 0;
                arg_info.arg_facts[idx]    = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
                arg_info.arg_ins[idx]      = ins;
            }
            break;
        }
        case MVM_OP_argconst_i:
        case MVM_OP_argconst_n:
        case MVM_OP_argconst_s: {
            MVMint16 idx = ins->operands[0].lit_i16;
            if (idx < MAX_ARGS_FOR_OPT) {
                arg_info.arg_is_const[idx] = 1;
                arg_info.arg_ins[idx]      = ins;
            }
            break;
        }
        case MVM_OP_coerce_in:
            optimize_coerce(tc, g, bb, ins);
            break;
        case MVM_OP_smrt_numify:
        case MVM_OP_smrt_strify:
            optimize_smart_coerce(tc, g, bb, ins);
            break;
        case MVM_OP_invoke_v:
            optimize_call(tc, g, bb, ins, p, 0, &arg_info);
            break;
        case MVM_OP_invoke_i:
        case MVM_OP_invoke_n:
        case MVM_OP_invoke_s:
        case MVM_OP_invoke_o:
            optimize_call(tc, g, bb, ins, p, 1, &arg_info);
            break;
        case MVM_OP_islist:
        case MVM_OP_ishash:
        case MVM_OP_isint:
        case MVM_OP_isnum:
        case MVM_OP_isstr:
            optimize_is_reprid(tc, g, ins);
            break;
        case MVM_OP_findmeth_s:
            optimize_findmeth_s_perhaps_constant(tc, g, ins);
            if (ins->info->opcode == MVM_OP_findmeth_s)
                break;
        case MVM_OP_findmeth:
            optimize_method_lookup(tc, g, ins);
            break;
        case MVM_OP_can:
        case MVM_OP_can_s:
            optimize_can_op(tc, g, bb, ins);
            break;
        case MVM_OP_gethow:
            optimize_gethow(tc, g, ins);
            break;
        case MVM_OP_isconcrete:
            optimize_isconcrete(tc, g, ins);
            break;
        case MVM_OP_istype:
            optimize_istype(tc, g, ins);
            break;
        case MVM_OP_objprimspec:
            optimize_objprimspec(tc, g, ins);
            break;
        case MVM_OP_unipropcode:
        case MVM_OP_unipvalcode:
            optimize_uniprop_ops(tc, g, bb, ins);
            break;
        case MVM_OP_unshift_i:
        case MVM_OP_unshift_n:
        case MVM_OP_unshift_s:
        case MVM_OP_unshift_o:
        case MVM_OP_bindkey_i:
        case MVM_OP_bindkey_n:
        case MVM_OP_bindkey_s:
        case MVM_OP_bindkey_o:
        case MVM_OP_bindpos_i:
        case MVM_OP_bindpos_n:
        case MVM_OP_bindpos_s:
        case MVM_OP_bindpos_o:
        case MVM_OP_pop_i:
        case MVM_OP_pop_n:
        case MVM_OP_pop_s:
        case MVM_OP_pop_o:
        case MVM_OP_deletekey:
        case MVM_OP_setelemspos:
        case MVM_OP_splice:
        case MVM_OP_bindattr_i:
        case MVM_OP_bindattr_n:
        case MVM_OP_bindattr_s:
        case MVM_OP_bindattr_o:
        case MVM_OP_bindattrs_i:
        case MVM_OP_bindattrs_n:
        case MVM_OP_bindattrs_s:
        case MVM_OP_bindattrs_o:
        case MVM_OP_assign_i:
        case MVM_OP_assign_n:
            optimize_repr_op(tc, g, bb, ins, 0);
            break;
        case MVM_OP_atpos_i:
        case MVM_OP_atpos_n:
        case MVM_OP_atpos_s:
        case MVM_OP_atpos_o:
        case MVM_OP_atkey_i:
        case MVM_OP_atkey_n:
        case MVM_OP_atkey_s:
        case MVM_OP_atkey_o:
        case MVM_OP_elems:
        case MVM_OP_shift_i:
        case MVM_OP_shift_n:
        case MVM_OP_shift_s:
        case MVM_OP_shift_o:
        case MVM_OP_push_i:
        case MVM_OP_push_n:
        case MVM_OP_push_s:
        case MVM_OP_push_o:
        case MVM_OP_existskey:
        case MVM_OP_existspos:
        case MVM_OP_getattr_i:
        case MVM_OP_getattr_n:
        case MVM_OP_getattr_s:
        case MVM_OP_getattr_o:
        case MVM_OP_getattrs_i:
        case MVM_OP_getattrs_n:
        case MVM_OP_getattrs_s:
        case MVM_OP_getattrs_o:
        case MVM_OP_decont_i:
        case MVM_OP_decont_n:
        case MVM_OP_decont_s:
        case MVM_OP_decont_u:
        case MVM_OP_create:
            optimize_repr_op(tc, g, bb, ins, 1);
            break;
        case MVM_OP_box_i:
        case MVM_OP_box_n:
        case MVM_OP_box_s:
            optimize_repr_op(tc, g, bb, ins, 2);
            break;
        case MVM_OP_newexception:
        case MVM_OP_bindexmessage:
        case MVM_OP_bindexpayload:
        case MVM_OP_getexmessage:
        case MVM_OP_getexpayload:
            optimize_exception_ops(tc, g, bb, ins);
            break;
        case MVM_OP_hllize:
            optimize_hllize(tc, g, ins);
            break;
        case MVM_OP_decont:
            optimize_decont(tc, g, bb, ins);
            break;
        case MVM_OP_assertparamcheck:
            optimize_assertparamcheck(tc, g, bb, ins);
            break;
        case MVM_OP_getlex:
            optimize_getlex(tc, g, ins);
            break;
        case MVM_OP_getlex_no:
            /* Use non-logging variant. */
            ins->info = MVM_op_get_op(MVM_OP_sp_getlex_no);
            break;
        case MVM_OP_getlexstatic_o:
            optimize_getlex_known(tc, g, bb, ins);
            break;
        case MVM_OP_getlexperinvtype_o:
            optimize_getlex_per_invocant(tc, g, bb, ins, p);
            break;
        case MVM_OP_iscont:
        case MVM_OP_isrwcont:
        case MVM_OP_iscont_i:
        case MVM_OP_iscont_n:
        case MVM_OP_iscont_s:
            optimize_container_check(tc, g, bb, ins);
            break;
        case MVM_OP_osrpoint:
            /* We don't need to poll for OSR in hot loops. (This also moves
             * the OSR annotation onto the next instruction.) */
            MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
            break;
        case MVM_OP_rebless:
            tweak_rebless(tc, g, ins);
            break;
        case MVM_OP_cas_o:
        case MVM_OP_atomicload_o:
            optimize_container_atomic(tc, g, ins, 1);
            break;
        case MVM_OP_atomicstore_o:
            optimize_container_atomic(tc, g, ins, 0);
            break;
        case MVM_OP_prof_enter:
            /* Profiling entered from spesh should indicate so. */
            ins->info = MVM_op_get_op(MVM_OP_prof_enterspesh);
            break;
        case MVM_OP_coverage_log:
            /* A coverage_log op that has already fired can be thrown out. */
            optimize_coverage_log(tc, g, bb, ins);
            break;
        default:
            if (ins->info->opcode == (MVMuint16)-1)
                optimize_extop(tc, g, bb, ins);
        }

        ins = ins->next;
    }
}
/* Visits the blocks in dominator tree order, recursively. */
static void optimize_bb(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                        MVMSpeshPlanned *p) {
    MVMint64 i = 0;
    /* Because optimize_bb() can be deeply recursive, separate as much code
     * as possible into a separate function optimize_bb_switch(), so we don't
     * trash the stack. (needed on musl) */
    optimize_bb_switch(tc, g, bb, p);
    /* Optimize the case where we only have one child. This avoids having
     * to do a recursive call to optimize_bb() */
    while (bb->num_children == 1) {
        bb = bb->children[0];
        /* Keep following the nodes and running optimize_bb_switch() on them
         * until we hit one with more than 1 child. */
        optimize_bb_switch(tc, g, bb, p);
    }
    /* Visit children. */
    for (; i < bb->num_children; i++)
        optimize_bb(tc, g, bb->children[i], p);
}

/* Eliminates any unused instructions. */
static void eliminate_dead_ins(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Keep eliminating to a fixed point. */
    MVMint8 death = 1;
    while (death) {
        MVMSpeshBB *bb = g->entry;
        death = 0;
        while (bb) {
            if (!bb->inlined) {
                MVMSpeshIns *ins = bb->last_ins;
                while (ins) {
                    MVMSpeshIns *prev = ins->prev;
                    if (ins->info->opcode == MVM_SSA_PHI) {
                        MVMSpeshFacts *facts = get_facts_direct(tc, g, ins->operands[0]);
                        if (facts->usages == 0) {
                            /* Remove this phi. */
                            MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
                            death = 1;
                        }
                    }
                    else if (ins->info->pure) {
                        /* Sanity check to make sure it's a write reg as first operand. */
                        if ((ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
                            MVMSpeshFacts *facts = get_facts_direct(tc, g, ins->operands[0]);
                            if (facts->usages == 0) {
                                /* Remove this instruction. */
                                MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
                                death = 1;
                            }
                        }
                    }
                    ins = prev;
                }
            }
            bb = bb->linear_next;
        }
    }
}

/* Optimization turns many things into simple set instructions, which we can
 * often further eliminate; others may become unrequired due to eliminated
 * branches, and some may be from sub-optimizal original code. */
static MVMint32 within_inline(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                              MVMSpeshOperand target) {
    if (bb->inlined) {
        MVMSpeshBB *check_bb = bb;
        while (check_bb) {
            MVMSpeshIns *last_ins = check_bb->last_ins;
            MVMSpeshAnn *ann = last_ins->annotations;
            MVMint32 max_inline = -1;
            while (ann) {
                if (ann->type == MVM_SPESH_ANN_INLINE_END)
                    if (ann->data.inline_idx > max_inline)
                        max_inline = ann->data.inline_idx;
                ann = ann->next;
            }
            if (max_inline >= 0) {
                /* We've found the inline that we're inside of. Check if the
                 * register is within its range of registers. */
                MVMuint16 locals_start = g->inlines[max_inline].locals_start;
                MVMuint16 num_locals = g->inlines[max_inline].num_locals;
                return target.reg.orig >= locals_start &&
                    target.reg.orig < locals_start + num_locals;
            }
            check_bb = check_bb->linear_next;
        }
    }
    return 1; /* We're not in an inline at all, so automatically yes. */
}
static void try_eliminate_set(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                              MVMSpeshIns *ins) {
    /* Sometimes, a set takes place between two versions of the same register.
     * This can go.
     * XXX Should rewrite the graph properly. */
    if (ins->operands[0].reg.orig == ins->operands[1].reg.orig) {
        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
    }

    /* Other optimizations depend on having a previous op. */
    else if (!ins->prev) {
        return;
    }

    /* If we have:
     *      set rT(j), rO(i)
     *      set rO(i + 1), rT(j)
     *  Then the second instruction can go away.
     *  XXX Should rewrite the graph properly. */
    else if (ins->prev->info->opcode == MVM_OP_set) {
        if (ins->operands[0].reg.orig == ins->prev->operands[1].reg.orig &&
                ins->operands[0].reg.i == ins->prev->operands[1].reg.i + 1 &&
                ins->operands[1].reg.orig == ins->prev->operands[0].reg.orig &&
                ins->operands[1].reg.i == ins->prev->operands[0].reg.i)
            MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
    }

    /* If a write operation is immediately followed by a set, we can look at
     * the usages of the intermediate register and make sure it's only ever
     * read by the set, and not, for example, required by a deopt barrier to
     * have a copy of the value. In that case, we don't need the temporary
     * and can assign the result of the instruction directly into the
     * target register. We must also check, if we're in an inline, that the
     * final target register is within the inline, since deopt depends on the
     * target register of an invoke being within a frame. */
    else if ((ins->prev->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg &&
            ins->prev->info->opcode != MVM_SSA_PHI &&
            ins->prev->operands[0].reg.orig == ins->operands[1].reg.orig &&
            ins->prev->operands[0].reg.i == ins->operands[1].reg.i) {
        MVMSpeshFacts *elim_facts = get_facts_direct(tc, g, ins->operands[1]);
        if (elim_facts->usages == 1 && within_inline(tc, g, bb, ins->operands[0])) {
            ins->prev->operands[0].reg = ins->operands[0].reg;
            get_facts_direct(tc, g, ins->prev->operands[0])->writer = ins->prev;
            MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
        }
    }
}

/* Drives the second, post-inline, optimization pass. */
static void second_pass(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb) {
    MVMint32 i;

    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        MVMSpeshIns *next = ins->next;
        switch (ins->info->opcode) {
            case MVM_OP_set:
                try_eliminate_set(tc, g, bb, ins);
                break;
            case MVM_OP_sp_getspeshslot:
                /* Sometimes we emit two getspeshslots in a row that write into the
                 * exact same register. That's clearly wasteful and we can save a
                 * tiny shred of code size here. */
                if (ins->prev && ins->prev->info->opcode == ins->info->opcode &&
                        ins->operands[0].reg.orig == ins->prev->operands[0].reg.orig)
                    MVM_spesh_manipulate_delete_ins(tc, g, bb, ins->prev);
                break;
            case MVM_OP_prof_allocated:
                optimize_prof_allocated(tc, g, bb, ins);
                break;
            case MVM_OP_throwcatdyn:
            case MVM_OP_throwcatlex:
            case MVM_OP_throwcatlexotic:
                optimize_throwcat(tc, g, bb, ins);
                break;
        }
        ins = next;
    }

    /* Visit children. */
    for (i = 0; i < bb->num_children; i++)
        second_pass(tc, g, bb->children[i]);
}

/* Goes through the various log-based guard instructions and removes any that
 * are not being used. */
static void eliminate_unused_log_guards(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMint32 i;
    for (i = 0; i < g->num_log_guards; i++)
        if (!g->log_guards[i].used)
            MVM_spesh_manipulate_delete_ins(tc, g, g->log_guards[i].bb,
                g->log_guards[i].ins);
}

/* Sometimes - almost always due to other optmimizations having done their
 * work - we end up with an unconditional goto at the end of a basic block
 * that points right to the very next basic block. Delete these. */
static void eliminate_pointless_gotos(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *cur_bb = g->entry;
    while (cur_bb) {
        if (!cur_bb->jumplist) {
            MVMSpeshIns *last_ins = cur_bb->last_ins;
            if (
                last_ins
                && last_ins->info->opcode == MVM_OP_goto
                && last_ins->operands[0].ins_bb == cur_bb->linear_next
            ) {
                MVM_spesh_manipulate_delete_ins(tc, g, cur_bb, last_ins);
            }
        }
        cur_bb = cur_bb->linear_next;
    }
}

static void merge_bbs(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *bb = g->entry;
    MVMint32 orig_bbs = g->num_bbs;
    if (!bb || !bb->linear_next) return; /* looks like there's only a single bb anyway */
    bb = bb->linear_next;

    while (bb->linear_next) {
        if (bb->num_succ == 1 && bb->succ[0] == bb->linear_next && bb->linear_next->num_pred == 1 && !bb->inlined && !bb->linear_next->inlined) {
            if (bb->linear_next->first_ins) {
                bb->linear_next->first_ins->prev = bb->last_ins;
                if (bb->last_ins) {
                    bb->last_ins->next = bb->linear_next->first_ins;
                    bb->last_ins->next->prev = bb->last_ins;
                    bb->last_ins = bb->linear_next->last_ins;
                }
                else {
                    bb->first_ins = bb->linear_next->first_ins;
                    bb->last_ins = bb->linear_next->last_ins;
                }
                bb->linear_next->first_ins = bb->linear_next->last_ins = NULL;
            }
            if (bb->linear_next->num_succ) {
                MVMSpeshBB **succ = MVM_spesh_alloc(tc, g, (bb->num_succ - 1 + bb->linear_next->num_succ) * sizeof(MVMSpeshBB *));
                int i, j = 0;
                for (i = 0; i < bb->num_succ; i++)
                    if (bb->succ[i] != bb->linear_next)
                        succ[j++] = bb->succ[i];
                for (i = 0; i < bb->linear_next->num_succ; i++)
                    succ[j++] = bb->linear_next->succ[i];
                bb->succ = succ;
            }
            bb->num_succ--;
            bb->num_succ += bb->linear_next->num_succ;

            bb->linear_next = bb->linear_next->linear_next;
            g->num_bbs--;
        }
        else {
            bb = bb->linear_next;
        }
    }

    /* Re-number BBs so we get sequential ordering again. */
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
void MVM_spesh_optimize(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshPlanned *p) {
    /* Before starting, we eliminate dead basic blocks that were tossed by
     * arg spesh, to simplify the graph. */
    MVM_spesh_eliminate_dead_bbs(tc, g, 1);

    /* Perform initial optimization pass, which performs a range of opts
     * including, most notably, inlining. */
    optimize_bb(tc, g, g->entry, p);

    /* Clear up the graph after this initial pass. */
    MVM_spesh_eliminate_dead_bbs(tc, g, 1);
    eliminate_unused_log_guards(tc, g);
    eliminate_pointless_gotos(tc, g);
    eliminate_dead_ins(tc, g);

    merge_bbs(tc, g);

    /* Make a second pass through the graph doing things that are better
     * done after inlinings have taken place. The dominance tree is first
     * recomputed, to account for any inlinings. */
    MVM_spesh_graph_recompute_dominance(tc, g);
    second_pass(tc, g, g->entry);
}
