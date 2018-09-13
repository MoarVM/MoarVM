#include "moar.h"

/* A transformation that we want to perform. */
#define TRANSFORM_DELETE_FASTCREATE 0
#define TRANSFORM_GETATTR_TO_SET    1
#define TRANSFORM_BINDATTR_TO_SET   2
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
    };
} Transformation;

/* State held per basic block. */
typedef struct {
    MVM_VECTOR_DECL(Transformation *, transformations);
} BBState;

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

/* Apply a transformation to the graph. */
static void apply_transform(MVMThreadContext *tc, MVMSpeshGraph *g, GraphState *gs,
        MVMSpeshBB *bb, Transformation *t) {
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
            break;
        }
        default:
            MVM_oops(tc, "Unimplemented partial escape analysis transform");
    }
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
        return alloc;
    }
    return NULL;
}

static void add_transform_for_bb(MVMThreadContext *tc, GraphState *gs, MVMSpeshBB *bb,
        Transformation *tran) {
    MVM_VECTOR_PUSH(gs->bb_states[bb->idx].transformations, tran);
}

static MVMuint16 attribute_offset_to_reg(MVMThreadContext *tc, MVMSpeshPEAAllocation *alloc,
        MVMint16 offset) {
    MVMuint32 idx = MVM_p6opaque_offset_to_attr_idx(tc, alloc->type, offset);
    return alloc->hypothetical_attr_reg_idxs[idx];
}

static MVMuint32 analyze(MVMThreadContext *tc, MVMSpeshGraph *g, GraphState *gs) {
    MVMSpeshBB *bb = g->entry;
    MVMuint32 found_replaceable = 0;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            /* We currently cannot handle any deoptimization taking place. */
            if (ins->info->may_cause_deopt)
                return 0;

            /* Look for significant instructions. */
            switch (ins->info->opcode) {
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
                case MVM_OP_sp_p6obind_i:
                case MVM_OP_sp_p6obind_n:
                case MVM_OP_sp_p6obind_s:
                case MVM_OP_sp_p6obind_o: {
                    /* TODO: For _o we should also inspect the argument that we read in order to
                     * bind into here. In case we are tracking it also. */
                    MVMSpeshFacts *target = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                    MVMSpeshPEAAllocation *alloc = target->pea.allocation;
                    if (alloc) {
                        Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
                        tran->allocation = alloc;
                        tran->transform = TRANSFORM_BINDATTR_TO_SET;
                        tran->attr.ins = ins;
                        tran->attr.hypothetical_reg_idx = attribute_offset_to_reg(tc, alloc,
                                ins->operands[1].lit_i16);
                        add_transform_for_bb(tc, gs, bb, tran);
                    }
                    break;
                }
                case MVM_OP_sp_p6oget_i:
                case MVM_OP_sp_p6oget_n:
                case MVM_OP_sp_p6oget_s:
                case MVM_OP_sp_p6ogetvt_o: {
                    MVMSpeshFacts *target = MVM_spesh_get_facts(tc, g, ins->operands[1]);
                    MVMSpeshPEAAllocation *alloc = target->pea.allocation;
                    if (alloc) {
                        Transformation *tran = MVM_spesh_alloc(tc, g, sizeof(Transformation));
                        tran->allocation = alloc;
                        tran->transform = TRANSFORM_GETATTR_TO_SET;
                        tran->attr.ins = ins;
                        tran->attr.hypothetical_reg_idx = attribute_offset_to_reg(tc, alloc,
                                ins->operands[2].lit_i16);
                        add_transform_for_bb(tc, gs, bb, tran);
                    }
                    break;
                }
                default: {
                   MVMuint32 i = 0;
                   for (i = 0; i < ins->info->num_operands; i++) {
                       if ((ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg) {
                            MVMSpeshFacts *target = MVM_spesh_get_facts(tc, g, ins->operands[i]);
                            /* If there's another op using it, we'd need to materialize.
                             * We don't support that yet, so bail out. */
                            if (target->pea.allocation)
                                return 0;
                       }
                   }
                   break;
               }
            }

            ins = ins->next;
        }

        /* For now, we only handle linear code with no flow control. */
        bb = bb->linear_next;
        if (bb && bb->num_succ > 1)
            return 0;
    }
    return found_replaceable;
}

void MVM_spesh_pea(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMuint32 i;

    GraphState gs;
    memset(&gs, 0, sizeof(GraphState));
    gs.bb_states = MVM_spesh_alloc(tc, g, g->num_bbs * sizeof(BBState));
    for (i = 0; i < g->num_bbs; i++)
        MVM_VECTOR_INIT(gs.bb_states[i].transformations, 0);
    
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
}
