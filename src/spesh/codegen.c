#include "moar.h"

/* Here we turn a spesh tree back into MoarVM bytecode, after optimizations
 * have been applied to it. */

typedef struct {
    MVM_VECTOR_DECL(MVMint32, idxs);
    MVM_VECTOR_DECL(MVMSpeshIns *, seen_phis);
} AllDeoptUsers;

/* Writer state. */
typedef struct {
    /* Bytecode output buffer. */
    MVMuint8  *bytecode;
    MVMuint32  bytecode_pos;
    MVMuint32  bytecode_alloc;

    /* Offsets where basic blocks are. */
    MVMint32 *bb_offsets;

    /* Fixups we need to do by basic block. */
    MVMint32    *fixup_locations;
    MVMSpeshBB **fixup_bbs;
    MVMuint32    num_fixups;
    MVMuint32    alloc_fixups;

    /* Copied frame handlers (which we'll update offsets of). */
    MVMFrameHandler *handlers;

    /* Persisted deopt usage info, so we can recover it should be produce an
     * inline from this bytecode in the future. */
    MVM_VECTOR_DECL(MVMint32, deopt_usage_info);

    MVM_VECTOR_DECL(MVMint32, deopt_synth_addrs);

    /* Working deopt users state (so we can allocate it once and re-use it). */
    AllDeoptUsers all_deopt_users;
} SpeshWriterState;

/* Write functions; all native endian. */
static void ensure_space(SpeshWriterState *ws, int bytes) {
    if (ws->bytecode_pos + bytes >= ws->bytecode_alloc) {
        ws->bytecode_alloc *= 2;
        ws->bytecode = MVM_realloc(ws->bytecode, ws->bytecode_alloc);
    }
}
static void write_int64(SpeshWriterState *ws, MVMuint64 value) {
    ensure_space(ws, 8);
    memcpy(ws->bytecode + ws->bytecode_pos, &value, 8);
    ws->bytecode_pos += 8;
}
static void write_uint64(SpeshWriterState *ws, MVMuint64 value) {
    ensure_space(ws, 8);
    memcpy(ws->bytecode + ws->bytecode_pos, &value, 8);
    ws->bytecode_pos += 8;
}
static void write_int32(SpeshWriterState *ws, MVMuint32 value) {
    ensure_space(ws, 4);
    memcpy(ws->bytecode + ws->bytecode_pos, &value, 4);
    ws->bytecode_pos += 4;
}
static void write_int16(SpeshWriterState *ws, MVMuint16 value) {
    ensure_space(ws, 2);
    memcpy(ws->bytecode + ws->bytecode_pos, &value, 2);
    ws->bytecode_pos += 2;
}
static void write_int8(SpeshWriterState *ws, MVMuint8 value) {
    ensure_space(ws, 1);
    memcpy(ws->bytecode + ws->bytecode_pos, &value, 1);
    ws->bytecode_pos++;
}
static void write_num32(SpeshWriterState *ws, MVMnum32 value) {
    ensure_space(ws, 4);
    memcpy(ws->bytecode + ws->bytecode_pos, &value, 4);
    ws->bytecode_pos += 4;
}
static void write_num64(SpeshWriterState *ws, MVMnum64 value) {
    ensure_space(ws, 8);
    memcpy(ws->bytecode + ws->bytecode_pos, &value, 8);
    ws->bytecode_pos += 8;
}

