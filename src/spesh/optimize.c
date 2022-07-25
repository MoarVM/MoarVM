#include "moar.h"

static void optimize_bigint_bool_op(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins);
static void optimize_bb(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                        MVMSpeshPlanned *p);

/* This is where the main optimization work on a spesh graph takes place,
 * using facts discovered during analysis. */

/* Logging of whether we can or can't inline. */
static void log_inline(MVMThreadContext *tc, MVMSpeshGraph *g, MVMStaticFrame *target_sf,
                       MVMSpeshGraph *inline_graph, MVMuint32 bytecode_size,
                       char *no_inline_reason, MVMint32 unspecialized, const MVMOpInfo *no_inline_info) {
    if (tc->instance->spesh_inline_log) {
        char *c_name_i = MVM_string_utf8_encode_C_string(tc, target_sf->body.name);
        char *c_cuid_i = MVM_string_utf8_encode_C_string(tc, target_sf->body.cuuid);
        char *c_name_t = MVM_string_utf8_encode_C_string(tc, g->sf->body.name);
        char *c_cuid_t = MVM_string_utf8_encode_C_string(tc, g->sf->body.cuuid);
        if (inline_graph) {
            fprintf(stderr, "Can inline %s%s (%s) with bytecode size %u into %s (%s)\n",
                unspecialized ? "unspecialized " : "",
                c_name_i, c_cuid_i,
                bytecode_size, c_name_t, c_cuid_t);
        }
        else {
            fprintf(stderr, "Can NOT inline %s (%s) with bytecode size %u into %s (%s): %s",
                c_name_i, c_cuid_i, bytecode_size, c_name_t, c_cuid_t, no_inline_reason);
            if (no_inline_info) {
                fprintf(stderr, " - ins: %s", no_inline_info->name);
            }
            fprintf(stderr, "\n");
        }
        MVM_free(c_name_i);
        MVM_free(c_cuid_i);
        MVM_free(c_name_t);
        MVM_free(c_cuid_t);
    }
    if (inline_graph && MVM_spesh_debug_enabled(tc)) {
        char *dump = MVM_spesh_dump(tc, inline_graph);
        MVM_spesh_debug_printf(tc, "Inlining graph\n%s\n", dump);
        MVM_free(dump);
    }
}

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
    MVMuint32 i;
    for (i = 0; i < facts->num_log_guards; i++)
        g->log_guards[facts->log_guards[i]].used = 1;
}

/* Obtains a string constant. */
MVMString * MVM_spesh_get_string(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o) {
    return MVM_cu_string(tc, g->sf->body.cu, o.lit_str_idx);
}

/* Copy facts between two register operands. */
static void copy_facts_resolved(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *tfacts,
                                MVMSpeshFacts *ffacts) {
    tfacts->flags         = ffacts->flags;
    tfacts->type          = ffacts->type;
    tfacts->decont_type   = ffacts->decont_type;
    tfacts->value         = ffacts->value;
    tfacts->log_guards    = ffacts->log_guards;
    tfacts->num_log_guards = ffacts->num_log_guards;
}
static void copy_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand to,
                       MVMSpeshOperand from) {
    MVMSpeshFacts *tfacts = get_facts_direct(tc, g, to);
    MVMSpeshFacts *ffacts = get_facts_direct(tc, g, from);
    copy_facts_resolved(tc, g, tfacts, ffacts);
}
void MVM_spesh_copy_facts_resolved(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *to,
                                   MVMSpeshFacts *from) {
    copy_facts_resolved(tc, g, to, from);
}
void MVM_spesh_copy_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand to,
                          MVMSpeshOperand from) {
    copy_facts(tc, g, to, from);
}
static int is_syscall(MVMThreadContext *tc, MVMSpeshFacts *facts,
                      void (*syscall) (MVMThreadContext *tc, MVMArgs arg_info)) {
    return facts->flags & MVM_SPESH_FACT_KNOWN_VALUE
        && REPR(facts->value.o)->ID == MVM_REPR_ID_MVMCFunction
        && ((MVMCFunction*)facts->value.o)->body.func == syscall;
}

static void MVM_spesh_turn_into_set(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    ins->info = MVM_op_get_op(MVM_OP_set);
    copy_facts(tc, g, ins->operands[0], ins->operands[1]);
}

/* Adds a value into a spesh slot and returns its index.
 * If a spesh slot already holds this value, return that instead. */
MVMint16 MVM_spesh_add_spesh_slot_try_reuse(MVMThreadContext *tc, MVMSpeshGraph *g, MVMCollectable *c) {
    MVMint16 prev_slot;
    for (prev_slot = 0; (MVMuint16)prev_slot < g->num_spesh_slots; prev_slot++) {
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

/* Some things optimize into the `null` op; make sure it has facts set. */
static void optimize_null(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                          MVMSpeshIns *ins) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_KNOWN_VALUE;
    facts->value.o = facts->type = tc->instance->VMNull;
}

/* If an `isnull` is on something we know the type of or value of, then we
 * can quickly verify the type is not based on the null REPR and turn it
 * into a constant. */
static void optimize_isnull(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                            MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        MVMint32 is_null = REPR(obj_facts->type)->ID == MVM_REPR_ID_MVMNull;
        MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        MVM_spesh_use_facts(tc, g, obj_facts);
        MVM_spesh_usages_delete(tc, g, obj_facts, ins);
        ins->info = MVM_op_get_op(MVM_OP_const_i64_16);
        ins->operands[1].lit_i16 = is_null;
        result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        result_facts->value.i = is_null;
        MVM_spesh_facts_depend(tc, g, result_facts, obj_facts);
    }
}

static void optimize_repr_op(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                             MVMSpeshIns *ins, MVMint32 type_operand);

/* Try to turn an istype into a constant; failing that, rewrite it into the
 * sp_istype op. */
static void optimize_istype(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    /* See if we can constant fold it. */
    MVMSpeshFacts *obj_facts  = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMSpeshFacts *type_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);
    MVMSpeshFacts *result_facts;
    if (type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE &&
         obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        MVMint64 result;
        if (MVM_6model_try_cache_type_check(tc, obj_facts->type, type_facts->type, &result)) {
            /* Yes; turn it into a constant. */
            ins->info = MVM_op_get_op(MVM_OP_const_i64_16);
            result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
            result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
            ins->operands[1].lit_i16 = result;
            result_facts->value.i  = result;
            MVM_spesh_usages_delete(tc, g, obj_facts, ins);
            MVM_spesh_usages_delete(tc, g, type_facts, ins);
            MVM_spesh_facts_depend(tc, g, result_facts, obj_facts);
            MVM_spesh_use_facts(tc, g, obj_facts);
            MVM_spesh_facts_depend(tc, g, result_facts, type_facts);
            MVM_spesh_use_facts(tc, g, type_facts);
            return;
        }
    }

    /* If we get here, turn it into sp_istype, so we retain the correct inline
     * cache position, even over inlining. */
    MVMSpeshAnn *ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_CACHED)
            break;
        ann = ann->next;
    }
    if (!ann)
        MVM_oops(tc, "Missing cache annotation on istype");
    ins->info = MVM_op_get_op(MVM_OP_sp_istype);
    MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g, 5 * sizeof(MVMSpeshOperand));
    new_operands[0] = ins->operands[0];
    new_operands[1] = ins->operands[1];
    new_operands[2] = ins->operands[2];
    new_operands[3].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
        (MVMCollectable *)g->sf);
    new_operands[4].lit_ui32 = MVM_disp_inline_cache_get_slot(tc, g->sf,
        ann->data.bytecode_offset);
    ins->operands = new_operands;
}

/* Sees if we can resolve an eqaddr at compile time. If we know both of the
 * values, we can resolve it to a 1. If we know the types differ, we can
 * resolve it to a 0. */
static void optimize_eqaddr(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_a_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMSpeshFacts *obj_b_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);
    MVMint16 known_result = -1;
    if (obj_a_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE &&
            obj_b_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE &&
            obj_a_facts->value.o == obj_b_facts->value.o)
        known_result = 1;
    else if (obj_a_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE &&
            obj_b_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE &&
            obj_a_facts->type != obj_b_facts->type)
        known_result = 0;
    if (known_result >= 0) {
        MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        ins->info = MVM_op_get_op(MVM_OP_const_i64_16);
        ins->operands[1].lit_i16 = known_result;
        result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        result_facts->value.i = known_result;

        MVM_spesh_usages_delete(tc, g, obj_a_facts, ins);
        MVM_spesh_usages_delete(tc, g, obj_b_facts, ins);
        MVM_spesh_facts_depend(tc, g, result_facts, obj_a_facts);
        MVM_spesh_use_facts(tc, g, obj_a_facts);
        MVM_spesh_facts_depend(tc, g, result_facts, obj_b_facts);
        MVM_spesh_use_facts(tc, g, obj_b_facts);
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
        MVM_spesh_usages_delete(tc, g, obj_facts, ins);
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
        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
        ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
        ins->operands[1].lit_i16 = spesh_slot;
        /* Store facts about the value in the write operand */
        how_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        how_facts->flags  |= (MVM_SPESH_FACT_KNOWN_VALUE | MVM_SPESH_FACT_KNOWN_TYPE);
        how_facts->value.o = how_obj;
        how_facts->type    = STABLE(how_obj)->WHAT;
        MVM_spesh_use_facts(tc, g, obj_facts);
        MVM_spesh_facts_depend(tc, g, how_facts, obj_facts);
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
        MVM_spesh_usages_delete(tc, g, obj_facts, ins);
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
    MVMint8 truthvalue = -1;

    switch (ins->info->opcode) {
        case MVM_OP_if_i:
        case MVM_OP_if_s:
        case MVM_OP_if_n:
        case MVM_OP_ifnonnull:
            negated_op = 0;
            break;
        case MVM_OP_unless_i:
        case MVM_OP_unless_s:
        case MVM_OP_unless_n:
            negated_op = 1;
            break;
        default:
            return;
    }

    if (flag_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        switch (ins->info->opcode) {
            case MVM_OP_if_i:
            case MVM_OP_unless_i:
                truthvalue = flag_facts->value.i ? 1 : 0;
                break;
            case MVM_OP_if_n:
            case MVM_OP_unless_n:
                truthvalue = flag_facts->value.n != 0.0 ? 1 : 0;
                break;
            case MVM_OP_ifnonnull:
                truthvalue = REPR(flag_facts->value.o)->ID != MVM_REPR_ID_MVMNull;
                break;
            default:
                return;
        }
    }
    else if (flag_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        if (ins->info->opcode == MVM_OP_ifnonnull)
            truthvalue = REPR(flag_facts->type)->ID != MVM_REPR_ID_MVMNull;
    }

    if (truthvalue >= 0) {
        MVM_spesh_use_facts(tc, g, flag_facts);

        truthvalue = truthvalue ? 1 : 0;
        if (truthvalue != negated_op) {
            /* This conditional can be turned into an unconditional jump. */
            ins->info = MVM_op_get_op(MVM_OP_goto);
            ins->operands[0] = ins->operands[1];
            MVM_spesh_usages_delete(tc, g, flag_facts, ins);

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
        MVM_spesh_usages_delete(tc, g, src_facts, ins);

        /* Need to depend on the source facts. */
        MVM_spesh_use_facts(tc, g, src_facts);
        MVM_spesh_facts_depend(tc, g, dest_facts, src_facts);
    }
}

static void optimize_bitwise_int_math(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins, MVMSpeshBB *bb) {
    MVMSpeshFacts *lhs_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMSpeshFacts *rhs_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);
    if ((lhs_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) && (rhs_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE)) {
        MVMint64 lhs_v = lhs_facts->value.i;
        MVMint64 rhs_v = rhs_facts->value.i;
        MVMint64 result_v;
        const char *opname = ins->info->name;

        switch (ins->info->opcode) {
            case MVM_OP_band_i:
                result_v = lhs_v & rhs_v;
                break;
            case MVM_OP_bor_i:
                result_v = lhs_v | rhs_v;
                break;
            case MVM_OP_bxor_i:
                result_v = lhs_v ^ rhs_v;
                break;
            default:
                MVM_spesh_graph_add_comment(tc, g, ins, "not the right opcode for some reason lol %s %d", opname, ins->info->opcode);
                return;
        }

        /* Turn the op into a constant and set result facts. */
        MVMSpeshFacts *dest_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        dest_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        dest_facts->value.i = result_v;
        ins->info = MVM_op_get_op(MVM_OP_const_i64);
        ins->operands[1].lit_i64 = result_v;

        /* This op no longer uses the source values. */
        MVM_spesh_usages_delete(tc, g, lhs_facts, ins);
        MVM_spesh_usages_delete(tc, g, rhs_facts, ins);

        /* Need to depend on the source facts. */
        MVM_spesh_use_facts(tc, g, lhs_facts);
        MVM_spesh_use_facts(tc, g, rhs_facts);
        MVM_spesh_facts_depend(tc, g, dest_facts, lhs_facts);
        MVM_spesh_facts_depend(tc, g, dest_facts, rhs_facts);

        MVM_spesh_graph_add_comment(tc, g, ins, "optimized math from an %s op.", opname);
    }
    else {
        MVM_spesh_graph_add_comment(tc, g, ins, "looked at this but no luck. flags: %d and %d", lhs_facts->flags, rhs_facts->flags);
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
        MVM_spesh_usages_delete(tc, g, obj_facts, ins);
    }
}

