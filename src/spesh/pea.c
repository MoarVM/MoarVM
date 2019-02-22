#include "moar.h"

/* Debug logging of EA. */
#define PEA_LOG 0
static void pea_log(char *fmt, ...) {
#if PEA_LOG
    va_list args;
    fprintf(stderr, "PEA: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
#endif
}

/* A transformation that we want to perform. */
#define TRANSFORM_DELETE_FASTCREATE 0
#define TRANSFORM_GETATTR_TO_SET    1
#define TRANSFORM_BINDATTR_TO_SET   2
#define TRANSFORM_DELETE_SET        3
#define TRANSFORM_GUARD_TO_SET      4
#define TRANSFORM_ADD_DEOPT_POINT   5
#define TRANSFORM_ADD_DEOPT_USAGE   6
#define TRANSFORM_PROF_ALLOCATED    7
typedef struct {
    /* The allocation that this transform relates to eliminating. */
    MVMSpeshPEAAllocation *allocation;

    /* What kind of transform do we need to do? */
    MVMuint16 transform;

    /* Data per kind of transformation. */
    union {
        struct {
            MVMSpeshIns *ins;
            MVMuint16 hypothetical_reg_idx;
        } attr;
        struct {
            MVMSpeshIns *ins;
            MVMSTable *st;
        } fastcreate;
        struct {
            MVMSpeshIns *ins;
        } set;
        struct {
            MVMSpeshIns *ins;
        } guard;
        struct {
            MVMint32 deopt_point_idx;
            MVMuint16 target_reg;
        } dp;
        struct {
            MVMint32 deopt_point_idx;
            MVMuint16 hypothetical_reg_idx;
        } du;
        struct {
            MVMSpeshIns *ins;
        } prof;
    };
} Transformation;

/* State held per basic block. */
typedef struct {
    MVM_VECTOR_DECL(Transformation *, transformations);
} BBState;

/* Shadow facts are used to track hypothetical extra information about an SSA
 * value. We hold them separately from the real facts, since they may not end
 * up applying (e.g. in the case of a loop where we have to iterate to a fixed
 * point). They can be indexed in two ways: by a hypothetical register ID or
 * by a concrete register ID (the former used for registers that we will only
 * create if we really do scalar replacement). */
typedef struct {
    MVMuint16 is_hypothetical;
    MVMuint16 hypothetical_reg_idx;
    MVMuint16 concrete_orig;
    MVMuint16 concrete_i;
    MVMSpeshFacts facts;
} ShadowFact;

/* A tracked register is one that is either the target of an allocation or
 * aliasing an allocation. We map it to the allocation tracking info. */
typedef struct {
    /* The register that is tracked. */
    MVMSpeshOperand reg;

    /* The allocation that is tracked there. */
    MVMSpeshPEAAllocation *allocation;
} TrackedRegister;

/* State we hold during the entire partial escape analysis process. */
typedef struct {
    /* The latest temporary register index. We use these indices before we
     * really allocate temporary registers. */
    MVMuint16 latest_hypothetical_reg_idx;

    /* The actual temporary registers allocated, matching the hypotheticals
     * above. */
    MVMuint16 *attr_regs;

    /* State held per basic block. */
    BBState *bb_states;

    /* Shadow facts. */
    MVM_VECTOR_DECL(ShadowFact, shadow_facts);

    /* Tracked registers. */
    MVM_VECTOR_DECL(TrackedRegister, tracked_registers);
} GraphState;

/* Turns a flattened-in STable into a register type to allocate, if possible.
 * Should it not be possible, returns a negative value. If passed NULL (which
 * indicates a reference type), then returns MVM_reg_obj. */
MVMint32 flattened_type_to_register_kind(MVMThreadContext *tc, MVMSTable *st) {
    if (st) {
        const MVMStorageSpec *ss = st->REPR->get_storage_spec(tc, st);
        switch (ss->boxed_primitive) {
            case MVM_STORAGE_SPEC_BP_INT:
                if (ss->bits == 64 && !ss->is_unsigned)
                    return MVM_reg_int64;
                break;
            case MVM_STORAGE_SPEC_BP_NUM:
                if (ss->bits == 64)
                    return MVM_reg_num64;
                break;
            case MVM_STORAGE_SPEC_BP_STR:
                return MVM_reg_str;
        }
        return -1;
    }
    else {
        return MVM_reg_obj;
    }
}

/* Gets, allocating if needed, the deopt materialization info index of a
 * particular tracked object. */
static MVMuint16 get_deopt_materialization_info(MVMThreadContext *tc, MVMSpeshGraph *g,
                                                GraphState *gs, MVMSpeshPEAAllocation *alloc) {
    if (alloc->has_deopt_materialization_idx) {
        return alloc->deopt_materialization_idx;
    }
    else {
        MVMSpeshPEAMaterializeInfo mi;

        /* Build up information about registers containing attribute data. */
        MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)alloc->type->st->REPR_data;
        MVMuint32 num_attrs = repr_data->num_attributes;
        MVMuint16 *attr_regs;
        if (num_attrs > 0) {
            MVMuint32 i;
            attr_regs = MVM_malloc(num_attrs * sizeof(MVMuint16));
            for (i = 0; i < num_attrs; i++)
                attr_regs[i] = gs->attr_regs[alloc->hypothetical_attr_reg_idxs[i]];
        }
        else {
            attr_regs = NULL;
        }

        /* Set up and add materialization info. */
        mi.stable_sslot = MVM_spesh_add_spesh_slot_try_reuse(tc, g, (MVMCollectable *)alloc->type->st);
        mi.num_attr_regs = num_attrs;
        mi.attr_regs = attr_regs;
        alloc->deopt_materialization_idx = MVM_VECTOR_ELEMS(g->deopt_pea.materialize_info);
        alloc->has_deopt_materialization_idx = 1;
        MVM_VECTOR_PUSH(g->deopt_pea.materialize_info, mi);

        return alloc->deopt_materialization_idx;
    }
}