/* Deopt user retention logic for the sake of inlining. */
static void collect_deopt_users(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand from,
        AllDeoptUsers *all_deopt_users) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, from);
    MVMSpeshDeoptUseEntry *deopt_users = facts->usage.deopt_users;
    MVMSpeshUseChainEntry *users = facts->usage.users;
    while (deopt_users) {
        MVM_VECTOR_PUSH(all_deopt_users->idxs, deopt_users->deopt_idx);
        deopt_users = deopt_users->next;
    }
    while (users) {
        MVMSpeshIns *ins = users->user;
        if (ins->info->opcode == MVM_SSA_PHI) {
            MVMint32 seen = 0;
            MVMuint32 i;
            for (i = 0; i < MVM_VECTOR_ELEMS(all_deopt_users->seen_phis); i++) {
                if (all_deopt_users->seen_phis[i] == ins) {
                    seen = 1;
                    break;
                }
            }
            if (!seen) {
                MVM_VECTOR_PUSH(all_deopt_users->seen_phis, ins);
                collect_deopt_users(tc, g, ins->operands[0], all_deopt_users);
            }
        }
        users = users->next;
    }
}

/* Writes instructions within a basic block boundary. */
static void write_instructions(MVMThreadContext *tc, MVMSpeshGraph *g, SpeshWriterState *ws, MVMSpeshBB *bb) {
    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        MVMint32 i;

        /* Process any pre-annotations, and note if there were any deopt ones;
         * if so, these should be processed later. */
        MVMSpeshAnn *ann = ins->annotations;
        MVMuint32 has_deopts = 0;
        while (ann) {
            switch (ann->type) {
            case MVM_SPESH_ANN_FH_START:
                ws->handlers[ann->data.frame_handler_index].start_offset =
                    ws->bytecode_pos;
                break;
            case MVM_SPESH_ANN_FH_END:
                ws->handlers[ann->data.frame_handler_index].end_offset =
                    ws->bytecode_pos;
                break;
            case MVM_SPESH_ANN_FH_GOTO:
                ws->handlers[ann->data.frame_handler_index].goto_offset =
                    ws->bytecode_pos;
                break;
            case MVM_SPESH_ANN_DEOPT_ONE_INS:
            case MVM_SPESH_ANN_DEOPT_ALL_INS:
            case MVM_SPESH_ANN_DEOPT_INLINE:
            case MVM_SPESH_ANN_DEOPT_SYNTH:
                has_deopts = 1;
                break;
            case MVM_SPESH_ANN_INLINE_START:
                g->inlines[ann->data.inline_idx].start = ws->bytecode_pos;
                break;
            case MVM_SPESH_ANN_INLINE_END:
                g->inlines[ann->data.inline_idx].end = ws->bytecode_pos;
                break;
            case MVM_SPESH_ANN_DEOPT_OSR:
            case MVM_SPESH_ANN_DEOPT_PRE_INS:
                g->deopt_addrs[2 * ann->data.deopt_idx + 1] = ws->bytecode_pos;
                break;
            }
            ann = ann->next;
        }

        if (ins->info->opcode != MVM_SSA_PHI) {
            /* Real instruction, not a phi. See if we need to save any deopt
             * usage information at this location. If the thing that is
             * written is consumed by a PHI and those also have deopt users
             * then we need to handle that case also. */
            if (g->facts && ins->info->num_operands >= 1 &&
                    (ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
                collect_deopt_users(tc, g, ins->operands[0], &(ws->all_deopt_users));
                if (MVM_VECTOR_ELEMS(ws->all_deopt_users.idxs)) {
                    MVMint32 count = MVM_VECTOR_ELEMS(ws->all_deopt_users.idxs);
                    MVMint32 i;
                    MVM_VECTOR_PUSH(ws->deopt_usage_info, ws->bytecode_pos);
                    MVM_VECTOR_PUSH(ws->deopt_usage_info, count);
                    for (i = 0; i < count; i++)
                        MVM_VECTOR_PUSH(ws->deopt_usage_info, ws->all_deopt_users.idxs[i]);
                }
                MVM_VECTOR_CLEAR(ws->all_deopt_users.idxs);
                MVM_VECTOR_CLEAR(ws->all_deopt_users.seen_phis);
            }

            /* Emit opcode. */
            if (ins->info->opcode == (MVMuint16)-1) {
                /* Ext op; resolve. */
                MVMExtOpRecord *extops     = g->sf->body.cu->body.extops;
                MVMuint16       num_extops = g->sf->body.cu->body.num_extops;
                MVMint32        found      = 0;
                for (i = 0; i < num_extops; i++) {
                    if (extops[i].info == ins->info) {
                        write_int16(ws, MVM_OP_EXT_BASE + i);
                        found = 1;
                        break;
                    }
                }
                if (!found)
                    MVM_oops(tc, "Spesh: failed to resolve extop in code-gen");
            }
            else {
                /* Core op. */
                write_int16(ws, ins->info->opcode);
            }

            /* Write out operands. */
            for (i = 0; i < ins->info->num_operands; i++) {
                MVMuint8 flags = ins->info->operands[i];
                MVMuint8 rw    = flags & MVM_operand_rw_mask;
                switch (rw) {
                case MVM_operand_read_reg:
                case MVM_operand_write_reg:
                    write_int16(ws, ins->operands[i].reg.orig);
                    break;
                case MVM_operand_read_lex:
                case MVM_operand_write_lex:
                    write_int16(ws, ins->operands[i].lex.idx);
                    write_int16(ws, ins->operands[i].lex.outers);
                    break;
                case MVM_operand_literal: {
                    MVMuint8 type = flags & MVM_operand_type_mask;
                    switch (type) {
                    case MVM_operand_int8:
                        write_int8(ws, ins->operands[i].lit_i8);
                        break;
                    case MVM_operand_int16:
                        write_int16(ws, ins->operands[i].lit_i16);
                        break;
                    case MVM_operand_int32:
                        write_int32(ws, ins->operands[i].lit_i32);
                        break;
                    case MVM_operand_uint32:
                        write_int32(ws, ins->operands[i].lit_ui32);
                        break;
                    case MVM_operand_int64:
                        write_int64(ws, ins->operands[i].lit_i64);
                        break;
                    case MVM_operand_uint64:
                        write_uint64(ws, ins->operands[i].lit_ui64);
                        break;
                    case MVM_operand_num32:
                        write_num32(ws, ins->operands[i].lit_n32);
                        break;
                    case MVM_operand_num64:
                        write_num64(ws, ins->operands[i].lit_n64);
                        break;
                    case MVM_operand_callsite:
                        write_int16(ws, ins->operands[i].callsite_idx);
                        break;
                    case MVM_operand_coderef:
                        write_int16(ws, ins->operands[i].coderef_idx);
                        break;
                    case MVM_operand_str:
                        write_int32(ws, ins->operands[i].lit_str_idx);
                        break;
                    case MVM_operand_ins: {
                        MVMuint32 bb_idx = ins->operands[i].ins_bb->idx;
                        MVMint32 offset;
                        if (bb_idx >= g->num_bbs)
                            MVM_panic(1, "Spesh codegen: out of range BB index %d", bb_idx);
                        offset = ws->bb_offsets[bb_idx];
                        if (offset >= 0) {
                            /* Already know where it is, so just write it. */
                            write_int32(ws, offset);
                        }
                        else {
                            /* Need to fix it up. */
                            if (ws->num_fixups == ws->alloc_fixups) {
                                ws->alloc_fixups *= 2;
                                ws->fixup_locations = MVM_realloc(ws->fixup_locations,
                                    ws->alloc_fixups * sizeof(MVMint32));
                                ws->fixup_bbs = MVM_realloc(ws->fixup_bbs,
                                    ws->alloc_fixups * sizeof(MVMSpeshBB *));
                            }
                            ws->fixup_locations[ws->num_fixups] = ws->bytecode_pos;
                            ws->fixup_bbs[ws->num_fixups]       = ins->operands[i].ins_bb;
                            write_int32(ws, 0);
                            ws->num_fixups++;
                        }
                        break;
                    }
                    case MVM_operand_spesh_slot:
                        write_int16(ws, ins->operands[i].lit_i16);
                        break;
                    default:
                        MVM_oops(tc,
                            "Spesh: unknown operand type %d in codegen (op %s)",
                            (int)type, ins->info->name);
                    }
                    }
                    break;
                default:
                    MVM_oops(tc, "Spesh: unknown operand type in codegen");
                }
            }
        }

        /* If there were deopt point annotations, update table. */
        if (has_deopts) {
#ifndef NDEBUG
            MVMint32 seen_deopt_idx = 0;
            MVMint32 deopt_idx;
            switch (ins->info->opcode) {
            case MVM_OP_sp_guard:
            case MVM_OP_sp_guardconc:
            case MVM_OP_sp_guardtype:
            case MVM_OP_sp_guardobj:
            case MVM_OP_sp_guardnotobj:
            case MVM_OP_sp_rebless:
                deopt_idx = ins->operands[3].lit_ui32;
                break;
            case MVM_OP_sp_guardsf:
            case MVM_OP_sp_guardsfouter:
            case MVM_OP_sp_guardjustconc:
            case MVM_OP_sp_guardjusttype:
                deopt_idx = ins->operands[2].lit_ui32;
                break;
            default:
                deopt_idx = -1;
                break;
            }
#endif
            ann = ins->annotations;
            while (ann) {
                switch (ann->type) {
                case MVM_SPESH_ANN_DEOPT_SYNTH:
                    MVM_VECTOR_PUSH(ws->deopt_synth_addrs, ann->data.deopt_idx);
                    MVM_VECTOR_PUSH(ws->deopt_synth_addrs, ws->bytecode_pos);
                    break;
                case MVM_SPESH_ANN_DEOPT_ONE_INS:
                case MVM_SPESH_ANN_DEOPT_ALL_INS:
                case MVM_SPESH_ANN_DEOPT_INLINE:
                    g->deopt_addrs[2 * ann->data.deopt_idx + 1] = ws->bytecode_pos;
#ifndef NDEBUG
                    if (deopt_idx == ann->data.deopt_idx)
                        seen_deopt_idx = 1;
#endif
                    break;
                }
                ann = ann->next;
            }
            assert(deopt_idx < 0 || seen_deopt_idx);
        }

        ins = ins->next;
    }
}