/* Turns a decont into a set, if we know it's not needed. Also make sure we
 * propagate any needed information. */
static void optimize_decont(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if ((obj_facts->flags & MVM_SPESH_FACT_TYPEOBJ) ||
            ((obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
            !obj_facts->type->st->container_spec)) {
        /* Know that we don't need to decont. */
        MVM_spesh_turn_into_set(tc, g, ins);
        MVM_spesh_use_facts(tc, g, obj_facts);
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

        /* If it's a known type and known concrete... */
        if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && obj_facts->type &&
                (obj_facts->flags & MVM_SPESH_FACT_CONCRETE)) {
            /* Can try to specialize the fetch. */
            MVMSTable *stable = STABLE(obj_facts->type);
            MVMContainerSpec const *contspec = stable->container_spec;
            if (contspec && contspec->fetch_never_invokes && contspec->spesh) {
                MVMSpeshAnn *ann = ins->annotations;
                /* Remove deopt annotation since we know we won't invoke. */
                if (ann && ann->type == MVM_SPESH_ANN_DEOPT_ONE_INS) {
                    ins->annotations = ann->next;
                }
                else {
                    while (ann) {
                        if (ann->next && ann->next->type == MVM_SPESH_ANN_DEOPT_ONE_INS) {
                            ann->next = ann->next->next;
                            break;
                        }
                        ann = ann->next;
                    }
                }
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
                MVMuint16 primitive_type = repr_data->primitive_type;
                MVMuint16 register_type = 0;
                MVMuint32 box_op;
                MVMuint32 unbox_op;

                if (!hll)
                    hll = g->sf->body.cu->body.hll_config;
                switch (primitive_type) {
                    case MVM_STORAGE_SPEC_BP_INT:
                        out_type = hll->int_box_type;
                        register_type = MVM_reg_int64;
                        box_op = MVM_OP_box_i;
                        unbox_op = MVM_OP_decont_i;
                        break;
                    case MVM_STORAGE_SPEC_BP_UINT64:
                        out_type = hll->int_box_type; /* UInt is just a subset, so box into Int */
                        register_type = MVM_reg_uint64;
                        box_op = MVM_OP_box_u;
                        unbox_op = MVM_OP_decont_u;
                        break;
                    case MVM_STORAGE_SPEC_BP_NUM:
                        out_type = hll->num_box_type;
                        register_type = MVM_reg_num64;
                        box_op = MVM_OP_box_n;
                        unbox_op = MVM_OP_decont_n;
                        break;
                    case MVM_STORAGE_SPEC_BP_STR:
                        out_type = hll->str_box_type;
                        register_type = MVM_reg_str;
                        box_op = MVM_OP_box_s;
                        unbox_op = MVM_OP_decont_s;
                        break;
                }

                if (out_type) {
                    MVMSpeshIns *box_ins = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));
                    MVMSpeshIns *ss_ins  = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));

                    MVMSpeshOperand val_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, register_type);
                    MVMSpeshOperand ss_temp  = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
                    MVMSpeshOperand orig_dst = ins->operands[0];
                    MVMSpeshOperand sslot;

                    MVMSpeshFacts *sslot_facts;

                    ins->info = MVM_op_get_op(unbox_op);
                    ins->operands[0] = val_temp;

                    sslot.lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g, (MVMCollectable *)out_type);

                    ss_ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
                    ss_ins->operands = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ) * 2);
                    ss_ins->operands[0] = ss_temp;
                    ss_ins->operands[1] = sslot;

                    box_ins->info = MVM_op_get_op(box_op);
                    box_ins->operands = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ) * 3);
                    box_ins->operands[0] = orig_dst;
                    box_ins->operands[1] = val_temp;
                    box_ins->operands[2] = ss_temp;

                    MVM_spesh_manipulate_insert_ins(tc, bb, ins, box_ins);
                    MVM_spesh_manipulate_insert_ins(tc, bb, ins, ss_ins);

                    get_facts_direct(tc, g, val_temp)->writer = ins;
                    get_facts_direct(tc, g, orig_dst)->writer = box_ins;

                    sslot_facts = get_facts_direct(tc, g, ss_temp);
                    sslot_facts->writer = ss_ins;
                    sslot_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE | MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_TYPEOBJ;
                    sslot_facts->type = out_type;
                    sslot_facts->value.o = out_type;

                    MVM_spesh_usages_add_by_reg(tc, g, ss_temp, box_ins);
                    MVM_spesh_usages_add_by_reg(tc, g, val_temp, box_ins);

                    MVM_spesh_graph_add_comment(tc, g, ins, "decont of %s -> %s + %s", stable->debug_name, ins->info->name, box_ins->info->name);

                    res_facts->type = out_type;
                    res_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE;
                    set_facts = 1;

                    MVM_spesh_manipulate_release_temp_reg(tc, g, ss_temp);
                    MVM_spesh_manipulate_release_temp_reg(tc, g, val_temp);

                    MVM_spesh_use_facts(tc, g, obj_facts);
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
        MVM_spesh_usages_delete(tc, g, facts, ins);
    }
}

/* Optimize away assertparamcheck if we know it will pass, otherwise tweak
 * it to work with inline caching after optimization. */
static void optimize_assertparamcheck(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE && facts->value.i) {
        MVM_spesh_use_facts(tc, g, facts);
        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
    }
    else {
        MVMSpeshAnn *ann = ins->annotations;
        while (ann) {
            if (ann->type == MVM_SPESH_ANN_CACHED)
                break;
            ann = ann->next;
        }
        if (ann) {
            ins->info = MVM_op_get_op(MVM_OP_sp_assertparamcheck);
            MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            new_operands[0] = ins->operands[0];
            new_operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                (MVMCollectable *)g->sf);
            new_operands[2].lit_ui32 = MVM_disp_inline_cache_get_slot(tc, g->sf,
                ann->data.bytecode_offset);
            ins->operands = new_operands;
        }
    }
}

/* Tweak bindcomplete so that it works with inline caching after optimization. */
static void optimize_bindcomplete(MVMThreadContext *tc, MVMSpeshGraph *g,
    MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshAnn *ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_CACHED)
            break;
        ann = ann->next;
    }
    if (ann) {
        ins->info = MVM_op_get_op(MVM_OP_sp_bindcomplete);
        MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
        new_operands[0].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
            (MVMCollectable *)g->sf);
        new_operands[1].lit_ui32 = MVM_disp_inline_cache_get_slot(tc, g->sf,
            ann->data.bytecode_offset);
        ins->operands = new_operands;
    }
}

static void optimize_guard(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                      MVMSpeshIns *ins) {
    MVMuint16 opcode = ins->info->opcode;

    MVMuint8 can_drop_type_guard = 0;
    MVMuint8 can_drop_concrete_guard = 0;
    MVMuint8 can_drop_typeobj_guard = 0;
    MVMuint8 turn_into_set = 0;

    /* The sslot is always the second-to-last parameter, except for
     * justconc and justtype, which don't have a spesh slot. */
    MVMuint16 sslot = ins->operands[ins->info->num_operands - 2].lit_i16;

    MVMSpeshFacts *facts    = &g->facts[ins->operands[1].reg.orig][ins->operands[1].reg.i];

    if (opcode == MVM_OP_sp_guardobj) {
        if ((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) && facts->value.o == (MVMObject *)g->spesh_slots[sslot]) {
            turn_into_set = 1;
        }
    }
    else if (opcode == MVM_OP_sp_guardnonzero) {
        if ((facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) && facts->value.i != 0) {
            turn_into_set = 1;
        }
    }
    else {
        if (opcode == MVM_OP_sp_guard
                || opcode == MVM_OP_sp_guardconc
                || opcode == MVM_OP_sp_guardtype) {
            if ((facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) && facts->type == ((MVMSTable *)g->spesh_slots[sslot])->WHAT) {
                can_drop_type_guard = 1;
                MVM_spesh_graph_add_comment(tc, g, ins, "used to guard for %s",
                        MVM_6model_get_debug_name(tc, facts->type));
            }
        }
        if (opcode == MVM_OP_sp_guardconc || opcode == MVM_OP_sp_guardjustconc) {
            if (facts->flags & MVM_SPESH_FACT_CONCRETE) {
                can_drop_concrete_guard = 1;
            }
        }
        if (opcode == MVM_OP_sp_guardtype || opcode == MVM_OP_sp_guardjusttype) {
            if (facts->flags & MVM_SPESH_FACT_TYPEOBJ) {
                can_drop_typeobj_guard = 1;
            }
        }
        if (can_drop_type_guard && (can_drop_concrete_guard || can_drop_typeobj_guard)) {
            turn_into_set = 1;
        }
        else if (opcode == MVM_OP_sp_guard && can_drop_type_guard) {
            turn_into_set = 1;
        }
        else if (  (opcode == MVM_OP_sp_guardjustconc && can_drop_concrete_guard)
                || (opcode == MVM_OP_sp_guardjusttype && can_drop_typeobj_guard)) {
            turn_into_set = 1;
        }
    }
    if (turn_into_set) {
        MVM_spesh_turn_into_set(tc, g, ins);
        MVM_spesh_use_facts(tc, g, facts);
    }
}