/* Apply a transformation to the graph. */
static void apply_transform(MVMThreadContext *tc, MVMSpeshGraph *g, GraphState *gs,
        MVMSpeshBB *bb, Transformation *t) {
    /* Don't apply if we discovered this allocation wasn't possible to scalar
     * replace. */
    if (t->allocation->irreplaceable)
        return;

    /* Otherwise, go by the type of transform. */
    switch (t->transform) {
        case TRANSFORM_DELETE_FASTCREATE: {
            MVMSTable *st = t->fastcreate.st;
            MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
            MVMSpeshPEAAllocation *alloc = t->allocation;
            MVMuint32 i;
            for (i = 0; i < repr_data->num_attributes; i++) {
                MVMuint32 idx = alloc->hypothetical_attr_reg_idxs[i];
                gs->attr_regs[idx] = MVM_spesh_manipulate_get_unique_reg(tc, g,
                    flattened_type_to_register_kind(tc, repr_data->flattened_stables[i]));
            }
            pea_log("OPT: eliminated an allocation of %s into r%d(%d)",
                    st->debug_name, t->fastcreate.ins->operands[0].reg.orig,
                    t->fastcreate.ins->operands[0].reg.i);
            MVM_spesh_manipulate_delete_ins(tc, g, bb, t->fastcreate.ins);
            break;
        }
        case TRANSFORM_GETATTR_TO_SET: {
            MVMSpeshIns *ins = t->attr.ins;
            MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[1], ins);
            ins->info = MVM_op_get_op(MVM_OP_set);
            ins->operands[1].reg.orig = gs->attr_regs[t->attr.hypothetical_reg_idx];
            ins->operands[1].reg.i = MVM_spesh_manipulate_get_current_version(tc, g,
                ins->operands[1].reg.orig);
            MVM_spesh_usages_add_by_reg(tc, g, ins->operands[1], ins);
            MVM_spesh_graph_add_comment(tc, g, ins, "read of scalar-replaced attribute");
            break;
        }
        case TRANSFORM_BINDATTR_TO_SET: {
            MVMSpeshIns *ins = t->attr.ins;
            MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[0], ins);
            ins->info = MVM_op_get_op(MVM_OP_set);
            ins->operands[0].reg.orig = gs->attr_regs[t->attr.hypothetical_reg_idx];
            /* This new_version handling assumes linear code with no flow
             * control. We need to revisit it later, probably by not caring
             * about versions here and then placing versions and PHIs as
             * needed after this operation. However, when we'll also have
             * to update usages at that point too. */
            ins->operands[0] = MVM_spesh_manipulate_new_version(tc, g,
                ins->operands[0].reg.orig);
            ins->operands[1] = ins->operands[2];
            MVM_spesh_get_facts(tc, g, ins->operands[0])->writer = ins;
            MVM_spesh_graph_add_comment(tc, g, ins, "write of scalar-replaced attribute");
            break;
        }
        case TRANSFORM_DELETE_SET:
            MVM_spesh_manipulate_delete_ins(tc, g, bb, t->set.ins);
            break;
        case TRANSFORM_GUARD_TO_SET: {
            MVMSpeshIns *ins = t->guard.ins;
            ins->info = MVM_op_get_op(MVM_OP_set);
            MVM_spesh_graph_add_comment(tc, g, ins, "guard eliminated by scalar replacement");
            pea_log("OPT: eliminated a guard");
            break;
        }
        case TRANSFORM_ADD_DEOPT_POINT: {
            MVMSpeshPEADeoptPoint dp;
            dp.deopt_point_idx = t->dp.deopt_point_idx;
            dp.materialize_info_idx = get_deopt_materialization_info(tc, g, gs, t->allocation);
            dp.target_reg = t->dp.target_reg;
            MVM_VECTOR_PUSH(g->deopt_pea.deopt_point, dp);
            break;
        }
        case TRANSFORM_ADD_DEOPT_USAGE: {
            MVMSpeshOperand used;
            used.reg.orig = gs->attr_regs[t->du.hypothetical_reg_idx];
            used.reg.i = MVM_spesh_manipulate_get_current_version(tc, g, used.reg.orig);
            MVM_spesh_usages_add_deopt_usage_by_reg(tc, g, used, t->du.deopt_point_idx);
            break;
        }
        case TRANSFORM_PROF_ALLOCATED: {
            MVMSpeshIns *ins = t->prof.ins;
            MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[0], ins);
            ins->info = MVM_op_get_op(MVM_OP_prof_replaced);
            ins->operands[0].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                    (MVMCollectable *)STABLE(t->allocation->type));
            break;
        }
        default:
            MVM_oops(tc, "Unimplemented partial escape analysis transform");
    }
}

