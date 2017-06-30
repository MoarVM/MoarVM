#include "moar.h"

/* Provided spesh is enabled, create a specialization data log for the thread
 * in question. */
void MVM_spesh_log_create_for_thread(MVMThreadContext *tc) {
    if (tc->instance->spesh_enabled)
        tc->spesh_log = (MVMSpeshLog *)MVM_repr_alloc_init(tc, tc->instance->SpeshLog); 
}

/* Increments the used count and - if it hits the limit - sends the log off
 * to the worker thread and NULLs it out. */
void commit_entry(MVMThreadContext *tc, MVMSpeshLog *sl) {
    sl->body.used++;
    if (sl->body.used == sl->body.limit) {
        tc->spesh_log = NULL;
        MVM_repr_push_o(tc, tc->instance->spesh_queue, (MVMObject *)sl);
    }
}

/* Log the entry to a call frame. */
void MVM_spesh_log_entry(MVMThreadContext *tc, MVMint32 cid, MVMStaticFrame *sf, MVMCallsite *cs) {
    MVMSpeshLog *sl = tc->spesh_log;
    if (sl) {
        MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
        entry->kind = MVM_SPESH_LOG_ENTRY;
        entry->id = cid;
        MVM_ASSIGN_REF(tc, &(sl->common.header), entry->entry.sf, sf);
        entry->entry.cs = cs->is_interned ? cs : NULL;
        commit_entry(tc, sl);
    }
}

/* Log an OSR point being hit. */
void MVM_spesh_log_osr(MVMThreadContext *tc) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    if (sl && cid) {
        MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
        entry->kind = MVM_SPESH_LOG_OSR;
        entry->id = cid;
        entry->osr.bytecode_offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start)) - 2;
        commit_entry(tc, sl);
    }
}

/* Log a type or parameter type. */
static void log_type(MVMThreadContext *tc, MVMObject *value, MVMSpeshLogEntryKind kind) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    if (sl && cid) {
        MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
        entry->kind = kind;
        entry->id = cid;
        MVM_ASSIGN_REF(tc, &(sl->common.header), entry->type.type, value->st->WHAT);
        entry->type.flags = IS_CONCRETE(value) ? MVM_SPESH_LOG_ENTRY : 0;
        entry->type.bytecode_offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start)) - 2;
        commit_entry(tc, sl);
    }
}
void MVM_spesh_log_type(MVMThreadContext *tc, MVMObject *value) {
    log_type(tc, value, MVM_SPESH_LOG_TYPE);
}
void MVM_spesh_log_parameter(MVMThreadContext *tc, MVMObject *param) {
    log_type(tc, param, MVM_SPESH_LOG_PARAMETER);
}

/* Log a static value. */
void MVM_spesh_log_static(MVMThreadContext *tc, MVMObject *value) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    if (sl && cid) {
        MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
        entry->kind = MVM_SPESH_LOG_STATIC;
        entry->id = cid;
        MVM_ASSIGN_REF(tc, &(sl->common.header), entry->value.value, value);
        entry->type.bytecode_offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start)) - 2;
        commit_entry(tc, sl);
    }
}

/* Log a decont, only those that did not invoke. */
void MVM_spesh_log_decont(MVMThreadContext *tc, MVMuint8 *prev_op, MVMObject *value) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    if (sl && cid && prev_op - 4 == *(tc->interp_cur_op)) {
        MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
        entry->kind = MVM_SPESH_LOG_TYPE;
        entry->id = cid;
        MVM_ASSIGN_REF(tc, &(sl->common.header), entry->type.type, value->st->WHAT);
        entry->type.flags = IS_CONCRETE(value) ? MVM_SPESH_LOG_ENTRY : 0;
        entry->type.bytecode_offset = (prev_op - *(tc->interp_bytecode_start)) - 2;
        commit_entry(tc, sl);
    }
}

/* Code below this point is legacy spesh logging infrasturcture, and will be
 * replaced or significantly changed once the new spesh worker approach is
 * in place. */

static void insert_log(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins, MVMint32 next_bb) {
    /* Add the entry. */
    MVMSpeshIns *log_ins         = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    log_ins->info                = MVM_op_get_op(MVM_OP_sp_log);
    log_ins->operands            = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    log_ins->operands[0].reg     = ins->operands[0].reg;
    log_ins->operands[1].lit_i16 = g->num_log_slots;
    if (next_bb)
        MVM_spesh_manipulate_insert_ins(tc, bb->succ[0], NULL, log_ins);
    else
        MVM_spesh_manipulate_insert_ins(tc, bb, ins, log_ins);
    g->num_log_slots++;

    /* Steal the de-opt annotation into the log instruction, if it exists. */
    if (ins->annotations) {
        MVMSpeshAnn *prev_ann = NULL;
        MVMSpeshAnn *cur_ann  = ins->annotations;
        while (cur_ann) {
            if (cur_ann->type == MVM_SPESH_ANN_DEOPT_ONE_INS) {
                if (prev_ann)
                    prev_ann->next = cur_ann->next;
                else
                    ins->annotations = cur_ann->next;
                cur_ann->next = NULL;
                log_ins->annotations = cur_ann;
                break;
            }
            prev_ann = cur_ann;
            cur_ann = cur_ann->next;
        }
    }
}
 
void MVM_spesh_log_add_logging(MVMThreadContext *tc, MVMSpeshGraph *g, MVMint32 osr) {
    MVMSpeshBB  *bb;

    /* We've no log slots so far. */
    g->num_log_slots = 0;

    /* Work through the code, adding logging instructions where needed. */
    bb = g->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            switch (ins->info->opcode) {
            case MVM_OP_getlex:
                if (g->sf->body.local_types[ins->operands[0].reg.orig] == MVM_reg_obj)
                    insert_log(tc, g, bb, ins, 0);
                break;
            case MVM_OP_getlex_no:
            case MVM_OP_getattr_o:
            case MVM_OP_getattrs_o:
            case MVM_OP_getlexstatic_o:
            case MVM_OP_getlexperinvtype_o:
                insert_log(tc, g, bb, ins, 0);
                break;
            case MVM_OP_invoke_o:
                insert_log(tc, g, bb, ins, 1);
                break;
            case MVM_OP_osrpoint:
                if (osr)
                    ins->info = MVM_op_get_op(MVM_OP_sp_osrfinalize);
                else
                    MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
                break;
            }
            ins = ins->next;
        }
        bb = bb->linear_next;
    }

    /* Allocate space for logging storage. */
    g->log_slots = g->num_log_slots
        ? MVM_calloc(g->num_log_slots * MVM_SPESH_LOG_RUNS, sizeof(MVMCollectable *))
        : NULL;
}