/* If we have a const_i and a coerce_in, we can emit a const_n instead. */
static void optimize_coerce(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);

    if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        MVMSpeshFacts *result_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        MVMnum64 result = facts->value.i;

        MVM_spesh_use_facts(tc, g, facts);
        MVM_spesh_usages_delete(tc, g, facts, ins);

        ins->info = MVM_op_get_op(MVM_OP_const_n64);
        ins->operands[1].lit_n64 = result;

        result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        result_facts->value.n = result;
    }
}

/* If we have a const_i and a coerce_iu or coerce_ui, we can emit a const_i instead. */
static void optimize_signedness_coerce(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    // fooled you! these coerces are actually
    // equivalent to "set" in implementation.
    MVM_spesh_turn_into_set(tc, g, ins);
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

/* Optimize string equality if one param is the empty string */
static void optimize_string_equality(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshFacts *a_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    MVMSpeshFacts *b_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);
    MVMuint8 was_eq = 0;

    if (ins->info->opcode == MVM_OP_eq_s)
        was_eq = 1;

    if (a_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE && b_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        /* Cool, we can constant-fold this. */
        MVMSpeshFacts *target_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);

        MVM_spesh_usages_delete(tc, g, a_facts, ins);
        MVM_spesh_usages_delete(tc, g, b_facts, ins);

        ins->operands[1].lit_i16 = MVM_string_equal(tc, a_facts->value.s, b_facts->value.s);
        if (!was_eq)
            ins->operands[1].lit_i16 = !ins->operands[1].lit_i16;
        ins->info = MVM_op_get_op(MVM_OP_const_i64_16);

        target_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
        target_facts->value.i = ins->operands[1].lit_i16;
    }
    else if (a_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE || b_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        MVMSpeshFacts *the_facts =
            a_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE ? a_facts : b_facts;

        if (MVM_string_graphs(tc, the_facts->value.s) == 0) {
            /* Turn this into an istrue_s or isfalse_s */
            ins->info = MVM_op_get_op(was_eq ? MVM_OP_isfalse_s : MVM_OP_istrue_s);

            /* Throw out the string argument that was the empty string */
            if (the_facts == a_facts)
                ins->operands[1] = ins->operands[2];
            MVM_spesh_usages_delete(tc, g, the_facts, ins);
        }
    }
}

/* Optimizes a hllbool instruction away if the value is known */
static void optimize_hllbool(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMuint32 for_op = ins->info->opcode == MVM_OP_hllboolfor ? 1 : 0;
    MVMSpeshFacts *obj_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
    if (obj_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        MVMSpeshFacts *tgt_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        MVMObject *hll_bool;
        MVMHLLConfig *hll_config;
        if (for_op) {
            MVMSpeshFacts *for_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);
            if (!(for_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE))
                return;
            hll_config = MVM_hll_get_config_for(tc, for_facts->value.s);
        }
        else {
            hll_config = g->sf->body.cu->body.hll_config;
        }
        hll_bool = obj_facts->value.i ? hll_config->true_value : hll_config->false_value;

        MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
        if (for_op)
            MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);

        ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
        ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
            (MVMCollectable*)hll_bool);
        tgt_facts->flags  |= (MVM_SPESH_FACT_KNOWN_VALUE | MVM_SPESH_FACT_KNOWN_TYPE);
        tgt_facts->value.o = hll_bool;
        tgt_facts->type    = STABLE(hll_bool)->WHAT;
        MVM_spesh_use_facts(tc, g, obj_facts);
        MVM_spesh_facts_depend(tc, g, tgt_facts, obj_facts);
    }
}

/* Gets the kind of register a lexical lookup targets. */
static MVMuint16 get_lexical_kind(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o) {
    MVMuint16 *lexical_types;
    MVMuint16 i;
    MVMStaticFrame *sf = g->sf;
    for (i = 0; i < o.lex.outers; i++)
        sf = sf->body.outer;
    lexical_types = sf == g->sf && g->lexical_types
        ? g->lexical_types
        : sf->body.lexical_types;
    return lexical_types[o.lex.idx];
}

/* Turns a getlex instruction into getlex_o or getlex_ins depending on type;
 * these get rid of some branching as well as don't log. */
static void optimize_getlex(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    ins->info = MVM_op_get_op(get_lexical_kind(tc, g, ins->operands[1]) == MVM_reg_obj
        ? MVM_OP_sp_getlex_o
        : MVM_OP_sp_getlex_ins);
}

/* Turns a bindlex instruction into sp_bindlex_in or sp_bindlex_os depending
 * on type; these get rid of checks whether we need the write barrier. */
static void optimize_bindlex(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMuint16 kind = get_lexical_kind(tc, g, ins->operands[0]);
    ins->info = MVM_op_get_op(kind == MVM_reg_obj || kind == MVM_reg_str
        ? MVM_OP_sp_bindlex_os
        : MVM_OP_sp_bindlex_in);
}

/* Transforms a late-bound lexical lookup into a constant. */
static void lex_to_constant(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
                            MVMObject *log_obj) {
    MVMSpeshFacts *facts;

    /* Place in a spesh slot. */
    MVMuint16 ss = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
        (MVMCollectable *)log_obj);

    if (MVM_spesh_debug_enabled(tc)) {
        MVMSpeshFacts *name_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
        char *lexname = MVM_string_utf8_encode_C_string(tc, name_facts->value.s);

        MVM_spesh_graph_add_comment(tc, g, ins, "%s of lexical '%s'",
                ins->info->name, lexname);
        MVM_free(lexname);
    }

    /* Transform lookup instruction into spesh slot read. */
    ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
    MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
    ins->operands[1].lit_i16 = ss;

    /* Set up facts. */
    facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
    facts->flags  |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_KNOWN_VALUE;
    facts->type    = STABLE(log_obj)->WHAT;
    facts->value.o = log_obj;
    if (IS_CONCRETE(log_obj))
        facts->flags |= MVM_SPESH_FACT_CONCRETE;
    else
        facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
}

/* Optimizes away a lexical lookup when we know the value won't change from
 * the logged one. */
static void optimize_getlexstatic(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                  MVMSpeshIns *ins) {
    /* Look up the bytecode offset (should never be missing). */
    MVMSpeshAnn *ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_CACHED)
            break;
        ann = ann->next;
    }
    if (ann) {
        /* If we resolved the lexical already, replace it with a constnat. */
        MVMObject *resolution =  MVM_disp_inline_cache_get_lex_resolution(tc, g->sf,
                ann->data.bytecode_offset);
        if (resolution) {
            lex_to_constant(tc, g, ins, resolution);
            return;
        }

        /* Failing that, we rewrite the instruction into a specialized getlexstatic
         * instruction, with the slot pre-calculated and the static frame stored in
         * a spesh slot (making it inlineable). */
        ins->info = MVM_op_get_op(MVM_OP_sp_getlexstatic_o);
        MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
        new_operands[0] = ins->operands[0];
        new_operands[1] = ins->operands[1];
        new_operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                (MVMCollectable *)g->sf);
        new_operands[3].lit_ui32 = MVM_disp_inline_cache_get_slot(tc, g->sf,
                ann->data.bytecode_offset);
        ins->operands = new_operands;
    }
    else {
        MVM_oops(tc, "Spesh optimize: missing bytecode offset for getlexstatic_o");
    }
}

/* Optimizes away a lexical lookup when we know the value won't change for a
 * given invocant type (this relies on us being in a typed specialization). */
static void optimize_getlex_per_invocant(MVMThreadContext *tc, MVMSpeshGraph *g,
                                         MVMSpeshBB *bb, MVMSpeshIns *ins,
                                         MVMSpeshPlanned *p) {
    MVMSpeshAnn *ann;

    /* Can only do this when we've specialized on the first argument type and
     * we have a plan. */
    if (!p || !g->specialized_on_invocant)
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
                    if (ts->by_offset[j].num_types == 1) {
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

/* Find the dispatch cache bytecode offset of the given instruction. Returns 0
 * if not found. */
static MVMuint32 find_cache_offset(MVMThreadContext *tc, MVMSpeshIns *ins) {
    MVMSpeshAnn *ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_CACHED)
            return ann->data.bytecode_offset;
        ann = ann->next;
    }
    return 0;
}

/* Find the pre-deopt index for the dispatch that was translated to a sequence
 * of operations culminating in the runbytecode instruction. It may be on the
 * runbytecode itself, if no guards were stacked up before it, but may also be
 * earlier (but always in the same basic block). */
static MVMint32 find_predeopt_index(MVMThreadContext *tc, MVMSpeshIns *ins) {
    while (ins) {
        MVMSpeshAnn *ann = ins->annotations;
        while (ann) {
            if (ann->type == MVM_SPESH_ANN_DEOPT_SYNTH)
                return ann->data.deopt_idx;
            ann = ann->next;
        }
        ann = ins->annotations;
        while (ann) {
            if (ann->type == MVM_SPESH_ANN_DEOPT_PRE_INS)
                return ann->data.deopt_idx;
            ann = ann->next;
        }
        ins = ins->prev;
    }
    return -1;
}

/* Given an instruction, finds the deopt target on it. Panics if there is not
 * one there. */
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
    MVM_panic(1, "Spesh: unexpectedly missing deopt annotation");
}

/* Given a callsite instruction, finds the type tuples there and checks if
 * there is a relatively stable one. */
