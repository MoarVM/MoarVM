#include "moar.h"

/* This is where the spesh stuff all begins. The logic in here takes bytecode
 * and builds a spesh graph from it. This is a CFG in SSA form. Transforming
 * to SSA involves computing dominance frontiers, done by the algorithm found
 * in http://www.cs.rice.edu/~keith/EMBED/dom.pdf. The SSA algorithm itself is
 * from http://www.cs.utexas.edu/~pingali/CS380C/2010/papers/ssaCytron.pdf. */

#define GET_I8(pc, idx)     *((MVMint8 *)(pc + idx))
#define GET_UI8(pc, idx)    *((MVMuint8 *)(pc + idx))
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))
#define GET_I32(pc, idx)    *((MVMint32 *)(pc + idx))
#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))
#define GET_N32(pc, idx)    *((MVMnum32 *)(pc + idx))

/* Allocate a piece of memory from the spesh graph's buffer. Deallocated when
 * the spesh graph is. */
void * MVM_spesh_alloc(MVMThreadContext *tc, MVMSpeshGraph *g, size_t bytes) {
    char *result = NULL;

#if !defined(MVM_CAN_UNALIGNED_INT64) || !defined(MVM_CAN_UNALIGNED_NUM64)
    /* Round up size to next multiple of 8, to ensure alignment. */
    bytes = (bytes + 7) & ~7;
#endif

    if (g->mem_block) {
        MVMSpeshMemBlock *block = g->mem_block;
        if (block->alloc + bytes < block->limit) {
            result = block->alloc;
            block->alloc += bytes;
        }
    }
    if (!result) {
        /* No block, or block was full. Add another. */
        MVMSpeshMemBlock *block = MVM_malloc(sizeof(MVMSpeshMemBlock));
        size_t buffer_size = g->mem_block
            ? MVM_SPESH_MEMBLOCK_SIZE
            : MVM_SPESH_FIRST_MEMBLOCK_SIZE;
        if (buffer_size < bytes)
            buffer_size = bytes;
        block->buffer = MVM_calloc(buffer_size, 1);
        block->alloc  = block->buffer;
        block->limit  = block->buffer + buffer_size;
        block->prev   = g->mem_block;
        g->mem_block  = block;

        /* Now allocate out of it. */
        result = block->alloc;
        block->alloc += bytes;
    }
    return result;
}

/* Looks up op info; doesn't sanity check, since we should be working on code
 * that already pass validation. */
static const MVMOpInfo * get_op_info(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint16 opcode) {
    if (opcode < MVM_OP_EXT_BASE) {
        return MVM_op_get_op(opcode);
    }
    else {
        MVMuint16       index  = opcode - MVM_OP_EXT_BASE;
        MVMExtOpRecord *record = &cu->body.extops[index];
        return MVM_ext_resolve_extop_record(tc, record);
    }
}