/* Generate bytecode from a spesh graph. */
MVMSpeshCode * MVM_spesh_codegen(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshCode *res;
    MVMSpeshBB   *bb;
    MVMuint32     i, hanlen;

    /* Initialize writer state. */
    SpeshWriterState *ws     = MVM_malloc(sizeof(SpeshWriterState));
    ws->bytecode_pos    = 0;
    ws->bytecode_alloc  = 1024;
    ws->bytecode        = MVM_malloc(ws->bytecode_alloc);
    ws->bb_offsets      = MVM_malloc(g->num_bbs * sizeof(MVMint32));
    ws->num_fixups      = 0;
    ws->alloc_fixups    = 64;
    ws->fixup_locations = MVM_malloc(ws->alloc_fixups * sizeof(MVMint32));
    ws->fixup_bbs       = MVM_malloc(ws->alloc_fixups * sizeof(MVMSpeshBB *));
    for (i = 0; i < g->num_bbs; i++)
        ws->bb_offsets[i] = -1;
    MVM_VECTOR_INIT(ws->deopt_usage_info, 0);
    MVM_VECTOR_INIT(ws->all_deopt_users.idxs, 0);
    MVM_VECTOR_INIT(ws->all_deopt_users.seen_phis, 0);
    MVM_VECTOR_INIT(ws->deopt_synth_addrs, 0);

    /* Create copy of handlers, and -1 all offsets so we can catch missing
     * updates. */
    hanlen = g->num_handlers * sizeof(MVMFrameHandler);
    if (hanlen) {
        ws->handlers = MVM_malloc(hanlen);
        memcpy(ws->handlers, g->handlers, hanlen);
        for (i = 0; i < g->num_handlers; i++) {
            ws->handlers[i].start_offset = -1;
            ws->handlers[i].end_offset   = -1;
            ws->handlers[i].goto_offset  = -1;
        }
    }
    else {
        ws->handlers = NULL;
    }

    /* -1 all the deopt targets, so we'll easily catch those that don't get
     * mapped if we try to use them. Same for inlines. */
    for (i = 0; i < g->num_deopt_addrs; i++)
        g->deopt_addrs[i * 2 + 1] = -1;
    for (i = 0; i < g->num_inlines; i++) {
        g->inlines[i].start = -1;
        g->inlines[i].end = -1;
    }

    /* Write out each of the basic blocks, in linear order. Skip the first,
     * dummy, block. */
    bb = g->entry->linear_next;
    while (bb) {
        ws->bb_offsets[bb->idx] = ws->bytecode_pos;
        write_instructions(tc, g, ws, bb);
        bb = bb->linear_next;
    }

    /* Fixup labels we were too early for. */
    for (i = 0; i < ws->num_fixups; i++)
        memcpy((ws->bytecode + ws->fixup_locations[i]),
               ws->bb_offsets + ws->fixup_bbs[i]->idx, sizeof(MVMuint32));

    /* Ensure all handlers that are reachable got fixed up. */
    for (i = 0; i < g->num_handlers; i++) {
        if (g->unreachable_handlers && g->unreachable_handlers[i]) {
            ws->handlers[i].start_offset = -1;
            ws->handlers[i].end_offset = -1;
            ws->handlers[i].goto_offset = -1;
        }
        else if (ws->handlers[i].start_offset == (MVMuint32)-1 ||
                 ws->handlers[i].end_offset   == (MVMuint32)-1 ||
                 ws->handlers[i].goto_offset  == (MVMuint32)-1) {
            MVM_oops(tc, "Spesh: failed to fix up handler %d in %s (%d, %d, %d)",
                i,
                MVM_string_utf8_maybe_encode_C_string(tc, g->sf->body.name),
                (int)ws->handlers[i].start_offset,
                (int)ws->handlers[i].end_offset,
                (int)ws->handlers[i].goto_offset);
        }
    }

    /* Ensure all inlines got fixed up. */
    for (i = 0; i < g->num_inlines; i++) {
        if (g->inlines[i].unreachable) {
            g->inlines[i].start = -1;
            g->inlines[i].end = -1;
        }
        else {
            if (g->inlines[i].start == (MVMuint32)-1 || g->inlines[i].end == (MVMuint32)-1)
                MVM_oops(tc, "Spesh: failed to fix up inline %d (%s) %d %d",
                    i,
                    MVM_string_utf8_maybe_encode_C_string(tc, g->inlines[i].sf->body.name),
                    g->inlines[i].start,
                    g->inlines[i].end
                );
        }
    }

    /* Add terminating -1 to the deopt usage info. */
    if (g->facts)
        MVM_VECTOR_PUSH(ws->deopt_usage_info, -1);

    /* Produce result data structure. */
    res                   = MVM_malloc(sizeof(MVMSpeshCode));
    res->bytecode         = ws->bytecode;
    res->bytecode_size    = ws->bytecode_pos;
    res->handlers         = ws->handlers;
    res->deopt_usage_info = ws->deopt_usage_info;
    res->deopt_synths     = ws->deopt_synth_addrs;
    res->num_deopt_synths = ws->deopt_synth_addrs_num / 2; /* 2 values per entry */

    /* Cleanup. */
    MVM_free(ws->bb_offsets);
    MVM_free(ws->fixup_locations);
    MVM_free(ws->fixup_bbs);
    MVM_VECTOR_DESTROY(ws->all_deopt_users.idxs);
    MVM_VECTOR_DESTROY(ws->all_deopt_users.seen_phis);
    MVM_free(ws);

    return res;
}