static MVMSpeshStatsType * find_invokee_type_tuple(MVMThreadContext *tc,
        MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins,
        MVMSpeshPlanned *p, MVMuint32 bytecode_offset, MVMCallsite *expect_cs) {
    MVMuint32 i;
    MVMSpeshStatsType *best_result = NULL;
    MVMuint32 best_result_hits = 0;
    MVMuint32 total_hits = 0;
    size_t tt_size = expect_cs->flag_count * sizeof(MVMSpeshStatsType);

    /* Now look for the best type tuple. */
    for (i = 0; i < p->num_type_stats; i++) {
        MVMSpeshStatsByType *ts = p->type_stats[i];
        MVMuint32 j;
        for (j = 0; j < ts->num_by_offset; j++) {
            if (ts->by_offset[j].bytecode_offset == bytecode_offset) {
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

/* Sees if any static frames were logged for the dispatch at this location,
 * and if so checks if there was a stable one. */
static MVMStaticFrame * find_runbytecode_static_frame(MVMThreadContext *tc, MVMSpeshPlanned *p,
        MVMSpeshIns *ins, MVMuint32 cache_offset) {
    MVMuint32 i;
    MVMStaticFrame *best_result = NULL;
    MVMuint32 best_result_hits = 0;
    MVMuint32 total_hits = 0;

    /* Now look for a stable invokee. */
    if (!p)
        return NULL;
    for (i = 0; i < p->num_type_stats; i++) {
        MVMSpeshStatsByType *ts = p->type_stats[i];
        MVMuint32 j;
        for (j = 0; j < ts->num_by_offset; j++) {
            if (ts->by_offset[j].bytecode_offset == cache_offset) {
                MVMSpeshStatsByOffset *by_offset = &(ts->by_offset[j]);
                MVMuint32 k;
                for (k = 0; k < by_offset->num_invokes; k++) {
                    MVMSpeshStatsInvokeCount *ic = &(by_offset->invokes[k]);

                    /* Add hits to total we've seen. */
                    total_hits += ic->count;

                    /* If it's the same as the best so far, add hits. */
                    if (best_result && ic->sf == best_result) {
                        best_result_hits += ic->count;
                    }

                    /* Otherwise, if it beats the best result in hits, use. */
                    else if (ic->count > best_result_hits) {
                        best_result = ic->sf;
                        best_result_hits = ic->count;
                    }
                }
            }
        }
    }

    /* If the static frame is consistent enough, return it. */
    return total_hits && (100 * best_result_hits) / total_hits >= MVM_SPESH_CALLSITE_STABLE_PERCENT
        ? best_result
        : NULL;
}

/* Take the predeopt index for a dispatch. Add a synthetic deopt annotation
 * to the instruction, to ensure the guard keeps alive deopt usages of the
 * pre-deopt point if it ends up as the only one. Also produce a new deopt
 * index for the guard currently being produced and return it. */
static MVMuint32 add_deopt_ann(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
        MVMint32 predeopt_index) {
    MVMSpeshAnn *ann = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
    ann->type = MVM_SPESH_ANN_DEOPT_SYNTH;
    ann->data.deopt_idx = predeopt_index;
    ann->next = ins->annotations;
    ins->annotations = ann;
    return MVM_spesh_graph_add_deopt_annotation(tc, g, ins,
            g->deopt_addrs[predeopt_index * 2], MVM_SPESH_ANN_DEOPT_PRE_INS);
}

/* Inserts a static frame guard instruction. */
static void insert_static_frame_guard(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns *ins, MVMSpeshOperand coderef_reg,
        MVMStaticFrame *target_sf, MVMint32 predeopt_index) {
    MVMSpeshIns *guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(MVM_OP_sp_guardsf);
    guard->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
    guard->operands[0] = coderef_reg;
    MVM_spesh_usages_add_by_reg(tc, g, coderef_reg, guard);
    guard->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
        (MVMCollectable *)target_sf);
    guard->operands[2].lit_ui32 = add_deopt_ann(tc, g, guard, predeopt_index);
    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard);
}

/* Inserts an argument type guard as suggested by a logged type tuple. */
static void insert_arg_type_guard(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns *ins, MVMint32 predeopt_index,
        MVMSpeshStatsType *type_info, MVMSpeshOperand preguard_reg) {
    /* Split the SSA version of the arg. */
    MVMSpeshOperand guard_reg = MVM_spesh_manipulate_split_version(tc, g,
            preguard_reg, bb, ins);

    /* Insert guard before the runbytecode instruction. */
    MVMSpeshIns *guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(type_info->type_concrete
        ? MVM_OP_sp_guardconc
        : MVM_OP_sp_guardtype);
    guard->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
    guard->operands[0] = guard_reg;
    MVM_spesh_get_facts(tc, g, guard_reg)->writer = guard;
    guard->operands[1] = preguard_reg;
    MVM_spesh_usages_add_by_reg(tc, g, preguard_reg, guard);
    guard->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
        (MVMCollectable *)type_info->type->st);
    guard->operands[3].lit_ui32 = add_deopt_ann(tc, g, guard, predeopt_index);
    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard);
    MVM_spesh_facts_guard_facts(tc, g, bb, guard);
    MVM_spesh_graph_add_comment(tc, g, guard, "Inserted to use specialization");
}

/* Inserts an argument decont type guard as suggested by a logged type tuple. */
static void insert_arg_decont_type_guard(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns *ins, MVMint32 predeopt_index,
        MVMSpeshStatsType *type_info, MVMSpeshOperand to_guard) {
    /* We need a temporary register to decont into. */
    MVMSpeshOperand temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);

    /* Insert the decont, then try to optimize it into something cheaper. */
    MVMSpeshIns *decont = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    decont->info = MVM_op_get_op(MVM_OP_decont);
    decont->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    decont->operands[0] = temp;
    MVM_spesh_get_facts(tc, g, temp)->writer = decont;
    decont->operands[1] = to_guard;
    MVM_spesh_usages_add_by_reg(tc, g, to_guard, decont);
    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, decont);
    MVM_spesh_graph_add_comment(tc, g, decont, "Decontainerized for guarding");
    optimize_decont(tc, g, bb, decont);

    /* Guard the decontainerized value. */
    MVMSpeshIns *guard = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    guard->info = MVM_op_get_op(type_info->decont_type_concrete
        ? MVM_OP_sp_guardconc
        : MVM_OP_sp_guardtype);
    guard->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
    MVM_spesh_manipulate_release_temp_reg(tc, g, temp);
    guard->operands[0] = MVM_spesh_manipulate_new_version(tc, g, temp.reg.orig);
    MVM_spesh_get_facts(tc, g, guard->operands[0])->writer = guard;
    guard->operands[1] = temp;
    MVM_spesh_usages_add_by_reg(tc, g, temp, guard);
    guard->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
        (MVMCollectable *)type_info->decont_type->st);
    guard->operands[3].lit_ui32 = add_deopt_ann(tc, g, guard, predeopt_index);
    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard);
}

/* Look through the call info and the type tuple, see what guards we are
 * missing, and insert them. */
static void check_and_tweak_arg_guards(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshBB *bb, MVMSpeshIns *ins, MVMuint32 predeopt_index,
        MVMSpeshStatsType *type_tuple, MVMCallsite *cs, MVMSpeshOperand *args) {
    MVMuint32 n = cs->flag_count;
    MVMuint32 i;
    for (i = 0; i < n; i++) {
        if (cs->arg_flags[i] & MVM_CALLSITE_ARG_OBJ) {
            MVMObject *t_type = type_tuple[i].type;
            MVMObject *t_decont_type = type_tuple[i].decont_type;
            if (t_type) {
                /* Add a guard unless the facts already match. */
                MVMSpeshFacts *arg_facts = MVM_spesh_get_and_use_facts(tc, g, args[i]);
                MVMuint32 need_guard = !arg_facts ||
                    !(arg_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) ||
                    arg_facts->type != t_type ||
                    (type_tuple[i].type_concrete
                        && !(arg_facts->flags & MVM_SPESH_FACT_CONCRETE)) ||
                    (!type_tuple[i].type_concrete
                        && !(arg_facts->flags & MVM_SPESH_FACT_TYPEOBJ));
                if (need_guard)
                    insert_arg_type_guard(tc, g, bb, ins, predeopt_index,
                        &type_tuple[i], args[i]);
            }
            if (t_decont_type)
                insert_arg_decont_type_guard(tc, g, bb, ins, predeopt_index,
                    &type_tuple[i], args[i]);
        }
    }
}

/* Ties to optimize a runbytecode instruction by either pre-selecting a spesh
 * candidate or, if possible, inlining it. */