/* Records a de-optimization annotation and mapping pair. */
static void add_deopt_annotation(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins_node,
                                 MVMuint8 *pc, MVMint32 type) {
    /* Add an the annotations. */
    MVMSpeshAnn *ann      = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
    ann->type             = type;
    ann->data.deopt_idx   = g->num_deopt_addrs;
    ann->next             = ins_node->annotations;
    ins_node->annotations = ann;

    /* Record PC in the deopt entries table. */
    if (g->num_deopt_addrs == g->alloc_deopt_addrs) {
        g->alloc_deopt_addrs += 4;
        if (g->deopt_addrs)
            g->deopt_addrs = MVM_realloc(g->deopt_addrs,
                g->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        else
            g->deopt_addrs = MVM_malloc(g->alloc_deopt_addrs * sizeof(MVMint32) * 2);
    }
    g->deopt_addrs[2 * g->num_deopt_addrs] = pc - g->bytecode;
    g->num_deopt_addrs++;
}

/* Finds the linearly previous basic block (not cheap, but uncommon). */
MVMSpeshBB * MVM_spesh_graph_linear_prev(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *search) {
    MVMSpeshBB *bb = g->entry;
    while (bb) {
        if (bb->linear_next == search)
            return bb;
        bb = bb->linear_next;
    }
    return NULL;
}

/* Builds the control flow graph, populating the passed spesh graph structure
 * with it. This also makes nodes for all of the instruction. */
#define MVM_CFG_BB_START    1
#define MVM_CFG_BB_END      2
static void build_cfg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMStaticFrame *sf,
                      MVMint32 *existing_deopts, MVMint32 num_existing_deopts) {
    MVMSpeshBB  *cur_bb, *prev_bb;
    MVMSpeshIns *last_ins;
    MVMint64     i;
    MVMint32     bb_idx;

    /* Temporary array of all MVMSpeshIns we create (one per instruction).
     * Overestimate at size. Has the flat view, matching the bytecode. */
    MVMSpeshIns **ins_flat = MVM_calloc(g->bytecode_size / 2, sizeof(MVMSpeshIns *));

    /* Temporary array where each byte in the input bytecode gets a 32-bit
     * integer. This is used for two things:
     * A) When we make the MVMSpeshIns for an instruction starting at the
     *    byte, we put the instruction index (into ins_flat) in the slot,
     *    shifting it by 2 bits to the left. We will use this to do fixups.
     * B) The first bit is "I have an incoming branch" - that is, start of
     *    a basic block. The second bit is "I can branch" - that is, end of
     *    a basic block. It's possible to have both bits set.
     * Anything that's just a zero has no instruction starting there. */
    MVMuint32 *byte_to_ins_flags = MVM_calloc(g->bytecode_size, sizeof(MVMuint32));

    /* Instruction to basic block mapping. Initialized later. */
    MVMSpeshBB **ins_to_bb = NULL;

    /* Make first pass through the bytecode. In this pass, we make MVMSpeshIns
     * nodes for each instruction and set the start/end of block bits. Also
     * set handler targets as basic block starters. */
    MVMCompUnit *cu       = sf->body.cu;
    MVMuint8    *pc       = g->bytecode;
    MVMuint8    *end      = g->bytecode + g->bytecode_size;
    MVMuint32    ins_idx  = 0;
    MVMuint8     next_bbs = 1; /* Next iteration (here, first) starts a BB. */
    for (i = 0; i < g->num_handlers; i++)
        byte_to_ins_flags[g->handlers[i].goto_offset] |= MVM_CFG_BB_START;
    while (pc < end) {
        /* Look up op info. */
        MVMuint16  opcode     = *(MVMuint16 *)pc;
        MVMuint8  *args       = pc + 2;
        MVMuint8   arg_size   = 0;
        const MVMOpInfo *info = get_op_info(tc, cu, opcode);

        /* Create an instruction node, add it, and record its position. */
        MVMSpeshIns *ins_node = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        ins_flat[ins_idx] = ins_node;
        byte_to_ins_flags[pc - g->bytecode] |= ins_idx << 2;

        /* Did previous instruction end a basic block? */
        if (next_bbs) {
            byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_START;
            next_bbs = 0;
        }

        /* Also check we're not already a BB start due to being a branch
         * target, in which case we should ensure our prior is marked as
         * a BB end. */
        else {
            if (byte_to_ins_flags[pc - g->bytecode] & MVM_CFG_BB_START) {
                MVMuint32 hunt = pc - g->bytecode;
                while (!byte_to_ins_flags[--hunt]);
                byte_to_ins_flags[hunt] |= MVM_CFG_BB_END;
            }
        }

        /* Store opcode */
        ins_node->info = info;

        /* Go over operands. */
        ins_node->operands = MVM_spesh_alloc(tc, g, info->num_operands * sizeof(MVMSpeshOperand));
        for (i = 0; i < info->num_operands; i++) {
            MVMuint8 flags = info->operands[i];
            MVMuint8 rw    = flags & MVM_operand_rw_mask;
            switch (rw) {
            case MVM_operand_read_reg:
            case MVM_operand_write_reg:
                ins_node->operands[i].reg.orig = GET_UI16(args, arg_size);
                arg_size += 2;
                break;
            case MVM_operand_read_lex:
            case MVM_operand_write_lex:
                ins_node->operands[i].lex.idx    = GET_UI16(args, arg_size);
                ins_node->operands[i].lex.outers = GET_UI16(args, arg_size + 2);
                arg_size += 4;
                break;
            case MVM_operand_literal: {
                MVMuint32 type = flags & MVM_operand_type_mask;
                switch (type) {
                case MVM_operand_int8:
                    ins_node->operands[i].lit_i8 = GET_I8(args, arg_size);
                    arg_size += 1;
                    break;
                case MVM_operand_int16:
                    ins_node->operands[i].lit_i16 = GET_I16(args, arg_size);
                    arg_size += 2;
                    break;
                case MVM_operand_int32:
                    ins_node->operands[i].lit_i32 = GET_I32(args, arg_size);
                    arg_size += 4;
                    break;
                case MVM_operand_int64:
                    ins_node->operands[i].lit_i64 = MVM_BC_get_I64(args, arg_size);
                    arg_size += 8;
                    break;
                case MVM_operand_num32:
                    ins_node->operands[i].lit_n32 = GET_N32(args, arg_size);
                    arg_size += 4;
                    break;
                case MVM_operand_num64:
                    ins_node->operands[i].lit_n64 = MVM_BC_get_N64(args, arg_size);
                    arg_size += 8;
                    break;
                case MVM_operand_callsite:
                    ins_node->operands[i].callsite_idx = GET_UI16(args, arg_size);
                    arg_size += 2;
                    break;
                case MVM_operand_coderef:
                    ins_node->operands[i].coderef_idx = GET_UI16(args, arg_size);
                    arg_size += 2;
                    break;
                case MVM_operand_str:
                    ins_node->operands[i].lit_str_idx = GET_UI32(args, arg_size);
                    arg_size += 4;
                    break;
                case MVM_operand_ins: {
                    /* Stash instruction offset. */
                    MVMuint32 target = GET_UI32(args, arg_size);
                    ins_node->operands[i].ins_offset = target;

                    /* This is a branching instruction, so it's a BB end. */
                    byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_END;

                    /* Its target is a BB start, and any previous instruction
                     * we already passed needs marking as a BB end. */
                    byte_to_ins_flags[target] |= MVM_CFG_BB_START;
                    if (target > 0 && target < pc - g->bytecode) {
                        while (!byte_to_ins_flags[--target]);
                        byte_to_ins_flags[target] |= MVM_CFG_BB_END;
                    }

                    /* Next instruction is also a BB start. */
                    next_bbs = 1;

                    arg_size += 4;
                    break;
                }
                case MVM_operand_spesh_slot:
                    ins_node->operands[i].lit_i16 = GET_I16(args, arg_size);
                    arg_size += 2;
                    break;
                default:
                    MVM_exception_throw_adhoc(tc,
                        "Spesh: unknown operand type %d in graph building (op %s)",
                        (int)type, ins_node->info->name);
                }
            }
            break;
            }
        }

        /* We specially handle the jumplist case, which needs to mark all of
         * the possible places we could jump to in the following instructions
         * as starts of basic blocks. It is, in itself, the end of one. Note
         * we jump to the instruction after the n jump points if none match,
         * so that is marked too. */
        if (opcode == MVM_OP_jumplist) {
            MVMint64 n = MVM_BC_get_I64(args, 0);
            for (i = 0; i <= n; i++)
                byte_to_ins_flags[(pc - g->bytecode) + 12 + i * 6] |= MVM_CFG_BB_START;
            byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_END;
        }

        /* Invocations, returns, and throws are basic block ends. */
        switch (opcode) {
        case MVM_OP_invoke_v:
        case MVM_OP_invoke_i:
        case MVM_OP_invoke_n:
        case MVM_OP_invoke_s:
        case MVM_OP_invoke_o:
        case MVM_OP_return_i:
        case MVM_OP_return_n:
        case MVM_OP_return_s:
        case MVM_OP_return_o:
        case MVM_OP_return:
        case MVM_OP_throwdyn:
        case MVM_OP_throwlex:
        case MVM_OP_throwlexotic:
        case MVM_OP_throwcatdyn:
        case MVM_OP_throwcatlex:
        case MVM_OP_throwcatlexotic:
        case MVM_OP_die:
        case MVM_OP_rethrow:
        case MVM_OP_resume:
            byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_END;
            next_bbs = 1;
            break;
        }

        /* Final instruction is basic block end. */
        if (pc + 2 + arg_size == end)
            byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_END;

        /* Caculate next instruction's PC. */
        pc += 2 + arg_size;

        /* If this is a deopt point opcode... */
        if (!existing_deopts && (info->deopt_point & MVM_DEOPT_MARK_ONE))
            add_deopt_annotation(tc, g, ins_node, pc, MVM_SPESH_ANN_DEOPT_ONE_INS);
        if (!existing_deopts && (info->deopt_point & MVM_DEOPT_MARK_ALL))
            add_deopt_annotation(tc, g, ins_node, pc, MVM_SPESH_ANN_DEOPT_ALL_INS);
        if (!existing_deopts && (info->deopt_point & MVM_DEOPT_MARK_OSR))
            add_deopt_annotation(tc, g, ins_node, pc, MVM_SPESH_ANN_DEOPT_OSR);

        /* Go to next instruction. */
        ins_idx++;
    }

    /* Annotate instructions that are handler-significant. */
    for (i = 0; i < g->num_handlers; i++) {
        MVMSpeshIns *start_ins = ins_flat[byte_to_ins_flags[g->handlers[i].start_offset] >> 2];
        MVMSpeshIns *end_ins   = ins_flat[byte_to_ins_flags[g->handlers[i].end_offset] >> 2];
        MVMSpeshIns *goto_ins  = ins_flat[byte_to_ins_flags[g->handlers[i].goto_offset] >> 2];
        MVMSpeshAnn *start_ann = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
        MVMSpeshAnn *end_ann   = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
        MVMSpeshAnn *goto_ann  = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));

        start_ann->next = start_ins->annotations;
        start_ann->type = MVM_SPESH_ANN_FH_START;
        start_ann->data.frame_handler_index = i;
        start_ins->annotations = start_ann;

        end_ann->next = end_ins->annotations;
        end_ann->type = MVM_SPESH_ANN_FH_END;
        end_ann->data.frame_handler_index = i;
        end_ins->annotations = end_ann;

        goto_ann->next = goto_ins->annotations;
        goto_ann->type = MVM_SPESH_ANN_FH_GOTO;
        goto_ann->data.frame_handler_index = i;
        goto_ins->annotations = goto_ann;
    }

    /* Annotate instructions that are inline start/end points. */
    for (i = 0; i < g->num_inlines; i++) {
        MVMSpeshIns *start_ins = ins_flat[byte_to_ins_flags[g->inlines[i].start] >> 2];
        MVMSpeshIns *end_ins   = ins_flat[byte_to_ins_flags[g->inlines[i].end] >> 2];
        MVMSpeshAnn *start_ann = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
        MVMSpeshAnn *end_ann   = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));

        start_ann->next = start_ins->annotations;
        start_ann->type = MVM_SPESH_ANN_INLINE_START;
        start_ann->data.inline_idx = i;
        start_ins->annotations = start_ann;

        end_ann->next = end_ins->annotations;
        end_ann->type = MVM_SPESH_ANN_INLINE_END;
        end_ann->data.inline_idx = i;
        end_ins->annotations = end_ann;
    }

    /* Now for the second pass, where we assemble the basic blocks. Also we
     * build a lookup table of instructions that start a basic block to that
     * basic block, for the final CFG construction. We make the entry block a
     * special one, containing a noop; it will have any exception handler
     * targets linked from it, so they show up in the graph. */
    g->entry                  = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB));
    g->entry->first_ins       = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    g->entry->first_ins->info = get_op_info(tc, cu, 0);
    g->entry->last_ins        = g->entry->first_ins;
    g->entry->idx             = 0;
    cur_bb                    = NULL;
    prev_bb                   = g->entry;
    last_ins                  = NULL;
    ins_to_bb                 = MVM_calloc(ins_idx, sizeof(MVMSpeshBB *));
    ins_idx                   = 0;
    bb_idx                    = 1;
    for (i = 0; i < g->bytecode_size; i++) {
        MVMSpeshIns *cur_ins;

        /* Skip zeros; no instruction here. */
        if (!byte_to_ins_flags[i])
            continue;

        /* Get current instruction. */
        cur_ins = ins_flat[byte_to_ins_flags[i] >> 2];

        /* Start of a basic block? */
        if (byte_to_ins_flags[i] & MVM_CFG_BB_START) {
            /* Should not already be in a basic block. */
            if (cur_bb) {
                MVM_spesh_graph_destroy(tc, g);
                MVM_exception_throw_adhoc(tc, "Spesh: confused during basic block analysis (in block)");
            }

            /* Create it, and set first instruction and index. */
            cur_bb = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB));
            cur_bb->first_ins = cur_ins;
            cur_bb->idx = bb_idx;
            cur_bb->initial_pc = i;
            bb_idx++;

            /* Record instruction -> BB start mapping. */
            ins_to_bb[ins_idx] = cur_bb;

            /* Link it to the previous one. */
            prev_bb->linear_next = cur_bb;
        }

        /* Should always be in a BB at this point. */
        if (!cur_bb) {
            MVM_spesh_graph_destroy(tc, g);
            MVM_exception_throw_adhoc(tc, "Spesh: confused during basic block analysis (no block)");
        }

        /* Add instruction into double-linked per-block instruction list. */
        if (last_ins) {
            last_ins->next = cur_ins;
            cur_ins->prev = last_ins;
        }
        last_ins = cur_ins;

        /* End of a basic block? */
        if (byte_to_ins_flags[i] & MVM_CFG_BB_END) {
            cur_bb->last_ins = cur_ins;
            prev_bb  = cur_bb;
            cur_bb   = NULL;
            last_ins = NULL;
        }

        ins_idx++;
    }
    g->num_bbs = bb_idx;

    /* Finally, link the basic blocks up to form a CFG. Along the way, any of
     * the instruction operands get the target BB stored. */
    cur_bb = g->entry;
    while (cur_bb) {
        /* If it's the first block, it's a special case; successors are the
         * real successor and all exception handlers. */
        if (cur_bb == g->entry) {
            cur_bb->num_succ = 1 + g->num_handlers;
            cur_bb->succ     = MVM_spesh_alloc(tc, g, cur_bb->num_succ * sizeof(MVMSpeshBB *));
            cur_bb->succ[0]  = cur_bb->linear_next;
            for (i = 0; i < g->num_handlers; i++) {
                MVMuint32 offset = g->handlers[i].goto_offset;
                cur_bb->succ[i + 1] = ins_to_bb[byte_to_ins_flags[offset] >> 2];
            }
        }

        /* Otherwise, consider the last instruction, to see how we leave the BB. */
        else {
            switch (cur_bb->last_ins->info->opcode) {
                case MVM_OP_jumplist: {
                    /* Jumplist, so successors are next N+1 basic blocks. */
                    MVMint64    num_bbs   = cur_bb->last_ins->operands[0].lit_i64 + 1;
                    MVMSpeshBB *bb_to_add = cur_bb->linear_next;
                    cur_bb->succ          = MVM_spesh_alloc(tc, g, num_bbs * sizeof(MVMSpeshBB *));
                    for (i = 0; i < num_bbs; i++) {
                        cur_bb->succ[i] = bb_to_add;
                        bb_to_add = bb_to_add->linear_next;
                    }
                    cur_bb->num_succ = num_bbs;
                }
                break;
                case MVM_OP_goto: {
                    /* Unconditional branch, so one successor. */
                    MVMuint32   offset = cur_bb->last_ins->operands[0].ins_offset;
                    MVMSpeshBB *tgt    = ins_to_bb[byte_to_ins_flags[offset] >> 2];
                    cur_bb->succ       = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB *));
                    cur_bb->succ[0]    = tgt;
                    cur_bb->num_succ   = 1;
                    cur_bb->last_ins->operands[0].ins_bb = tgt;
                }
                break;
                default: {
                    /* Probably conditional branch, so two successors: one from
                     * the instruction, another from fall-through. Or may just be
                     * a non-branch that exits for other reasons. */
                    cur_bb->succ = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshBB *));
                    for (i = 0; i < cur_bb->last_ins->info->num_operands; i++) {
                        if (cur_bb->last_ins->info->operands[i] == MVM_operand_ins) {
                            MVMuint32 offset = cur_bb->last_ins->operands[i].ins_offset;
                            cur_bb->succ[0] = ins_to_bb[byte_to_ins_flags[offset] >> 2];
                            cur_bb->num_succ++;
                            cur_bb->last_ins->operands[i].ins_bb = cur_bb->succ[0];
                        }
                    }
                    if (cur_bb->num_succ > 1) {
                        /* If we ever get instructions with multiple targets, this
                         * area of the code needs an update. */
                        MVM_spesh_graph_destroy(tc, g);
                        MVM_exception_throw_adhoc(tc, "Spesh: unhandled multi-target branch");
                    }
                    if (cur_bb->linear_next) {
                        cur_bb->succ[cur_bb->num_succ] = cur_bb->linear_next;
                        cur_bb->num_succ++;
                    }
                }
                break;
            }
        }

        /* Move on to next block. */
        cur_bb = cur_bb->linear_next;
    }

    /* If we're building the graph for optimized bytecode, insert existing
     * deopt points. */
    if (existing_deopts) {
        for (i = 0; i < num_existing_deopts; i ++) {
            if (existing_deopts[2 * i + 1] >= 0) {
                MVMSpeshIns *post_ins     = ins_flat[byte_to_ins_flags[existing_deopts[2 * i + 1]] >> 2];
                MVMSpeshIns *deopt_ins    = post_ins->prev ? post_ins->prev :
                    MVM_spesh_graph_linear_prev(tc, g,
                        ins_to_bb[byte_to_ins_flags[existing_deopts[2 * i + 1]] >> 2])->last_ins;
                MVMSpeshAnn *deopt_ann    = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
                deopt_ann->next           = deopt_ins->annotations;
                deopt_ann->type           = MVM_SPESH_ANN_DEOPT_INLINE;
                deopt_ann->data.deopt_idx = i;
                deopt_ins->annotations    = deopt_ann;
            }
        }
    }

    /* Clear up the temporary arrays. */
    MVM_free(byte_to_ins_flags);
    MVM_free(ins_flat);
    MVM_free(ins_to_bb);
}