/* Adds a register to the set of those being tracked by the escape algorithm. */
static void add_tracked_register(MVMThreadContext *tc, GraphState *gs, MVMSpeshOperand reg,
                                 MVMSpeshPEAAllocation *alloc) {
    TrackedRegister tr;
    tr.reg = reg;
    tr.allocation = alloc;
    MVM_VECTOR_PUSH(gs->tracked_registers, tr);
}

/* Sees if this is something we can potentially avoid really allocating. If
 * it is, sets up the allocation tracking state that we need. */
static MVMSpeshPEAAllocation * try_track_allocation(MVMThreadContext *tc, MVMSpeshGraph *g,
        GraphState *gs, MVMSpeshIns *alloc_ins, MVMSTable *st) {
    if (st->REPR->ID == MVM_REPR_ID_P6opaque) {
        MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
        MVMSpeshPEAAllocation *alloc = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshPEAAllocation));
        MVMuint32 i;
        alloc->allocator = alloc_ins;
        alloc->type = st->WHAT;
        alloc->hypothetical_attr_reg_idxs = MVM_spesh_alloc(tc, g,
                repr_data->num_attributes * sizeof(MVMuint16));
        for (i = 0; i < repr_data->num_attributes; i++) {
            /* Make sure it's an attribute type we know how to handle. */
            if (flattened_type_to_register_kind(tc, repr_data->flattened_stables[i]) < 0)
                return NULL;

            /* Pick an index that will later come to refer to an allocated
             * register if we apply transforms. */
            alloc->hypothetical_attr_reg_idxs[i] = gs->latest_hypothetical_reg_idx++;
        }
        add_tracked_register(tc, gs, alloc_ins->operands[0], alloc);
        return alloc;
    }
    return NULL;
}