static void optimize_runbytecode(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
        MVMSpeshIns *ins, MVMSpeshPlanned *p) {
    /* Make sure we can find the dispatch bytecode offset (used for both
     * looking up in the spesh log) and the pre-deopt index (for use with
     * any guards we add). */
    MVMuint32 bytecode_offset = find_cache_offset(tc, ins);
    if (!bytecode_offset)
        return;
    MVMint32 predeopt_index = find_predeopt_index(tc, ins);
    if (predeopt_index < 0)
        return;

    /* Extract the interesting parts. */
    MVMSpeshOperand coderef_reg;
    MVMCallsite *cs;
    MVMSpeshOperand *args;
    MVMSpeshOperand *selected;
    if (ins->info->opcode == MVM_OP_sp_runbytecode_v) {
        coderef_reg = ins->operands[0];
        cs = (MVMCallsite *)ins->operands[1].lit_ui64;
        selected = &(ins->operands[2]);
        args = ins->operands + 3;
    }
    else {
        coderef_reg = ins->operands[1];
        cs = (MVMCallsite *)ins->operands[2].lit_ui64;
        selected = &(ins->operands[3]);
        args = ins->operands + 4;
    }

    /* If there's any resume initializations, keep track of the final one. */
    MVMSpeshIns *resume_init = ins->prev && ins->prev->info->opcode == MVM_OP_sp_resumption
        ? ins->prev
        : NULL;

    /* Is the bytecode we're invoking a constant? */
    MVMSpeshFacts *coderef_facts = MVM_spesh_get_and_use_facts(tc, g, coderef_reg);
    MVMStaticFrame *target_sf = NULL;
    MVMuint32 need_guardsf;
    if (coderef_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
        /* Yes, so we know exactly what we'll be invoking. */
        target_sf = ((MVMCode *)coderef_facts->value.o)->body.sf;
        need_guardsf = 0;
    }
    else {
        /* No; see if there's a stable target static frame from the log, and
         * if so add a guard on the static frame. */
        target_sf = find_runbytecode_static_frame(tc, p, ins, bytecode_offset);
        need_guardsf = 1;
    }
    if (!target_sf) {
        MVM_spesh_graph_add_comment(tc, g, ins,
                "Cannot specialize runbytecode; no target static frame found");
        return;
    }

    /* If the target static frame is not invoked or has no specializations,
     * give up. */
    if (target_sf->body.instrumentation_level != tc->instance->instrumentation_level)
        return;
    if (!target_sf->body.spesh)
        return;

    /* See if there's a stable type tuple at this callsite. Failing that,
     * form one based on the facts. */
    MVMSpeshStatsType *stable_type_tuple = p
        ? find_invokee_type_tuple(tc, g, bb, ins, p, bytecode_offset, cs)
        : NULL;
    MVMint32 type_tuple_from_facts;
    if (stable_type_tuple) {
        type_tuple_from_facts = 0;
    }
    else {
        MVMuint16 flags = cs->flag_count;
        stable_type_tuple = MVM_spesh_alloc(tc, g, flags * sizeof(MVMSpeshStatsType));
        MVMuint16 i;
        for (i = 0; i < flags; i++) {
            if (cs->arg_flags[i] & MVM_CALLSITE_ARG_OBJ) {
                MVMSpeshFacts *facts = MVM_spesh_get_and_use_facts(tc, g, args[i]);
                if (facts) {
                    if (facts->flags & MVM_SPESH_FACT_KNOWN_TYPE &&
                            (facts->flags & (MVM_SPESH_FACT_CONCRETE | MVM_SPESH_FACT_TYPEOBJ))) {
                        stable_type_tuple[i].type = facts->type;
                        stable_type_tuple[i].type_concrete = facts->flags & MVM_SPESH_FACT_CONCRETE;
                    }
                    else if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
                        stable_type_tuple[i].type = STABLE(facts->value.o)->WHAT;
                        stable_type_tuple[i].type_concrete = IS_CONCRETE(facts->value.o);
                    }
                }
            }
        }
        type_tuple_from_facts = 1;
    }

    /* Try to find a specialization. */
    MVMSpeshArgGuard *ag = target_sf->body.spesh->body.spesh_arg_guard;
    MVMint16 spesh_cand = MVM_spesh_arg_guard_run_types(tc, ag, cs, stable_type_tuple);
   if (spesh_cand >= 0) {
       /* Found a candidate. Stack up any required guards. */
       if (need_guardsf)
           insert_static_frame_guard(tc, g, bb, ins, coderef_reg, target_sf,
               predeopt_index);
        if (!type_tuple_from_facts)
            check_and_tweak_arg_guards(tc, g, bb, ins, predeopt_index,
                stable_type_tuple, cs, args);

        /* See if we'll be able to inline it. */
        char *no_inline_reason = NULL;
        const MVMOpInfo *no_inline_info = NULL;
        MVMuint32 effective_size;
        /* Do not try to inline calls from inlined basic blocks! Otherwise the new inlinees would
         * get added to the inlines table after the original inlinee which they are nested in and
         * the frame walker would find the outer inlinee first, giving wrong results */
        MVMSpeshGraph *inline_graph = bb->inlined ? NULL : MVM_spesh_inline_try_get_graph(tc, g,
            target_sf, target_sf->body.spesh->body.spesh_candidates[spesh_cand],
            ins, &no_inline_reason, &effective_size, &no_inline_info);
        log_inline(tc, g, target_sf, inline_graph, effective_size, no_inline_reason,
            0, no_inline_info);
        if (inline_graph) {
            /* Yes, have inline graph, so go ahead and do it. Make sure we
             * keep the code ref reg alive by giving it a usage count as
             * it will be referenced from the deopt table. */
            MVMSpeshBB *optimize_from_bb = inline_graph->entry;
            MVM_spesh_usages_add_unconditional_deopt_usage_by_reg(tc, g, coderef_reg);
            MVM_spesh_inline(tc, g, cs, args, bb, ins, inline_graph, target_sf,
                coderef_reg, resume_init,
                (MVMuint16)target_sf->body.spesh->body.spesh_candidates[spesh_cand]->body.bytecode_size);
            optimize_bb(tc, g, optimize_from_bb, NULL);

            /* In debug mode, annotate what we inlined. */
            if (MVM_spesh_debug_enabled(tc)) {
                char *cuuid_cstr = MVM_string_utf8_encode_C_string(tc, target_sf->body.cuuid);
                char *name_cstr  = MVM_string_utf8_encode_C_string(tc, target_sf->body.name);
                MVMSpeshBB *pointer = bb->succ[0];
                while (!pointer->first_ins && pointer->num_succ > 0) {
                    pointer = pointer->succ[0];
                }
                if (pointer->first_ins)
                    MVM_spesh_graph_add_comment(tc, g, pointer->first_ins,
                        "inline of '%s' (%s) candidate %d",
                        name_cstr, cuuid_cstr,
                        spesh_cand);
                MVM_free(cuuid_cstr);
                MVM_free(name_cstr);
            }
        }
        else {
            /* Can't inline, but can still set the chosen spesh candidate in
             * the runbytecode instruction. */
            selected->lit_i16 = spesh_cand;

            /* Maybe add inline blocking reason as a comment also. */
            if (MVM_spesh_debug_enabled(tc)) {
                char *cuuid_cstr = MVM_string_utf8_encode_C_string(tc, target_sf->body.cuuid);
                char *name_cstr  = MVM_string_utf8_encode_C_string(tc, target_sf->body.name);
                MVM_spesh_graph_add_comment(tc, g, ins,
                    "could not inline '%s' (%s) candidate %d: %s",
                    name_cstr, cuuid_cstr, spesh_cand, no_inline_reason);
                if (no_inline_info)
                    MVM_spesh_graph_add_comment(tc, g, ins, "inline-preventing instruction: %s",
                        no_inline_info->name);
                MVM_free(cuuid_cstr);
                MVM_free(name_cstr);
            }
        }
    }
    /* Do not try to inline calls from inlined basic blocks! Otherwise the new inlinees would
     * get added to the inlines table after the original inlinee which they are nested in and
     * the frame walker would find the outer inlinee first, giving wrong results */
    else if (!bb->inlined && target_sf->body.bytecode_size < MVM_spesh_inline_get_max_size(tc, target_sf)) {
        /* Consider producing a candidate to inline. */
        char *no_inline_reason = NULL;
        const MVMOpInfo *no_inline_info = NULL;
        MVMSpeshGraph *inline_graph = MVM_spesh_inline_try_get_graph_from_unspecialized(
                tc, g, target_sf, ins, cs, args, stable_type_tuple,
                &no_inline_reason, &no_inline_info);
        log_inline(tc, g, target_sf, inline_graph, target_sf->body.bytecode_size,
                no_inline_reason, 1, no_inline_info);
        if (inline_graph) {
            /* We can do the inline. Stack up any required guards. */
            if (need_guardsf)
                insert_static_frame_guard(tc, g, bb, ins, coderef_reg, target_sf,
                    predeopt_index);
            if (!type_tuple_from_facts)
                check_and_tweak_arg_guards(tc, g, bb, ins, predeopt_index,
                    stable_type_tuple, cs, args);

            /* Optimize and then inline the graph. */
            MVMSpeshBB *optimize_from_bb = inline_graph->entry;
            MVM_spesh_usages_add_unconditional_deopt_usage_by_reg(tc, g, coderef_reg);
            MVM_spesh_inline(tc, g, cs, args, bb, ins, inline_graph, target_sf,
                    coderef_reg, resume_init, 0); /* Don't know an accurate size */
            optimize_bb(tc, g, optimize_from_bb, NULL);
        }
    }

    /* Otherwise, nothing to be done. */
    else {
        log_inline(tc, g, target_sf, NULL, target_sf->body.bytecode_size,
            "no spesh candidate available and bytecode too large to produce an inline",
            0, NULL);
    }
}

static void optimize_coverage_log(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    char *cache        = (char *)(uintptr_t)ins->operands[3].lit_i64;
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
            MVM_spesh_usages_delete(tc, g, arg1_facts, ins);
        } else if (ins->info->opcode == MVM_OP_unipvalcode) {
            MVMSpeshFacts *arg2_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);

            if (arg2_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
                result_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                result_facts->value.i = (MVMint64)MVM_unicode_name_to_property_value_code(tc, arg1_facts->value.i, arg2_facts->value.s);
                ins->info = MVM_op_get_op(MVM_OP_const_i64);
                ins->operands[1].lit_i64 = result_facts->value.i;
                MVM_spesh_usages_delete(tc, g, arg1_facts, ins);
                MVM_spesh_usages_delete(tc, g, arg2_facts, ins);
            }
        }
    }
}

/* If something is only kept alive because we log its allocation, kick out
 * the allocation logging and let the op that creates it die. */
static void optimize_prof_allocated(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    if (MVM_spesh_usages_used_once(tc, g, ins->operands[0])) {
        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
    }
}

/* Tries to optimize a throwcat instruction. Note that within a given frame
 * (we don't consider inlines here) the throwcat instructions all have the
 * same semantics. */
static void optimize_throwcat(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    /* First, see if we have any goto handlers for this category. */
    MVMint32 *handlers_found = MVM_malloc(g->num_handlers * sizeof(MVMint32));
    MVMuint32 num_found      = 0;
    MVMuint32 category       = (MVMuint32)ins->operands[1].lit_i64;
    MVMuint32  i;
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
                            if (picked >= 0 && ann->data.frame_handler_index == (MVMuint32)picked)
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
            MVMSpeshFacts *resume_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
            resume_facts->writer = NULL;
            resume_facts->dead_writer = 1;

            MVM_spesh_graph_add_comment(tc, g, ins, "%s of category %ld for handler %d",
                    ins->info->name, ins->operands[1].lit_i64, picked);

            ins->info = MVM_op_get_op(MVM_OP_goto);
            ins->operands[0].ins_bb = goto_bbs[picked];

            while (bb->num_succ)
                MVM_spesh_manipulate_remove_successor(tc, bb, bb->succ[0]);
            MVM_spesh_manipulate_add_successor(tc, g, bb, goto_bbs[picked]);
        }

        MVM_free(in_handlers);
        MVM_free(goto_bbs);
    }

    MVM_free(handlers_found);
}

/* Updates rebless with rebless_sp, which will deopt from the current code. */
static void tweak_rebless(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMuint32 deopt_target, deopt_index;
    MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
    new_operands[0] = ins->operands[0];
    new_operands[1] = ins->operands[1];
    new_operands[2] = ins->operands[2];
    find_deopt_target_and_index(tc, g, ins, &deopt_target, &deopt_index);
    new_operands[3].lit_ui32 = deopt_index;
    ins->info = MVM_op_get_op(MVM_OP_sp_rebless);
    ins->operands = new_operands;
}

/* A wval instruction does a few checks on its way; it's faster to pop the
 * value into a spesh splot. */
static void optimize_wval(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMObject *value = MVM_spesh_get_facts(tc, g, ins->operands[0])->value.o;
    if (value) {
        ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                (MVMCollectable *)value);
        ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
    }
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
                if (!cs->store_atomic)
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