/* Eliminates any unreachable basic blocks (that is, dead code). Not having
 * to consider them any further simplifies all that follows. */
static void eliminate_dead(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Iterate to fixed point. */
    MVMint8  *seen     = MVM_malloc(g->num_bbs);
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
    MVM_free(seen);

    /* If we removed some, need to re-number so they're consecutive, for the
     * post-order and dominance calcs to be happy. */
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

/* Annotates the control flow graph with predecessors. */
static void add_predecessors(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *cur_bb = g->entry;
    while (cur_bb) {
        MVMuint16 i;
        for (i = 0; i < cur_bb->num_succ; i++) {
            MVMSpeshBB  *tgt = cur_bb->succ[i];
            MVMSpeshBB **new_pred = MVM_spesh_alloc(tc, g,
                (tgt->num_pred + 1) * sizeof(MVMSpeshBB *));
            memcpy(new_pred, tgt->pred, tgt->num_pred * sizeof(MVMSpeshBB *));
            new_pred[tgt->num_pred] = cur_bb;
            tgt->pred = new_pred;
            tgt->num_pred++;
        }
        cur_bb = cur_bb->linear_next;
    }
}

/* Produces an array of the basic blocks, sorted in reverse postorder from
 * the entry point. */
static void dfs(MVMSpeshBB **rpo, MVMint32 *insert_pos, MVMuint8 *seen, MVMSpeshBB *bb) {
    MVMint32 i;
    seen[bb->idx] = 1;
    for (i = 0; i < bb->num_succ; i++) {
        MVMSpeshBB *succ = bb->succ[i];
        if (!seen[succ->idx])
            dfs(rpo, insert_pos, seen, succ);
    }
    rpo[*insert_pos] = bb;
    bb->rpo_idx = *insert_pos;
    (*insert_pos)--;
}
static MVMSpeshBB ** reverse_postorder(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB **rpo  = MVM_calloc(g->num_bbs, sizeof(MVMSpeshBB *));
    MVMuint8    *seen = MVM_calloc(g->num_bbs, 1);
    MVMint32     ins  = g->num_bbs - 1;
    dfs(rpo, &ins, seen, g->entry);
    MVM_free(seen);
    if (ins != -1) {
        printf("%s", MVM_spesh_dump(tc, g));
        MVM_spesh_graph_destroy(tc, g);
        MVM_exception_throw_adhoc(tc, "Spesh: reverse postorder calculation failed");
    }
    return rpo;
}

/* 2-finger intersection algorithm, to find new immediate dominator. */
static void iter_check(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo, MVMint32 *doms, MVMint32 iters) {
    if (iters > 100000) {
#ifdef NDEBUG
        MVMint32 k;
        printf("%s", MVM_spesh_dump(tc, g));
        printf("RPO: ");
        for (k = 0; k < g->num_bbs; k++)
            printf("%d, ", rpo[k]->idx);
        printf("\n");
        printf("Doms: ");
        for (k = 0; k < g->num_bbs; k++)
            printf("%d (%d), ", doms[k], doms[k] >= 0 ? rpo[doms[k]]->idx : -1);
        printf("\n");
#endif
        MVM_spesh_graph_destroy(tc, g);
        MVM_exception_throw_adhoc(tc, "Spesh: dominator intersection went infinite");
    }
}
static MVMint32 intersect(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo, MVMint32 *doms, MVMint32 finger1, MVMint32 finger2) {
    MVMint32 iters = 0;
    while (finger1 != finger2) {
        while (finger1 > finger2) {
            iter_check(tc, g, rpo, doms, iters++);
            finger1 = doms[finger1];
        }
        while (finger2 > finger1) {
            iter_check(tc, g, rpo, doms, iters++);
            finger2 = doms[finger2];
        }
    }
    return finger1;
}

/* Computes dominator information about the basic blocks. */
static MVMint32 * compute_dominators(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo) {
    MVMint32 i, j, changed;

    /* Create result list, with all initialized to undefined (use -1, as it's
     * not a valid basic block index). Start node dominates itself. */
    MVMint32 *doms = MVM_malloc(g->num_bbs * sizeof(MVMint32));
    doms[0] = 0;
    for (i = 1; i < g->num_bbs; i++)
        doms[i] = -1;

    /* Iterate to fixed point. */
    changed = 1;
    while (changed) {
        changed = 0;

        /* Visit all except the start node in reverse postorder. */
        for (i = 1; i < g->num_bbs; i++) {
            MVMSpeshBB *b = rpo[i];

            /* See if there's a better dominator. */
            MVMint32 chosen_pred = -1;
            MVMint32 new_idom;
            for (j = 0; j < b->num_pred; j++) {
                new_idom = b->pred[j]->rpo_idx;
                if (doms[new_idom] != -1)
                {
                    chosen_pred = j;
                    break;
                }
            }
            if (chosen_pred == -1) {
                MVM_spesh_graph_destroy(tc, g);
                MVM_exception_throw_adhoc(tc, "Spesh: could not find processed initial dominator");
            }
            for (j = 0; j < b->num_pred; j++) {
                if (j != chosen_pred) {
                    MVMint32 p_idx = b->pred[j]->rpo_idx;
                    if (doms[p_idx] != -1)
                        new_idom = intersect(tc, g, rpo, doms, p_idx, new_idom);
                }
            }
            if (doms[i] != new_idom) {
                doms[i] = new_idom;
                changed = 1;
            }
        }
    }

    return doms;
}

/* Builds the dominance tree children lists for each node. */
static void add_child(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *target, MVMSpeshBB *to_add) {
    MVMSpeshBB **new_children;
    MVMint32 i;

    /* Already in the child list? */
    for (i = 0; i < target->num_children; i++)
        if (target->children[i] == to_add)
            return;

    /* Nope, so insert. */
    new_children = MVM_spesh_alloc(tc, g, (target->num_children + 1) * sizeof(MVMSpeshBB *));
    memcpy(new_children, target->children, target->num_children * sizeof(MVMSpeshBB *));
    new_children[target->num_children] = to_add;
    target->children = new_children;
    target->num_children++;
}
static void add_children(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo, MVMint32 *doms) {
    MVMint32 i;
    for (i = 0; i < g->num_bbs; i++) {
        MVMSpeshBB *bb   = rpo[i];
        MVMint32    idom = doms[i];
        if (idom != i)
            add_child(tc, g, rpo[idom], bb);
    }
}

/* Builds the dominance frontier set for each node. */
static void add_to_frontier_set(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *target, MVMSpeshBB *to_add) {
    MVMSpeshBB **new_df;
    MVMint32 i;

    /* Already in the set? */
    for (i = 0; i < target->num_df; i++)
        if (target->df[i] == to_add)
            return;

    /* Nope, so insert. */
    new_df = MVM_spesh_alloc(tc, g, (target->num_df + 1) * sizeof(MVMSpeshBB *));
    memcpy(new_df, target->df, target->num_df * sizeof(MVMSpeshBB *));
    new_df[target->num_df] = to_add;
    target->df = new_df;
    target->num_df++;
}
static void add_dominance_frontiers(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo, MVMint32 *doms) {
    MVMint32 j;
    MVMSpeshBB *b = g->entry;
    while (b) {
        if (b->num_pred >= 2) { /* Thus it's a join point */
            for (j = 0; j < b->num_pred; j++) {
                MVMint32 runner      = b->pred[j]->rpo_idx;
                MVMint32 finish_line = doms[b->rpo_idx];
                while (runner != finish_line) {
                    add_to_frontier_set(tc, g, rpo[runner], b);
                    runner = doms[runner];
                }
            }
        }
        b = b->linear_next;
    }
}

/* Per-local SSA info. */
typedef struct {
    /* Nodes that assign to the variable. */
    MVMSpeshBB **ass_nodes;
    MVMuint16    num_ass_nodes;

    /* Count of processed assignments aka. C(V). */
    MVMint32 count;

    /* Stack of integers aka. S(V). */
    MVMint32 *stack;
    MVMint32  stack_top;
    MVMint32  stack_alloc;
} SSAVarInfo;

/* Creates an SSAVarInfo for each local, initializing it with a list of nodes
 * that assign to the local. */
static SSAVarInfo * initialize_ssa_var_info(MVMThreadContext *tc, MVMSpeshGraph *g) {
    SSAVarInfo *var_info = MVM_calloc(sizeof(SSAVarInfo), g->num_locals);
    MVMint32 i;

    /* Visit all instructions, looking for local writes. */
    MVMSpeshBB *bb = g->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            for (i = 0; i < ins->info->num_operands; i++) {
                if ((ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
                    MVMuint16 written = ins->operands[i].reg.orig;
                    MVMint32  found   = 0;
                    MVMint32  j;
                    for (j = 0; j < var_info[written].num_ass_nodes; j++)
                        if (var_info[written].ass_nodes[j] == bb) {
                            found = 1;
                            break;
                        }
                    if (!found) {
                        if (var_info[written].num_ass_nodes % 8 == 0) {
                            MVMint32 new_size = var_info[written].num_ass_nodes + 8;
                            var_info[written].ass_nodes = MVM_realloc(
                                var_info[written].ass_nodes,
                                new_size * sizeof(MVMSpeshBB *));
                        }
                        var_info[written].ass_nodes[var_info[written].num_ass_nodes] = bb;
                        var_info[written].num_ass_nodes++;
                    }
                }
            }
            ins = ins->next;
        }
        bb = bb->linear_next;
    }

    /* Set stack top to -1 sentinel for all nodes, and count = 1 (as we may
     * read the default value of a register). */
    for (i = 0; i < g->num_locals; i++) {
        var_info[i].count     = 1;
        var_info[i].stack_top = -1;
    }

    return var_info;
}