/* Add a transform to hypothetically be applied. */
static void add_transform_for_bb(MVMThreadContext *tc, GraphState *gs, MVMSpeshBB *bb,
        Transformation *tran) {
    MVM_VECTOR_PUSH(gs->bb_states[bb->idx].transformations, tran);
}

/* Gets the shadow facts for a register, or returns NULL if there aren't
 * any. The _h form takes a hypothetical register ID, the _c form a
 * concrete register.*/
static MVMSpeshFacts * get_shadow_facts_h(MVMThreadContext *tc, GraphState *gs, MVMuint16 idx) {
    MVMint32 i;
    for (i = 0; i < gs->shadow_facts_num; i++) {
        ShadowFact *sf = &(gs->shadow_facts[i]);
        if (sf->is_hypothetical && sf->hypothetical_reg_idx == idx)
            return &(sf->facts);
    }
    return NULL;
}
static MVMSpeshFacts * get_shadow_facts_c(MVMThreadContext *tc, GraphState *gs, MVMSpeshOperand o) {
    MVMint32 i;
    for (i = 0; i < gs->shadow_facts_num; i++) {
        ShadowFact *sf = &(gs->shadow_facts[i]);
        if (!sf->is_hypothetical && sf->concrete_orig == o.reg.orig &&
                sf->concrete_i == o.reg.i)
            return &(sf->facts);
    }
    return NULL;
}

/* Shadow facts are facts that we hold about a value based upon the new
 * information we have available thanks to scalar replacement. This adds
 * a new one. Note that any previously held shadow facts are this point
 * may be invalidated due to reallocation. This will get recreate new
 * shadow facts if they already exist. The _h form takes a hypothetical
 * register ID, the _c form a concrete register. */
static MVMSpeshFacts * create_shadow_facts_h(MVMThreadContext *tc, GraphState *gs, MVMuint16 idx) {
    MVMSpeshFacts *facts = get_shadow_facts_h(tc, gs, idx);
    if (!facts) {
        ShadowFact sf;
        sf.is_hypothetical = 1;
        sf.hypothetical_reg_idx = idx;
        memset(&(sf.facts), 0, sizeof(MVMSpeshFacts));
        MVM_VECTOR_PUSH(gs->shadow_facts, sf);
        facts = &(gs->shadow_facts[gs->shadow_facts_num - 1].facts);
    }
    return facts;
}
static MVMSpeshFacts * create_shadow_facts_c(MVMThreadContext *tc, GraphState *gs, MVMSpeshOperand o) {
    MVMSpeshFacts *facts = get_shadow_facts_c(tc, gs, o);
    if (!facts) {
        ShadowFact sf;
        sf.is_hypothetical = 0;
        sf.concrete_orig = o.reg.orig;
        sf.concrete_i = o.reg.i;
        memset(&(sf.facts), 0, sizeof(MVMSpeshFacts));
        MVM_VECTOR_PUSH(gs->shadow_facts, sf);
        facts = &(gs->shadow_facts[gs->shadow_facts_num - 1].facts);
    }
    return facts;
}

/* Map an object offset to the register with its scalar replacement. */
static MVMuint16 attribute_offset_to_reg(MVMThreadContext *tc, MVMSpeshPEAAllocation *alloc,
        MVMint16 offset) {
    MVMuint32 idx = MVM_p6opaque_offset_to_attr_idx(tc, alloc->type, offset);
    return alloc->hypothetical_attr_reg_idxs[idx];
}

/* Check if an allocation is being tracked. */
static MVMuint32 allocation_tracked(MVMSpeshPEAAllocation *alloc) {
    return alloc && !alloc->irreplaceable;
}

/* Indicates that a real object is required; will eventually mark a point at
 * which we materialize. */