/* Lower bigint binary ops to specialized forms where possible. */
static void optimize_bigint_binary_op(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    /* Check that input types and result type are consistent. */
    MVMObject *common_type = NULL;
    MVMSpeshFacts *facts[3];
    MVMuint32 i;
    for (i = 1; i < 4; i++) {
        facts[i - 1] = MVM_spesh_get_facts(tc, g, ins->operands[i]);
        if (facts[i - 1]->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
            if (common_type == NULL) {
                common_type = facts[i - 1]->type;
            }
            else if (facts[i - 1]->type != common_type) {
                common_type = NULL;
                break;
            }
        }
        else {
            common_type = NULL;
            break;
        }
    }
    if (common_type && REPR(common_type)->ID == MVM_REPR_ID_P6opaque) {
        MVMuint16 offset = MVM_p6opaque_get_bigint_offset(tc, common_type->st);
        MVMint16 cache_type_index = MVM_intcache_type_index(tc, common_type->st->WHAT);
        if (offset && cache_type_index >= 0) {
            /* Lower the op. */
            MVMSpeshOperand *orig_operands = ins->operands;
            switch (ins->info->opcode) {
                case MVM_OP_add_I: ins->info = MVM_op_get_op(MVM_OP_sp_add_I); break;
                case MVM_OP_sub_I: ins->info = MVM_op_get_op(MVM_OP_sp_sub_I); break;
                case MVM_OP_mul_I: ins->info = MVM_op_get_op(MVM_OP_sp_mul_I); break;
                default: return;
            }
            ins->operands = MVM_spesh_alloc(tc, g, 7 * sizeof(MVMSpeshOperand));
            ins->operands[0] = orig_operands[0];
            ins->operands[1].lit_i16 = common_type->st->size;
            ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                    (MVMCollectable *)common_type->st);
            ins->operands[3] = orig_operands[1];
            ins->operands[4] = orig_operands[2];
            ins->operands[5].lit_i16 = offset;
            ins->operands[6].lit_i16 = cache_type_index;
            MVM_spesh_usages_delete_by_reg(tc, g, orig_operands[3], ins);

            /* Mark all facts as used. */
            for (i = 0; i < 3; i++)
                MVM_spesh_use_facts(tc, g, facts[i]);
        }
    }
}

/* the bool_I op is implemented as two extremely cheap checks, as long as you
 * don't count getting the bigint body out of the containing object. That's
 * why it's very beneficial to give it the "calculate offset into object
 * body statically and access directly via an sp_ op" treatment. */
static void optimize_bigint_bool_op(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMObject *type = NULL;
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);

    if (facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) {
        type = facts->type;
    }

    if (type && REPR(type)->ID == MVM_REPR_ID_P6opaque) {
        MVMuint16 offset = MVM_p6opaque_get_bigint_offset(tc, type->st);
        if (offset) {
            MVMSpeshOperand input = ins->operands[1];
            MVMSpeshOperand output = ins->operands[0];

            ins->info = MVM_op_get_op(MVM_OP_sp_bool_I);
            ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0] = output;
            ins->operands[1] = input;
            ins->operands[2].lit_i16 = offset;

            MVM_spesh_use_facts(tc, g, facts);
        }
    }
}