MVMOpInfo *get_phi(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint32 nrargs) {
    MVMOpInfo *result = NULL;

    /* Check number of args to phi isn't huge. */
    if (nrargs > 0xFFFF)
        MVM_panic(1, "Spesh: SSA calculation failed; cannot allocate enormous PHI node");

    /* Up to 64 args, almost every number is represented, but after that
     * we have a sparse array through which we must search */
    if (nrargs - 2 < MVMPhiNodeCacheSparseBegin) {
        result = &g->phi_infos[nrargs - 2];
    } else {
        MVMint32 cache_idx;

        for (cache_idx = MVMPhiNodeCacheSparseBegin; !result && cache_idx < MVMPhiNodeCacheSize; cache_idx++) {
            if (g->phi_infos[cache_idx].opcode == MVM_SSA_PHI) {
                if (g->phi_infos[cache_idx].num_operands == nrargs) {
                    result = &g->phi_infos[cache_idx];
                }
            } else {
                result = &g->phi_infos[cache_idx];
            }
        }
    }

    if (result == NULL) {
        result = MVM_spesh_alloc(tc, g, sizeof(MVMOpInfo));
        result->opcode = 0;
    }

    if (result->opcode != MVM_SSA_PHI) {
        result->opcode       = MVM_SSA_PHI;
        result->name         = "PHI";
        result->num_operands = nrargs;
    }

    return result;
}