static void real_object_required(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
                                 MVMSpeshOperand o) {
    MVMSpeshFacts *target = MVM_spesh_get_facts(tc, g, o);
    /* If there's another op using it, we'd need to materialize.
     * We don't support that yet, so just mark it irreplaceable. */
    if (target->pea.allocation) {
        if (!target->pea.allocation->irreplaceable) {
            target->pea.allocation->irreplaceable = 1;
            pea_log("replacement impossible due to %s", ins->info->name);
        }
    }
}

/* Checks if any of the tracked objects are needed beyond this deopt point,
 * and adds a transform to set up that deopt info if needed. Also makes sure
 * that current versions of registers used in scalar replacement will have a
 * deopt usage added, otherwise the data we need to deopt could go missing. */
static void add_scalar_replacement_deopt_usages(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                                GraphState *gs, MVMSpeshPEAAllocation *alloc,
                                                MVMint32 deopt_idx) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)alloc->type->st->REPR_data;
    MVMuint32 i;
    for (i = 0; i < repr_data->num_attributes; i++) {
        Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
        tran->allocation = alloc;
        tran->transform = TRANSFORM_ADD_DEOPT_USAGE;
        tran->du.deopt_point_idx = deopt_idx;
        tran->du.hypothetical_reg_idx = alloc->hypothetical_attr_reg_idxs[i];
        add_transform_for_bb(tc, gs, bb, tran);
    }
}
static void add_deopt_materializations_idx(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                           GraphState *gs, MVMint32 deopt_idx,
                                           MVMint32 deopt_user_idx) {
    MVMint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(gs->tracked_registers); i++) {
        MVMSpeshFacts *tracked_facts = MVM_spesh_get_facts(tc, g, gs->tracked_registers[i].reg);
        MVMSpeshPEAAllocation *alloc = tracked_facts->pea.allocation;
        MVMSpeshDeoptUseEntry *deopt_user = tracked_facts->usage.deopt_users;
        while (deopt_user) {
            if (deopt_user->deopt_idx == deopt_user_idx) {
                Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
                tran->allocation = alloc;
                tran->transform = TRANSFORM_ADD_DEOPT_POINT;
                tran->dp.deopt_point_idx = deopt_idx;
                tran->dp.target_reg = gs->tracked_registers[i].reg.reg.orig;
                add_transform_for_bb(tc, gs, bb, tran);
                add_scalar_replacement_deopt_usages(tc, g, bb, gs, alloc, deopt_user_idx);
            }
            deopt_user = deopt_user->next;
        }
    }
}

/* Goes through the deopt indices at the specified instruction, and sees if
 * any of the tracked objects are needed beyond the deopt point. If so,
 * adds their materialization. */
static void add_deopt_materializations_ins(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                           GraphState *gs, MVMSpeshIns *deopt_ins) {
    /* Make a first pass to see if there's a SYNTH deopt index; if there is,
     * that is the one we use to do a lookup inside of the usages. */
    MVMint32 deopt_user_idx = -1;
    MVMSpeshAnn *ann = deopt_ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_DEOPT_SYNTH) {
            deopt_user_idx = ann->data.deopt_idx;
            break;
        }
        ann = ann->next;
    }

    /* Now go over the concrete indexes that will appear when we actually deopt. */
    ann = deopt_ins->annotations;
    while (ann) {
        switch (ann->type) {
            case MVM_SPESH_ANN_DEOPT_ONE_INS:
            case MVM_SPESH_ANN_DEOPT_ALL_INS:
            case MVM_SPESH_ANN_DEOPT_INLINE:
                add_deopt_materializations_idx(tc, g, bb, gs, ann->data.deopt_idx,
                        deopt_user_idx >= 0 ? deopt_user_idx : ann->data.deopt_idx);
                break;
        }
        ann = ann->next;
    }
}

/* Performs the analysis phase of partial escape anslysis, figuring out what
 * rewrites we can do on the graph to achieve scalar replacement of objects
 * and, perhaps, some guard eliminations. */