static int eliminate_phi_dead_reads(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMuint32 operand = 1;
    MVMuint32 insert_pos = 1;
    MVMuint32 num_operands = ins->info->num_operands;
    while (operand < ins->info->num_operands) {
        if (get_facts_direct(tc, g, ins->operands[operand])->dead_writer) {
            MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[operand], ins);
            num_operands--;
        }
        else {
            ins->operands[insert_pos] = ins->operands[operand];
            insert_pos++;
        }
        operand++;
    }
    if (num_operands != ins->info->num_operands)
        ins->info = MVM_spesh_graph_get_phi(tc, g, num_operands);
    if (num_operands <= 1) {
        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
        return 0;
    }
    return 1;
}
static void analyze_phi(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMuint32 operand;
    MVMint32 common_flags;
    MVMObject *common_type;
    MVMObject *common_value;
    MVMObject *common_decont_type;
    MVMSpeshFacts *target_facts = get_facts_direct(tc, g, ins->operands[0]);
    MVMSpeshFacts *cur_operand_facts;

    /* If we have facts that we merge that depended on log guards, then we need
     * to carry them forward. */
    MVMuint32 total_log_guards = 0;

    if (!eliminate_phi_dead_reads(tc, g, bb, ins))
        return; /* PHI eliminated completely */

    cur_operand_facts = get_facts_direct(tc, g, ins->operands[1]);
    common_flags       = cur_operand_facts->flags;
    common_type        = cur_operand_facts->type;
    common_value       = cur_operand_facts->value.o;
    common_decont_type = cur_operand_facts->decont_type;
    total_log_guards   = cur_operand_facts->num_log_guards;

    for (operand = 2; operand < ins->info->num_operands; operand++) {
        cur_operand_facts = get_facts_direct(tc, g, ins->operands[operand]);
        common_flags = common_flags & cur_operand_facts->flags;
        common_type = common_type == cur_operand_facts->type && common_type ? common_type : NULL;
        common_value = common_value == cur_operand_facts->value.o && common_value ? common_value : NULL;
        common_decont_type = common_decont_type == cur_operand_facts->decont_type && common_decont_type
            ? common_decont_type
            : NULL;
        total_log_guards += cur_operand_facts->num_log_guards;
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
        if (common_flags & MVM_SPESH_FACT_KNOWN_VALUE) {
            if (common_value) {
                target_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                target_facts->value.o = common_value;
            }
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

        if (total_log_guards) {
            MVMuint32 insert_pos = 0;
            target_facts->num_log_guards = total_log_guards;
            target_facts->log_guards = MVM_spesh_alloc(tc, g, total_log_guards * sizeof(MVMint32));
            for (operand = 1; operand < ins->info->num_operands; operand++) {
                cur_operand_facts = get_facts_direct(tc, g, ins->operands[operand]);
                if (cur_operand_facts->num_log_guards) {
                    memcpy(target_facts->log_guards + insert_pos, cur_operand_facts->log_guards,
                            cur_operand_facts->num_log_guards * sizeof(MVMint32));
                    insert_pos += cur_operand_facts->num_log_guards;
                }
            }
        }
    } else {
        /*fprintf(stderr, "a PHI node of %d operands had no intersecting flags\n", ins->info->num_operands);*/
    }
}

static void optimize_bb_switch(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                        MVMSpeshPlanned *p) {
    /* Look for instructions that are interesting to optimize. */
    MVMSpeshIns *ins = bb->first_ins;
    MVMSpeshIns *next_ins = ins;
    while (ins) {
        next_ins = ins->next;
        switch (ins->info->opcode) {
        case MVM_SSA_PHI:
            analyze_phi(tc, g, bb, ins);
            break;
        case MVM_OP_set:
            copy_facts(tc, g, ins->operands[0], ins->operands[1]);
            break;
        case MVM_OP_null:
            optimize_null(tc, g, bb, ins);
            break;
        case MVM_OP_isnull:
            optimize_isnull(tc, g, bb, ins);
            break;
        case MVM_OP_hllbool:
        case MVM_OP_hllboolfor:
            optimize_hllbool(tc, g, ins);
            break;
        case MVM_OP_if_i:
        case MVM_OP_unless_i:
        case MVM_OP_if_n:
        case MVM_OP_unless_n:
        case MVM_OP_ifnonnull:
            optimize_iffy(tc, g, ins, bb);
            break;
        case MVM_OP_not_i:
            optimize_not_i(tc, g, ins, bb);
            break;
        case MVM_OP_band_i:
        case MVM_OP_bor_i:
        case MVM_OP_bxor_i:
            optimize_bitwise_int_math(tc, g, ins, bb);
            break;
        case MVM_OP_coerce_ui:
        case MVM_OP_coerce_iu:
            optimize_signedness_coerce(tc, g, bb, ins);
            break;
        case MVM_OP_coerce_in:
            optimize_coerce(tc, g, bb, ins);
            break;
        case MVM_OP_islist:
        case MVM_OP_ishash:
        case MVM_OP_isint:
        case MVM_OP_isnum:
        case MVM_OP_isstr:
            optimize_is_reprid(tc, g, ins);
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
        case MVM_OP_eqaddr:
            optimize_eqaddr(tc, g, ins);
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
        case MVM_OP_push_i:
        case MVM_OP_push_n:
        case MVM_OP_push_s:
        case MVM_OP_push_o:
        case MVM_OP_bindkey_i:
        case MVM_OP_bindkey_n:
        case MVM_OP_bindkey_s:
        case MVM_OP_bindkey_o:
        case MVM_OP_bindpos_i:
        case MVM_OP_bindpos_u:
        case MVM_OP_bindpos_n:
        case MVM_OP_bindpos_s:
        case MVM_OP_bindpos_o:
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
        case MVM_OP_pop_i:
        case MVM_OP_pop_n:
        case MVM_OP_pop_s:
        case MVM_OP_pop_o:
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
        case MVM_OP_create:
            optimize_repr_op(tc, g, bb, ins, 1);
            break;
        case MVM_OP_box_i:
        case MVM_OP_box_n:
        case MVM_OP_box_s:
        case MVM_OP_box_u: {
            /* We'll lower these in a later pass, but we should preemptively
             * use the facts on the box type. */
            MVMSpeshFacts *type_facts = MVM_spesh_get_facts(tc, g, ins->operands[2]);
            if (type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE)
                MVM_spesh_use_facts(tc, g, type_facts);
            break;
        }
        case MVM_OP_unbox_i:
        case MVM_OP_unbox_n:
        case MVM_OP_unbox_s:
        case MVM_OP_unbox_u:
        case MVM_OP_decont_i:
        case MVM_OP_decont_n:
        case MVM_OP_decont_s:
        case MVM_OP_decont_u: {
            /* We'll lower these in a later pass, but we should preemptively
             * use the facts on the box type. */
            MVMSpeshFacts *type_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
            if (type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE)
                MVM_spesh_use_facts(tc, g, type_facts);
            break;
        }
        case MVM_OP_ne_s:
        case MVM_OP_eq_s:
            optimize_string_equality(tc, g, bb, ins);
            break;
        case MVM_OP_newexception:
        case MVM_OP_bindexmessage:
        case MVM_OP_bindexpayload:
        case MVM_OP_getexmessage:
        case MVM_OP_getexpayload:
            optimize_exception_ops(tc, g, bb, ins);
            break;
        case MVM_OP_decont:
            optimize_decont(tc, g, bb, ins);
            break;
        case MVM_OP_assertparamcheck:
            optimize_assertparamcheck(tc, g, bb, ins);
            break;
        case MVM_OP_bindcomplete:
            optimize_bindcomplete(tc, g, bb, ins);
            break;
        case MVM_OP_getlex:
            optimize_getlex(tc, g, ins);
            break;
        case MVM_OP_bindlex:
            optimize_bindlex(tc, g, ins);
            break;
        case MVM_OP_getlex_no:
            /* Use non-logging variant. */
            ins->info = MVM_op_get_op(MVM_OP_sp_getlex_no);
            break;
        case MVM_OP_getlexstatic_o:
            optimize_getlexstatic(tc, g, bb, ins);
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
        case MVM_OP_wval:
        case MVM_OP_wval_wide:
            optimize_wval(tc, g, ins);
            break;
        case MVM_OP_add_I:
        case MVM_OP_sub_I:
        case MVM_OP_mul_I:
            optimize_bigint_binary_op(tc, g, bb, ins);
            break;
        case MVM_OP_bool_I:
            optimize_bigint_bool_op(tc, g, bb, ins);
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
        case MVM_OP_dispatch_v:
        case MVM_OP_dispatch_o:
        case MVM_OP_dispatch_n:
        case MVM_OP_dispatch_s:
        case MVM_OP_dispatch_i:
        case MVM_OP_dispatch_u:
            MVM_spesh_disp_optimize(tc, g, bb, p, ins, &next_ins);
            break;
        case MVM_OP_sp_guard:
        case MVM_OP_sp_guardconc:
        case MVM_OP_sp_guardtype:
        case MVM_OP_sp_guardobj:
        case MVM_OP_sp_guardjustconc:
        case MVM_OP_sp_guardjusttype:
        case MVM_OP_sp_guardnonzero:
            optimize_guard(tc, g, bb, ins);
            break;
        case MVM_OP_sp_runbytecode_v:
        case MVM_OP_sp_runbytecode_o:
        case MVM_OP_sp_runbytecode_i:
        case MVM_OP_sp_runbytecode_u:
        case MVM_OP_sp_runbytecode_n:
        case MVM_OP_sp_runbytecode_s: {
            MVMSpeshAnn *temps_ann = ins->annotations;
            if (temps_ann && temps_ann->type == MVM_SPESH_ANN_DELAYED_TEMPS)
                ins->annotations = temps_ann->next;
            else
                temps_ann = NULL;
            optimize_runbytecode(tc, g, bb, ins, p);
            if (temps_ann) {
                MVMint32 i = 0;
                while (temps_ann->data.temps_to_release[i].lit_i64 != -1)
                    MVM_spesh_manipulate_release_temp_reg(tc, g,
                        temps_ann->data.temps_to_release[i++]);
                MVM_free(temps_ann->data.temps_to_release);
            }
            break;
        }
        case MVM_OP_sp_runnativecall_v:
        case MVM_OP_sp_runnativecall_o:
        case MVM_OP_sp_runnativecall_i:
        case MVM_OP_sp_runnativecall_u:
        case MVM_OP_sp_runnativecall_n:
        case MVM_OP_sp_runnativecall_s:
        case MVM_OP_sp_runcfunc_v:
        case MVM_OP_sp_runcfunc_o:
        case MVM_OP_sp_runcfunc_i:
        case MVM_OP_sp_runcfunc_u:
        case MVM_OP_sp_runcfunc_n:
        case MVM_OP_sp_runcfunc_s: {
            MVMSpeshAnn *temps_ann = ins->annotations;
            if (temps_ann && temps_ann->type == MVM_SPESH_ANN_DELAYED_TEMPS)
                ins->annotations = temps_ann->next;
            else
                temps_ann = NULL;
            if (temps_ann) {
                MVMint32 i = 0;
                while (temps_ann->data.temps_to_release[i].lit_i64 != -1)
                    MVM_spesh_manipulate_release_temp_reg(tc, g,
                        temps_ann->data.temps_to_release[i++]);
                MVM_free(temps_ann->data.temps_to_release);
            }
            break;
        }
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
        ins = next_ins;
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
    for (; i < bb->num_children; i++) {
        optimize_bb(tc, g, bb->children[i], p);
    }
}

/* The post-inline optimization pass tries to do various simplifications that
 * are at their most valuable when we can see between inline boundaries. For
 * example, a control exception throwing thing may have been inlined into the
 * loop block, so we may rewrite it into a goto. We also look for box/unbox
 * pairings and see if we can eliminate those. Many redundant `set` instructions
 * produced in the first pass can now be productively eliminated too.
 *
 * Box instructions may be linked to their use sites by an eliminatable `set`
 * chain, so we keep track of them and check if they are used by the end of
 * the pass. If they are unused, we can delete them. If they are used, then
 * we can try to lower them.
 */
typedef struct {
    MVMSpeshBB *bb;
    MVMSpeshIns *ins;
} SeenBox;
typedef struct {
    MVM_VECTOR_DECL(SeenBox *, seen_box_ins);
} PostInlinePassState;

/* Optimization turns many things into simple set instructions, which we can
 * often further eliminate; others may become unrequired due to eliminated
 * branches, and some may be from sub-optimizal original code. */
static MVMSpeshBB * find_bb_with_instruction_linearly_after(MVMThreadContext *tc, MVMSpeshGraph *g,
                                                            MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshBB *cur_bb = bb;
    while (cur_bb) {
        MVMSpeshIns *cur_ins = cur_bb->first_ins;
        while (cur_ins) {
            if (cur_ins == ins)
                return cur_bb;
            cur_ins = cur_ins->next;
        }
        cur_bb = cur_bb->linear_next;
    }
    return NULL;
}
static MVMuint32 conflict_free(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
        MVMSpeshIns *from, MVMSpeshIns *to, MVMuint16 reg, MVMuint16 allow_reads) {
    /* A conflict over reg exists if either from and to are a non-linear BB
     * sequence or if another version of reg is written between from and to. */
    MVMSpeshBB *start_bb = find_bb_with_instruction_linearly_after(tc, g, bb, to);
    MVMSpeshBB *cur_bb = start_bb;
    while (cur_bb) {
        MVMSpeshIns *check = cur_bb == start_bb ? to->prev : cur_bb->last_ins;
        while (check) {
            MVMuint32 i;

            /* If we found the instruction, no conflict. */
            if (check == from)
                return 1;

            /* Make sure there's no conflicting register use. */
            for (i = 0; i < check->info->num_operands; i++) {
                MVMuint16 rw_mode = check->info->operands[i] & MVM_operand_rw_mask;
                if (rw_mode == MVM_operand_write_reg || (!allow_reads && rw_mode == MVM_operand_read_reg))
                    if (check->operands[i].reg.orig == reg)
                        return 0;
            }

            check = check->prev;
        }

        if (cur_bb->num_pred == 1)
            cur_bb = cur_bb->pred[0];
        else
            return 0;
    }
    return 0;
}
static void try_eliminate_set(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                              MVMSpeshIns *ins) {
    /* If this set is the only user of its second operand, we might be able to
     * change the writing instruction to just write the target of the set. */
    if (MVM_spesh_usages_used_once(tc, g, ins->operands[1])) {
        MVMSpeshFacts *source_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
        MVMSpeshIns *writer = source_facts->writer;
        if (!MVM_spesh_is_inc_dec_op(writer->info->opcode) && writer->info->opcode != MVM_SSA_PHI) {
            /* Instruction is OK. Check there is no register use conflict. */
            if (conflict_free(tc, g, bb, writer, ins, ins->operands[0].reg.orig, 0)) {
                /* All is well. Update writer and delete set. */
                MVMSpeshOperand new_target = ins->operands[0];
                MVMSpeshFacts *new_target_facts = MVM_spesh_get_facts(tc, g, new_target);
                MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
                writer->operands[0] = new_target;
                new_target_facts->writer = writer;
                new_target_facts->dead_writer = 0;
                new_target_facts->flags = source_facts->flags;
                new_target_facts->value = source_facts->value;
                new_target_facts->type = source_facts->type;
                new_target_facts->decont_type = source_facts->decont_type;
                return;
            }
        }
    }

    /* If that didn't work out, it may be that the register we write only has
     * a single user, and we can propagate the source value to be used at
     * that usage site. */
    if (MVM_spesh_usages_used_once(tc, g, ins->operands[0])) {
        MVMSpeshIns *user = MVM_spesh_get_facts(tc, g, ins->operands[0])->usage.users->user;
        if (!MVM_spesh_is_inc_dec_op(user->info->opcode) && user->info->opcode != MVM_SSA_PHI) {
            /* Instruction is OK. Check there is no register use conflict. */
            if (conflict_free(tc, g, bb, ins, user, ins->operands[1].reg.orig, 1)) {
                /* It will work. Find reading operand and update it. */
                MVMuint32 i;
                for (i = 1; i < user->info->num_operands; i++) {
                    if ((user->info->operands[i] & MVM_operand_rw_mask)
                            && user->operands[i].reg.orig == ins->operands[0].reg.orig
                            && user->operands[i].reg.i == ins->operands[0].reg.i) {
                        /* Found operand. Update reader to use what the set
                         * instruction reads, then delete the set. */
                        MVM_spesh_usages_delete_by_reg(tc, g, user->operands[i], user);
                        user->operands[i] = ins->operands[1];
                        MVM_spesh_usages_add_by_reg(tc, g, user->operands[i], user);
                        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
                        return;
                    }
                }
            }
        }
    }

    copy_facts(tc, g, ins->operands[0], ins->operands[1]);
}

/* Find box_* that are used by a matching unbox_*. This can happen in part due
 * to imperfect code-gen, but also because the box is in an inlinee and the
 * unbox on the outside, or vice versa. */
static void try_eliminate_one_box_unbox(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                         MVMSpeshIns *box_ins, MVMSpeshIns *unbox_ins) {
    if (conflict_free(tc, g, bb, box_ins, unbox_ins, box_ins->operands[1].reg.orig, 1)) {
        /* Make unbox instruction no longer use the boxed value. */
        for (int i = 1; i < unbox_ins->info->num_operands; i++) {
            if ((unbox_ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg)
                MVM_spesh_usages_delete_by_reg(tc, g, unbox_ins->operands[i], unbox_ins);
        }

        /* Use the unboxed version instead, rewriting to a set. */
        unbox_ins->operands[1] = box_ins->operands[1];
        MVM_spesh_turn_into_set(tc, g, unbox_ins);
        MVM_spesh_usages_add_by_reg(tc, g, unbox_ins->operands[1], unbox_ins);
    }
}
static void walk_set_looking_for_unbox(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                       MVMSpeshIns *box_ins, MVMuint16 unbox_op, MVMuint16 decont_op,
                                       MVMSpeshIns *set_ins) {
    MVMSpeshUseChainEntry *user_entry = MVM_spesh_get_facts(tc, g, set_ins->operands[0])->usage.users;
    while (user_entry) {
        MVMSpeshIns *user = user_entry->user;
        if (user->info->opcode == unbox_op || user->info->opcode == decont_op)
            try_eliminate_one_box_unbox(tc, g, bb, box_ins, user);
        else if (user->info->opcode == MVM_OP_set)
            walk_set_looking_for_unbox(tc, g, bb, box_ins, unbox_op, decont_op, user);
        user_entry = user_entry->next;
    }
}
static void try_eliminate_box_unbox_pair(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                         MVMSpeshIns *ins, MVMuint16 unbox_op, MVMuint16 decont_op,
                                         PostInlinePassState *pips) {
    MVMSpeshUseChainEntry *user_entry = MVM_spesh_get_facts(tc, g, ins->operands[0])->usage.users;
    while (user_entry) {
        MVMSpeshIns *user = user_entry->user;
        if (user->info->opcode == unbox_op || user->info->opcode == decont_op)
            try_eliminate_one_box_unbox(tc, g, bb, ins, user);
        else if (user->info->opcode == MVM_OP_set)
            walk_set_looking_for_unbox(tc, g, bb, ins, unbox_op, decont_op, user);
        user_entry = user_entry->next;
    }
    if (MVM_spesh_usages_is_used(tc, g, ins->operands[0])) {
        SeenBox *sb = MVM_malloc(sizeof(SeenBox));
        sb->bb = bb;
        sb->ins = ins;
        MVM_VECTOR_PUSH(pips->seen_box_ins, sb);
    }
    else {
        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
    }
}
static void walk_set_looking_for_unbool(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                       MVMSpeshIns *box_ins, MVMSpeshIns *set_ins) {
    MVMSpeshUseChainEntry *user_entry = MVM_spesh_get_facts(tc, g, set_ins->operands[0])->usage.users;
    while (user_entry) {
        MVMSpeshIns *user = user_entry->user;
        const MVMOpInfo *opinfo = user->info;
        MVMuint16 opcode = opinfo->opcode;

        if (opcode == MVM_OP_sp_runcfunc_i) {
            MVMSpeshFacts *dispatch_facts = MVM_spesh_get_facts(tc, g, user->operands[1]);
            if (is_syscall(tc, dispatch_facts, MVM_disp_syscall_boolify_boxed_int_impl))
                try_eliminate_one_box_unbox(tc, g, bb, box_ins, user);
        }
        else if (opcode == MVM_OP_set || (opcode == MVM_SSA_PHI && opinfo->num_operands == 2)) {
            walk_set_looking_for_unbool(tc, g, bb, box_ins, user);
        }
        user_entry = user_entry->next;
    }
}
static void try_eliminate_bool_unbool_pair(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                         MVMSpeshIns *ins, PostInlinePassState *pips) {
    MVMSpeshUseChainEntry *user_entry = MVM_spesh_get_facts(tc, g, ins->operands[0])->usage.users;
    while (user_entry) {
        MVMSpeshIns *user = user_entry->user;
        const MVMOpInfo *opinfo = user->info;
        MVMuint16 opcode = opinfo->opcode;

        if (opcode == MVM_OP_sp_runcfunc_i) {
            MVMSpeshFacts *dispatch_facts = MVM_spesh_get_facts(tc, g, user->operands[1]);
            if (is_syscall(tc, dispatch_facts, MVM_disp_syscall_boolify_boxed_int_impl))
                try_eliminate_one_box_unbox(tc, g, bb, ins, user);
            else
                return;
        }
        else if (opcode == MVM_OP_set || (opcode == MVM_SSA_PHI && opinfo->num_operands == 2)) {
            walk_set_looking_for_unbool(tc, g, bb, ins, user);
        }
        user_entry = user_entry->next;
    }
    if (!MVM_spesh_usages_is_used(tc, g, ins->operands[0]))
        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
}


static void post_inline_visit_bb(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                 PostInlinePassState *pips) {
    MVMint32 i;

    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        MVMSpeshIns *next = ins->next;
        switch (ins->info->opcode) {
            case MVM_OP_set:
                try_eliminate_set(tc, g, bb, ins);
                break;
            case MVM_OP_box_i:
                try_eliminate_box_unbox_pair(tc, g, bb, ins, MVM_OP_unbox_i, MVM_OP_decont_i, pips);
                break;
            case MVM_OP_box_n:
                try_eliminate_box_unbox_pair(tc, g, bb, ins, MVM_OP_unbox_n, MVM_OP_decont_n, pips);
                break;
            case MVM_OP_box_s:
                try_eliminate_box_unbox_pair(tc, g, bb, ins, MVM_OP_unbox_s, MVM_OP_decont_s, pips);
                break;
            case MVM_OP_box_u:
                try_eliminate_box_unbox_pair(tc, g, bb, ins, MVM_OP_unbox_u, MVM_OP_decont_u, pips);
                break;
            case MVM_OP_hllbool:
                try_eliminate_bool_unbool_pair(tc, g, bb, ins, pips);
                break;
            case MVM_OP_unbox_i:
            case MVM_OP_unbox_n:
            case MVM_OP_unbox_s:
            case MVM_OP_unbox_u:
            case MVM_OP_decont_i:
            case MVM_OP_decont_n:
            case MVM_OP_decont_s:
            case MVM_OP_decont_u: {
                /* Unoptimized unbox or possible decont that would be an unbox.
                 * We might be able to lower them. (The dominance tree structure
                 * means that box/unbox pairs we could otherwise lower will have
                 * already been lowered.) */
                MVMSpeshFacts *type_facts = MVM_spesh_get_facts(tc, g, ins->operands[1]);
                if ((type_facts-> flags & MVM_SPESH_FACT_KNOWN_TYPE) && REPR(type_facts->type)->spesh)
                    REPR(type_facts->type)->spesh(tc, STABLE(type_facts->type), g, bb, ins);
                break;
            }
            case MVM_OP_sp_getspeshslot:
                /* Sometimes we emit two getspeshslots in a row that write into the
                 * exact same register. That's clearly wasteful and we can save a
                 * tiny shred of code size here. */
                if (ins->prev && ins->prev->info->opcode == ins->info->opcode &&
                        ins->operands[0].reg.orig == ins->prev->operands[0].reg.orig)
                    MVM_spesh_manipulate_delete_ins(tc, g, bb, ins->prev);
                break;
            case MVM_OP_coerce_ui:
            case MVM_OP_coerce_iu:
                optimize_signedness_coerce(tc, g, bb, ins);
                break;
            case MVM_OP_band_i:
            case MVM_OP_bor_i:
            case MVM_OP_bxor_i:
                optimize_bitwise_int_math(tc, g, ins, bb);
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
        post_inline_visit_bb(tc, g, bb->children[i], pips);
}
static void post_inline_pass(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb) {
    MVMuint32 i;

    /* Walk the basic blocks for the second pass. */
    PostInlinePassState pips;
    MVM_VECTOR_INIT(pips.seen_box_ins, 0);
    post_inline_visit_bb(tc, g, g->entry, &pips);

    /* Walk through any processed box instructions. */
    for (i = 0; i < MVM_VECTOR_ELEMS(pips.seen_box_ins); i++) {
        SeenBox *sb = pips.seen_box_ins[i];
        if (MVM_spesh_usages_is_used(tc, g, sb->ins->operands[0])) {
            /* Try to lower the box instruction. */
            MVMSpeshFacts *type_facts = MVM_spesh_get_facts(tc, g, sb->ins->operands[2]);
            if ((type_facts-> flags & MVM_SPESH_FACT_KNOWN_TYPE) && REPR(type_facts->type)->spesh)
                REPR(type_facts->type)->spesh(tc, STABLE(type_facts->type), g, sb->bb, sb->ins);
        }
        else {
            /* Box instruction became unused; delete. */
            MVM_spesh_manipulate_delete_ins(tc, g, sb->bb, sb->ins);
        }
        MVM_free(sb);
    }

    MVM_VECTOR_DESTROY(pips.seen_box_ins);
}

/* Goes through the various log-based guard instructions and removes any that
 * are not being used. */
static void eliminate_unused_log_guards(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMint32 i;
    for (i = 0; i < g->num_log_guards; i++)
        if (!g->log_guards[i].used) {
            MVM_spesh_turn_into_set(tc, g, g->log_guards[i].ins);
        }
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
    MVMuint32 orig_bbs = g->num_bbs;
    if (!bb || !bb->linear_next) return; /* looks like there's only a single bb anyway */
    bb = bb->linear_next;

    while (bb->linear_next) {
        if (
               bb->num_succ == 1
            && bb->succ[0] == bb->linear_next
            && bb->linear_next->num_pred == 1
            && !bb->inlined
            && !bb->linear_next->inlined
            && (!bb->last_ins || !bb->linear_next->first_ins || !MVM_spesh_graph_ins_ends_bb(tc, bb->last_ins->info))
        ) {
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
                int i, j = 0, p;
                for (i = 0; i < bb->num_succ; i++)
                    if (bb->succ[i] != bb->linear_next)
                        succ[j++] = bb->succ[i];
                for (i = 0; i < bb->linear_next->num_succ; i++) {
                    succ[j++] = bb->linear_next->succ[i];
                    /* fixup the to be removed bb's succs' pred */
                    for (p = 0; p < bb->linear_next->succ[i]->num_pred; p++)
                        if (bb->linear_next->succ[i]->pred[p] == bb->linear_next)
                            bb->linear_next->succ[i]->pred[p] = bb;
                }
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
#if MVM_SPESH_CHECK_DU
    MVM_spesh_usages_check(tc, g);
#endif

    /* Clear up the graph after this initial pass, recomputing the dominance
     * tree (which in turn updates the preds). */
    MVM_spesh_eliminate_dead_bbs(tc, g, 1);
    MVM_spesh_graph_recompute_dominance(tc, g);
    eliminate_unused_log_guards(tc, g);
    eliminate_pointless_gotos(tc, g);
    MVM_spesh_usages_remove_unused_deopt(tc, g);
    MVM_spesh_eliminate_dead_ins(tc, g);

    merge_bbs(tc, g);

    /* Perform partial escape analysis at this point, which may make more
     * information available, or give more `set` instructions for the `set`
     * elimination in the post-inline pass to get rid of. */
    if (tc->instance->spesh_pea_enabled)
        MVM_spesh_pea(tc, g);

    /* Make a post-inline pass through the graph doing things that are better
     * done after inlinings have taken place. Note that these things must not
     * add new fact dependencies. Do a final dead instruction elimination pass
     * to clean up after it, and also delete dead BBs thanks to any control
     * flow opts. */
    post_inline_pass(tc, g, g->entry);
    MVM_spesh_eliminate_dead_ins(tc, g);
    MVM_spesh_eliminate_dead_bbs(tc, g, 1);

#if MVM_SPESH_CHECK_DU
    MVM_spesh_usages_check(tc, g);
#endif
}