/* Inserts SSA phi functions at the required places in the graph. */
static void place_phi(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMint32 n, MVMuint16 var) {
    MVMint32     i;
    MVMOpInfo   *phi_op  = get_phi(tc, g, n + 1);
    MVMSpeshIns *ins     = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    ins->info            = phi_op;
    ins->operands        = MVM_spesh_alloc(tc, g, phi_op->num_operands * sizeof(MVMSpeshOperand));
    for (i = 0; i < phi_op->num_operands; i++)
        ins->operands[i].reg.orig = var;
    ins->next           = bb->first_ins;
    bb->first_ins->prev = ins;
    bb->first_ins       = ins;
}
static void insert_phi_functions(MVMThreadContext *tc, MVMSpeshGraph *g, SSAVarInfo *var_info) {
    MVMint32    *has_already  = MVM_calloc(g->num_bbs, sizeof(MVMint32));
    MVMint32    *work         = MVM_calloc(g->num_bbs, sizeof(MVMint32));
    MVMSpeshBB **worklist     = MVM_calloc(g->num_bbs, sizeof(MVMSpeshBB *));
    MVMint32     worklist_top = 0;
    MVMint32     iter_count   = 0;

    /* Go over all locals. */
    MVMint32 var, i, j, found;
    for (var = 0; var < g->num_locals; var++) {
        /* Move to next iteration. */
        iter_count++;

        /* Add blocks assigning to this variable to the worklist. */
        for (i = 0; i < var_info[var].num_ass_nodes; i++) {
            MVMSpeshBB *bb = var_info[var].ass_nodes[i];
            work[bb->idx] = iter_count;
            worklist[worklist_top++] = bb; /* Algo unions, but ass_nodes unique */
        }

        /* Process the worklist. */
        while (worklist_top) {
            MVMSpeshBB *x = worklist[--worklist_top];
            for (i = 0; i < x->num_df; i++) {
                MVMSpeshBB *y = x->df[i];
                if (has_already[y->idx] < iter_count) {
                    /* Place phi function, and mark we have. */
                    place_phi(tc, g, y, y->num_pred, var);
                    has_already[y->idx] = iter_count;

                    /* Add this block to worklist if needed. */
                    if (work[y->idx] < iter_count) {
                        work[y->idx] = iter_count;
                        found = 0;
                        for (j = 0; j < worklist_top; j++)
                            if (worklist[j] == y) {
                                found = 1;
                                break;
                            }
                        if (!found)
                            worklist[worklist_top++] = y;
                    }
                }
            }
        }
    }

    MVM_free(has_already);
    MVM_free(work);
    MVM_free(worklist);
}