static MVMuint32 analyze(MVMThreadContext *tc, MVMSpeshGraph *g, GraphState *gs) {
    MVMSpeshBB **rpo = MVM_spesh_graph_reverse_postorder(tc, g);
    MVMuint8 *seen = MVM_calloc(g->num_bbs, 1);
    MVMuint32 found_replaceable = 0;
    MVMuint32 ins_count = 0;
    MVMuint32 i;
    for (i = 0; i < g->num_bbs; i++) {
        MVMSpeshBB *bb = rpo[i];
        MVMSpeshIns *ins = bb->first_ins;

        /* For now, we don't handle loops; bail entirely if we see one. */
        MVMuint32 j;
        for (j = 0; j < bb->num_pred; j++) {
            if (!seen[bb->pred[j]->rpo_idx]) {
                pea_log("partial escape analysis not implemented for loops");
                MVM_free(seen);
                MVM_free(rpo);
                return 0;
            }
        }

        while (ins) {
            MVMuint16 opcode = ins->info->opcode;

            /* See if this is an instruction where a deopt might take place.
             * If yes, then we first consider whether it's a guard that the
             * extra information available thanks to Scalar Replacement might
             * let us eliminate. If it *is*, then we no longer consider this a
             * deopt point, and schedule a transform of the guard into a set.
             * Also, make entries into the deopt materializations table. */
            MVMuint32 settified_guard = 0;
            if (ins->info->may_cause_deopt) {
                MVMSpeshPEAAllocation *settify_dep = NULL;
                switch (opcode) {
                    case MVM_OP_sp_guardconc: {
                        MVMSpeshFacts *hyp_facts = get_shadow_facts_c(tc, gs,
                                ins->operands[1]);
                        if (hyp_facts && (hyp_facts->flags & MVM_SPESH_FACT_CONCRETE) &&
                                (hyp_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                                hyp_facts->pea.depend_allocation) {
                            MVMSTable *wanted = (MVMSTable *)g->spesh_slots[ins->operands[2].lit_ui16];
                            settified_guard = wanted == hyp_facts->type->st;
                            settify_dep = hyp_facts->pea.depend_allocation;
                        }
                        break;
                    }
                }
                if (settified_guard) {
                    Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
                    tran->allocation = settify_dep;
                    tran->transform = TRANSFORM_GUARD_TO_SET;
                    tran->guard.ins = ins;
                    add_transform_for_bb(tc, gs, bb, tran);
                }
                add_deopt_materializations_ins(tc, g, bb, gs, ins);
            }

            /* Look for significant instructions. */
            switch (opcode) {
                case MVM_OP_sp_fastcreate: {
                    MVMSTable *st = (MVMSTable *)g->spesh_slots[ins->operands[2].lit_i16];
                    MVMSpeshPEAAllocation *alloc = try_track_allocation(tc, g, gs, ins, st);
                    if (alloc) {
                        MVMSpeshFacts *target = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                        Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
                        tran->allocation = alloc;
                        tran->transform = TRANSFORM_DELETE_FASTCREATE;
                        tran->fastcreate.ins = ins;
                        tran->fastcreate.st = st;
                        add_transform_for_bb(tc, gs, bb, tran);
                        target->pea.allocation = alloc;
                        found_replaceable = 1;
                    }
                    break;
                }
                case MVM_OP_set: {
                    /* A set instruction just aliases the tracked object; we
                     * can potentially elimiante it. */
                    MVMSpeshFacts *source = MVM_spesh_get_facts(tc, g, ins->operands[1]);
                    MVMSpeshPEAAllocation *alloc = source->pea.allocation;
                    if (allocation_tracked(alloc)) {
                        Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
                        tran->allocation = alloc;
                        tran->transform = TRANSFORM_DELETE_SET;
                        tran->set.ins = ins;
                        add_transform_for_bb(tc, gs, bb, tran);
                        MVM_spesh_get_facts(tc, g, ins->operands[0])->pea.allocation = alloc;
                        add_tracked_register(tc, gs, ins->operands[0], alloc);
                    }
                    break;
                }
                case MVM_OP_sp_bind_i64:
                case MVM_OP_sp_bind_n:
                case MVM_OP_sp_bind_s:
                case MVM_OP_sp_bind_s_nowb:
                case MVM_OP_sp_bind_o:
                case MVM_OP_sp_p6obind_i:
                case MVM_OP_sp_p6obind_n:
                case MVM_OP_sp_p6obind_s:
                case MVM_OP_sp_p6obind_o: {
                    /* Schedule transform of bind into an attribute of a
                     * tracked object into a set. */
                    MVMSpeshFacts *target = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                    MVMSpeshPEAAllocation *alloc = target->pea.allocation;
                    if (allocation_tracked(alloc)) {
                        MVMint32 is_p6o_op = opcode == MVM_OP_sp_p6obind_i ||
                            opcode == MVM_OP_sp_p6obind_n ||
                            opcode == MVM_OP_sp_p6obind_s ||
                            opcode == MVM_OP_sp_p6obind_o;
                        MVMuint16 hypothetical_reg = attribute_offset_to_reg(tc, alloc,
                                is_p6o_op
                                    ? ins->operands[1].lit_i16
                                    : ins->operands[1].lit_i16 - sizeof(MVMObject));
                        Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
                        tran->allocation = alloc;
                        tran->transform = TRANSFORM_BINDATTR_TO_SET;
                        tran->attr.ins = ins;
                        tran->attr.hypothetical_reg_idx = hypothetical_reg;
                        add_transform_for_bb(tc, gs, bb, tran);
                        if (opcode == MVM_OP_sp_p6obind_o || opcode == MVM_OP_sp_bind_o) {
                            MVMSpeshFacts *tgt_facts = create_shadow_facts_h(tc, gs,
                                    hypothetical_reg);
                            MVMSpeshFacts *src_facts = MVM_spesh_get_facts(tc, g,
                                    ins->operands[2]);
                            MVM_spesh_copy_facts_resolved(tc, g, tgt_facts, src_facts);
                        }
                    }

                    /* For now, no transitive EA, so for the object case,
                     * mark the object being stored as requiring the real
                     * object. */
                    if (opcode == MVM_OP_sp_p6obind_o || opcode == MVM_OP_sp_bind_o)
                        real_object_required(tc, g, ins, ins->operands[2]);
                    break;
                }
                case MVM_OP_sp_p6oget_i:
                case MVM_OP_sp_p6oget_n:
                case MVM_OP_sp_p6oget_s:
                case MVM_OP_sp_p6oget_o:
                case MVM_OP_sp_p6ogetvc_o:
                case MVM_OP_sp_p6ogetvt_o: {
                    MVMSpeshFacts *target = MVM_spesh_get_facts(tc, g, ins->operands[1]);
                    MVMSpeshPEAAllocation *alloc = target->pea.allocation;
                    if (allocation_tracked(alloc)) {
                        MVMuint16 hypothetical_reg = attribute_offset_to_reg(tc, alloc,
                                ins->operands[2].lit_i16);
                        Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
                        tran->allocation = alloc;
                        tran->transform = TRANSFORM_GETATTR_TO_SET;
                        tran->attr.ins = ins;
                        tran->attr.hypothetical_reg_idx = hypothetical_reg;
                        add_transform_for_bb(tc, gs, bb, tran);
                        if (opcode == MVM_OP_sp_p6oget_o || opcode == MVM_OP_sp_p6ogetvc_o ||
                                opcode == MVM_OP_sp_p6ogetvt_o) {
                            MVMSpeshFacts *tgt_facts = create_shadow_facts_c(tc, gs,
                                    ins->operands[0]);
                            MVMSpeshFacts *src_facts = get_shadow_facts_h(tc, gs,
                                    hypothetical_reg);
                            if (src_facts) {
                                MVM_spesh_copy_facts_resolved(tc, g, tgt_facts, src_facts);
                                tgt_facts->pea.depend_allocation = alloc;
                            }
                        }
                    }
                    break;
                }
                case MVM_OP_sp_guardconc:
                    if (!settified_guard)
                        real_object_required(tc, g, ins, ins->operands[1]);
                    break;
                case MVM_OP_prof_allocated: {
                    MVMSpeshFacts *target = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                    MVMSpeshPEAAllocation *alloc = target->pea.allocation;
                    if (allocation_tracked(alloc)) {
                        Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
                        tran->allocation = alloc;
                        tran->transform = TRANSFORM_PROF_ALLOCATED;
                        tran->prof.ins = ins;
                        add_transform_for_bb(tc, gs, bb, tran);
                    }
                }
                case MVM_SSA_PHI: {
                    /* If a PHI doesn't really merge anything, and its input is
                     * a tracked object, we just alias the output. */
                    MVMuint16 num_operands = ins->info->num_operands;
                    if (num_operands == 2) {
                        MVMSpeshFacts *source = MVM_spesh_get_facts(tc, g, ins->operands[1]);
                        MVMSpeshPEAAllocation *alloc = source->pea.allocation;
                        if (allocation_tracked(alloc))
                            MVM_spesh_get_facts(tc, g, ins->operands[0])->pea.allocation = alloc;
                    }
                    else {
                        /* Otherwise, don't handle these for now. */
                        MVMuint32 i = 0;
                        for (i = 1; i < ins->info->num_operands; i++)
                            real_object_required(tc, g, ins, ins->operands[i]);
                    }
                    break;
                }
                default: {
                    /* Other instructions using tracked objects require the
                     * real object. */
                   MVMuint32 i = 0;
                   for (i = 0; i < ins->info->num_operands; i++)
                       if ((ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg)
                            real_object_required(tc, g, ins, ins->operands[i]);
                   break;
               }
            }

            ins = ins->next;
            ins_count++;
        }

        seen[bb->rpo_idx] = 1;
    }
    MVM_free(rpo);
    MVM_free(seen);
    return found_replaceable;
}

void MVM_spesh_pea(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMuint32 i;

    GraphState gs;
    memset(&gs, 0, sizeof(GraphState));
    MVM_VECTOR_INIT(gs.shadow_facts, 0);
    MVM_VECTOR_INIT(gs.tracked_registers, 0);
    gs.bb_states = MVM_spesh_alloc(tc, g, g->num_bbs * sizeof(BBState));
    for (i = 0; i < g->num_bbs; i++)
        MVM_VECTOR_INIT(gs.bb_states[i].transformations, 0);

    if (PEA_LOG) {
        char *sf_name = MVM_string_utf8_encode_C_string(tc, g->sf->body.name);
        char *sf_cuuid = MVM_string_utf8_encode_C_string(tc, g->sf->body.cuuid);
        pea_log("considering frame '%s' (%s)", sf_name, sf_cuuid);
        MVM_free(sf_name);
        MVM_free(sf_cuuid);
    }

    if (analyze(tc, g, &gs)) {
        MVMSpeshBB *bb = g->entry;
        gs.attr_regs = MVM_spesh_alloc(tc, g, gs.latest_hypothetical_reg_idx * sizeof(MVMuint16));
        while (bb) {
            for (i = 0; i < MVM_VECTOR_ELEMS(gs.bb_states[bb->idx].transformations); i++)
                apply_transform(tc, g, &gs, bb, gs.bb_states[bb->idx].transformations[i]);
            bb = bb->linear_next;
        }
    }

    for (i = 0; i < g->num_bbs; i++)
        MVM_VECTOR_DESTROY(gs.bb_states[i].transformations);
    MVM_VECTOR_DESTROY(gs.shadow_facts);
    MVM_VECTOR_DESTROY(gs.tracked_registers);
}

/* Clean up any deopt info. */
void MVM_spesh_pea_destroy_deopt_info(MVMThreadContext *tc, MVMSpeshPEADeopt *deopt_pea) {
    MVMint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(deopt_pea->materialize_info); i++)
        MVM_free(deopt_pea->materialize_info[i].attr_regs);
    MVM_VECTOR_DESTROY(deopt_pea->materialize_info);
    MVM_VECTOR_DESTROY(deopt_pea->deopt_point);
}