/* Renames the local variables such that we end up with SSA form. */
static MVMint32 which_pred(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *y, MVMSpeshBB *x) {
    MVMint32 i;
    for (i = 0; i < y->num_pred; i++)
        if (y->pred[i] == x)
            return i;
    MVM_spesh_graph_destroy(tc, g);
    MVM_exception_throw_adhoc(tc, "Spesh: which_pred failed to find x");
}
static void rename_locals(MVMThreadContext *tc, MVMSpeshGraph *g, SSAVarInfo *var_info, MVMSpeshBB *x) {
    MVMint32 i;

    /* Visit instructions and do renames in normal (non-phi) instructions. */
    MVMSpeshIns *a = x->first_ins;
    while (a) {
        /* Rename reads, provided it's not a PHI. */
        MVMint32 is_phi = a->info->opcode == MVM_SSA_PHI;
        if (!is_phi) {
            for (i = 0; i < a->info->num_operands; i++) {
                if ((a->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg) {
                    MVMuint16 orig = a->operands[i].reg.orig;
                    MVMint32  st   = var_info[orig].stack_top;
                    if (st >= 0)
                        a->operands[i].reg.i = var_info[orig].stack[st];
                    else
                        a->operands[i].reg.i = 0;
                }
            }
        }

        /* Rename writes. */
        for (i = 0; i < a->info->num_operands; i++) {
            if (is_phi || (a->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
                MVMuint16 orig = a->operands[i].reg.orig;
                MVMint32 reg_i = var_info[orig].count;
                a->operands[i].reg.i = reg_i;
                if (var_info[orig].stack_top + 1 >= var_info[orig].stack_alloc) {
                    if (var_info[orig].stack_alloc)
                        var_info[orig].stack_alloc *= 2;
                    else
                        var_info[orig].stack_alloc = 8;
                    var_info[orig].stack = MVM_realloc(var_info[orig].stack,
                        var_info[orig].stack_alloc * sizeof(MVMint32));
                }
                var_info[orig].stack[++var_info[orig].stack_top] = reg_i;
                var_info[orig].count++;
            }
            if (is_phi)
                break;
        }

        a = a->next;
    }

    /* Visit successors and update their phi functions. */
    for (i = 0; i < x->num_succ; i++) {
        MVMSpeshBB  *y = x->succ[i];
        MVMint32     j = which_pred(tc, g, y, x);
        MVMSpeshIns *p = y->first_ins;
        while (p && p->info->opcode == MVM_SSA_PHI) {
            MVMuint16 orig = p->operands[j + 1].reg.orig;
            MVMint32  st   = var_info[orig].stack_top;
            if (st >= 0)
                p->operands[j + 1].reg.i = var_info[orig].stack[st];
            else
                p->operands[j + 1].reg.i = 0;
            p = p->next;
        }
    }

    /* Rename for all the children in the dominator tree. */
    for (i = 0; i < x->num_children; i++)
        rename_locals(tc, g, var_info, x->children[i]);

    /* Go over assignments and pop new variable names. */
    a = x->first_ins;
    while (a) {
        MVMint32 is_phi = a->info->opcode == MVM_SSA_PHI;
        for (i = 0; i < a->info->num_operands; i++) {
            if (is_phi || (a->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
                MVMuint16 orig = a->operands[i].reg.orig;
                var_info[orig].stack_top--;
            }
            if (is_phi)
                break;
        }
        a = a->next;
    }
}

/* Transforms a spesh graph into SSA form. After this, the graph will have all
 * register accesses given an SSA "version", and phi instructions inserted as
 * needed. */
static void ssa(MVMThreadContext *tc, MVMSpeshGraph *g) {
    SSAVarInfo *var_info;
    MVMint32 i, num_locals;

    /* Compute dominance frontiers. */
    MVMSpeshBB **rpo  = reverse_postorder(tc, g);
    MVMint32    *doms = compute_dominators(tc, g, rpo);
    add_children(tc, g, rpo, doms);
    add_dominance_frontiers(tc, g, rpo, doms);
    MVM_free(rpo);
    MVM_free(doms);

    /* Initialize per-local data for SSA analysis. */
    var_info = initialize_ssa_var_info(tc, g);

    /* Compute SSA itself. */
    insert_phi_functions(tc, g, var_info);
    rename_locals(tc, g, var_info, g->entry);

    /* Allocate space for spesh facts for each local; clean up stacks while
     * we're at it. */
    num_locals     = g->num_locals;
    g->facts       = MVM_spesh_alloc(tc, g, num_locals * sizeof(MVMSpeshFacts *));
    g->fact_counts = MVM_spesh_alloc(tc, g, num_locals * sizeof(MVMuint16));
    for (i = 0; i < num_locals; i++) {
        g->fact_counts[i] = var_info[i].count;
        g->facts[i]       = MVM_spesh_alloc(tc, g, var_info[i].count * sizeof(MVMSpeshFacts));
        if (var_info[i].stack_alloc) {
            MVM_free(var_info[i].stack);
            MVM_free(var_info[i].ass_nodes);
        }
    }
    MVM_free(var_info);
}

/* Takes a static frame and creates a spesh graph for it. */
MVMSpeshGraph * MVM_spesh_graph_create(MVMThreadContext *tc, MVMStaticFrame *sf, MVMuint32 cfg_only) {
    /* Create top-level graph object. */
    MVMSpeshGraph *g = MVM_calloc(1, sizeof(MVMSpeshGraph));
    g->sf            = sf;
    g->bytecode      = sf->body.bytecode;
    g->bytecode_size = sf->body.bytecode_size;
    g->handlers      = sf->body.handlers;
    g->num_handlers  = sf->body.num_handlers;
    g->num_locals    = sf->body.num_locals;
    g->num_lexicals  = sf->body.num_lexicals;
    g->phi_infos     = MVM_spesh_alloc(tc, g, MVMPhiNodeCacheSize * sizeof(MVMOpInfo));

    /* Ensure the frame is validated, since we'll rely on this. */
    if (sf->body.instrumentation_level == 0) {
        MVM_spesh_graph_destroy(tc, g);
        MVM_exception_throw_adhoc(tc, "Spesh: cannot build CFG from unvalidated frame");
    }

    /* Build the CFG out of the static frame, and transform it to SSA. */
    build_cfg(tc, g, sf, NULL, 0);
    if (!cfg_only) {
        eliminate_dead(tc, g);
        add_predecessors(tc, g);
        ssa(tc, g);
    }

    /* Hand back the completed graph. */
    return g;
}

/* Takes a static frame and creates a spesh graph for it. */
MVMSpeshGraph * MVM_spesh_graph_create_from_cand(MVMThreadContext *tc, MVMStaticFrame *sf,
                                                 MVMSpeshCandidate *cand, MVMuint32 cfg_only) {
    /* Create top-level graph object. */
    MVMSpeshGraph *g     = MVM_calloc(1, sizeof(MVMSpeshGraph));
    g->sf                = sf;
    g->bytecode          = cand->bytecode;
    g->bytecode_size     = cand->bytecode_size;
    g->handlers          = cand->handlers;
    g->num_handlers      = sf->body.num_handlers;
    g->num_locals        = cand->num_locals;
    g->num_lexicals      = cand->num_lexicals;
    g->inlines           = cand->inlines;
    g->num_inlines       = cand->num_inlines;
    g->deopt_addrs       = cand->deopts;
    g->num_deopt_addrs   = cand->num_deopts;
    g->alloc_deopt_addrs = cand->num_deopts;
    g->local_types       = cand->local_types;
    g->lexical_types     = cand->lexical_types;
    g->spesh_slots       = cand->spesh_slots;
    g->num_spesh_slots   = cand->num_spesh_slots;
    g->phi_infos         = MVM_spesh_alloc(tc, g, MVMPhiNodeCacheSize * sizeof(MVMOpInfo));

    /* Ensure the frame is validated, since we'll rely on this. */
    if (sf->body.instrumentation_level == 0) {
        MVM_spesh_graph_destroy(tc, g);
        MVM_exception_throw_adhoc(tc, "Spesh: cannot build CFG from unvalidated frame");
    }

    /* Build the CFG out of the static frame, and transform it to SSA. */
    build_cfg(tc, g, sf, cand->deopts, cand->num_deopts);
    if (!cfg_only) {
        eliminate_dead(tc, g);
        add_predecessors(tc, g);
        ssa(tc, g);
    }

    /* Hand back the completed graph. */
    return g;
}

/* Marks GCables held in a spesh graph. */
void MVM_spesh_graph_mark(MVMThreadContext *tc, MVMSpeshGraph *g, MVMGCWorklist *worklist) {
    MVMuint16 i, j, num_locals, num_facts, *local_types;

    /* Mark static frame. */
    MVM_gc_worklist_add(tc, worklist, &g->sf);

    /* Mark facts. */
    num_locals = g->num_locals;
    local_types = g->local_types ? g->local_types : g->sf->body.local_types;
    for (i = 0; i < num_locals; i++) {
        num_facts = g->fact_counts[i];
        for (j = 0; j < num_facts; j++) {
            MVMint32 flags = g->facts[i][j].flags;
            if (flags & MVM_SPESH_FACT_KNOWN_TYPE)
                MVM_gc_worklist_add(tc, worklist, &(g->facts[i][j].type));
            if (flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE)
                MVM_gc_worklist_add(tc, worklist, &(g->facts[i][j].decont_type));
            if (flags & MVM_SPESH_FACT_KNOWN_VALUE) {
                if (local_types[i] == MVM_reg_obj)
                    MVM_gc_worklist_add(tc, worklist, &(g->facts[i][j].value.o));
                else if (local_types[i] == MVM_reg_str)
                    MVM_gc_worklist_add(tc, worklist, &(g->facts[i][j].value.s));
            }
        }
    }
}

/* Destroys a spesh graph, deallocating all its associated memory. */
void MVM_spesh_graph_destroy(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Free all of the allocated node memory. */
    MVMSpeshMemBlock *cur_block = g->mem_block;
    while (cur_block) {
        MVMSpeshMemBlock *prev = cur_block->prev;
        MVM_free(cur_block->buffer);
        MVM_free(cur_block);
        cur_block = prev;
    }

    /* Free the graph itself. */
    MVM_free(g);
}
