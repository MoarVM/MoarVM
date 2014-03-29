#include "moar.h"

/* This is where the spesh stuff all begins. The logic in here takes bytecode
 * and builds a spesh graph from it. This is a CFG in SSA form. Transforming
 * to SSA involves computing dominance frontiers, done by the algorithm found
 * in http://www.cs.rice.edu/~keith/EMBED/dom.pdf. */

#define GET_I8(pc, idx)     *((MVMint8 *)(pc + idx))
#define GET_UI8(pc, idx)    *((MVMuint8 *)(pc + idx))
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))
#define GET_I32(pc, idx)    *((MVMint32 *)(pc + idx))
#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))
#define GET_I64(pc, idx)    *((MVMint64 *)(pc + idx))
#define GET_UI64(pc, idx)   *((MVMuint64 *)(pc + idx))
#define GET_N32(pc, idx)    *((MVMnum32 *)(pc + idx))
#define GET_N64(pc, idx)    *((MVMnum64 *)(pc + idx))

/* Allocate a piece of memory from the spesh graph's buffer. Deallocated when
 * the spesh graph is. */
void * spesh_alloc(MVMThreadContext *tc, MVMSpeshGraph *g, size_t bytes) {
    char *result = NULL;
    if (g->mem_block) {
        MVMSpeshMemBlock *block = g->mem_block;
        if (block->alloc + bytes < block->limit) {
            result = block->alloc;
            block->alloc += bytes;
        }
    }
    if (!result) {
        /* No block, or block was full. Add another. */
        MVMSpeshMemBlock *block = malloc(sizeof(MVMSpeshMemBlock));
        block->buffer = calloc(MVM_SPESH_MEMBLOCK_SIZE, 1);
        block->alloc  = block->buffer;
        block->limit  = block->buffer + MVM_SPESH_MEMBLOCK_SIZE;
        block->prev   = g->mem_block;
        g->mem_block  = block;

        /* Now allocate out of it. */
        if (bytes > MVM_SPESH_MEMBLOCK_SIZE) {
            MVM_spesh_graph_destroy(tc, g);
            MVM_exception_throw_adhoc(tc, "spesh_alloc: requested oversized block");
        }
        result = block->alloc;
        block->alloc += bytes;
    }
    return result;
}

/* Looks up op info; doesn't sanity check, since we should be working on code
 * that already pass validation. */
static MVMOpInfo * get_op_info(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint16 opcode) {
    if (opcode < MVM_OP_EXT_BASE) {
        return MVM_op_get_op(opcode);
    }
    else {
        MVMuint16       index  = opcode - MVM_OP_EXT_BASE;
        MVMExtOpRecord *record = &cu->body.extops[index];
        return (MVMOpInfo *)MVM_ext_resolve_extop_record(tc, record);
    }
}

/* Builds the control flow graph, populating the passed spesh graph structure
 * with it. This also makes nodes for all of the instruction. */
#define MVM_CFG_BB_START    1
#define MVM_CFG_BB_END      2
static void build_cfg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMStaticFrame *sf) {
    MVMSpeshBB  *cur_bb, *prev_bb;
    MVMSpeshIns *last_ins;
    MVMint64     i;
    MVMint32     bb_idx;

    /* Temporary array of all MVMSpeshIns we create (one per instruction).
     * Overestimate at size. Has the flat view, matching the bytecode. */
    MVMSpeshIns **ins_flat = calloc(sf->body.bytecode_size / 2, sizeof(MVMSpeshIns *));

    /* Temporary array where each byte in the input bytecode gets a 32-bit
     * integer. This is used for two things:
     * A) When we make the MVMSpeshIns for an instruction starting at the
     *    byte, we put the instruction index (into ins_flat) in the slot,
     *    shifting it by 2 bits to the left. We will use this to do fixups.
     * B) The first bit is "I have an incoming branch" - that is, start of
     *    a basic block. The second bit is "I can branch" - that is, end of
     *    a basic block. It's possible to have both bits set.
     * Anything that's just a zero has no instruction starting there. */
    MVMuint32 *byte_to_ins_flags = calloc(sf->body.bytecode_size, sizeof(MVMuint32));

    /* Instruction to basic block mapping. Initialized later. */
    MVMSpeshBB **ins_to_bb = NULL;

    /* Make first pass through the bytecode. In this pass, we make MVMSpeshIns
     * nodes for each instruction and set the start/end of block bits. Also
     * set handler targets as basic block starters. */
    MVMCompUnit *cu       = sf->body.cu;
    MVMuint8    *pc       = sf->body.bytecode;
    MVMuint8    *end      = sf->body.bytecode + sf->body.bytecode_size;
    MVMuint32    ins_idx  = 0;
    MVMuint8     next_bbs = 1; /* Next iteration (here, first) starts a BB. */
    for (i = 0; i < sf->body.num_handlers; i++)
        byte_to_ins_flags[sf->body.handlers[i].goto_offset] |= MVM_CFG_BB_START;
    while (pc < end) {
        /* Look up op info. */
        MVMuint16  opcode   = *(MVMuint16 *)pc;
        MVMuint8  *args     = pc + 2;
        MVMuint8   arg_size = 0;
        MVMOpInfo *info     = get_op_info(tc, cu, opcode);

        /* Create an instruction node, add it, and record its position. */
        MVMSpeshIns *ins_node = spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        ins_flat[ins_idx] = ins_node;
        byte_to_ins_flags[pc - sf->body.bytecode] |= ins_idx << 2;

        /* Did previous instruction end a basic block? */
        if (next_bbs) {
            byte_to_ins_flags[pc - sf->body.bytecode] |= MVM_CFG_BB_START;
            next_bbs = 0;
        }

        /* Also check we're not already a BB start due to being a branch
         * target, in which case we should ensure our prior is marked as
         * a BB end. */
        else {
            if (byte_to_ins_flags[pc - sf->body.bytecode] & MVM_CFG_BB_START) {
                MVMuint32 hunt = pc - sf->body.bytecode;
                while (!byte_to_ins_flags[--hunt]);
                byte_to_ins_flags[hunt] |= MVM_CFG_BB_END;
            }
        }

        /* Store opcode */
        ins_node->info = info;

        /* Go over operands. */
        ins_node->operands = spesh_alloc(tc, g, info->num_operands * sizeof(MVMSpeshOperand));
        for (i = 0; i < info->num_operands; i++) {
            MVMuint8 flags = info->operands[i];
            MVMuint8 rw    = flags & MVM_operand_rw_mask;
            switch (rw) {
            case MVM_operand_read_reg:
            case MVM_operand_write_reg:
                ins_node->operands[i].reg_orig = GET_UI16(args, arg_size);
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
                    ins_node->operands[i].lit_i64 = GET_I64(args, arg_size);
                    arg_size += 8;
                    break;
                case MVM_operand_num32:
                    ins_node->operands[i].lit_n32 = GET_N32(args, arg_size);
                    arg_size += 4;
                    break;
                case MVM_operand_num64:
                    ins_node->operands[i].lit_n64 = GET_N64(args, arg_size);
                    arg_size += 8;
                    break;
                case MVM_operand_callsite:
                    ins_node->operands[i].callsite = cu->body.callsites[GET_UI16(args, arg_size)];
                    arg_size += 2;
                    break;
                case MVM_operand_coderef:
                    ins_node->operands[i].coderef = (MVMCode *)
                        cu->body.coderefs[GET_UI16(args, arg_size)];
                    arg_size += 2;
                    break;
                case MVM_operand_str:
                    ins_node->operands[i].lit_str = cu->body.strings[GET_UI32(args, arg_size)];
                    arg_size += 4;
                    break;
                case MVM_operand_ins: {
                    /* Stash instruction offset. */
                    MVMuint32 target = GET_UI32(args, arg_size);
                    ins_node->operands[i].ins_offset = target;

                    /* This is a branching instruction, so it's a BB end. */
                    byte_to_ins_flags[pc - sf->body.bytecode] |= MVM_CFG_BB_END;

                    /* Its target is a BB start, and any previous instruction
                     * we already passed needs marking as a BB end. */
                    byte_to_ins_flags[target] |= MVM_CFG_BB_START;
                    if (target > 0 && target < pc - sf->body.bytecode) {
                        while (!byte_to_ins_flags[--target]);
                        byte_to_ins_flags[target] |= MVM_CFG_BB_END;
                    }

                    /* Next instruction is also a BB start. */
                    next_bbs = 1;

                    arg_size += 4;
                    break;
                }
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
            MVMint64 n = GET_I64(args, 0);
            for (i = 0; i <= n; i++)
                byte_to_ins_flags[(pc - sf->body.bytecode) + 12 + i * 6] |= MVM_CFG_BB_START;
            byte_to_ins_flags[pc - sf->body.bytecode] |= MVM_CFG_BB_END;
        }

        /* Final instruction is basic block end. */
        if (pc + 2 + arg_size == end)
            byte_to_ins_flags[pc - sf->body.bytecode] |= MVM_CFG_BB_END;

        /* Go to next instruction. */
        ins_idx++;
        pc += 2 + arg_size;
    }

    /* Now for the second pass, where we assemble the basic blocks. Also we
     * build a lookup table of instructions that start a basic block to that
     * basic block, for the final CFG construction. We make the entry block a
     * special one, containing a noop; it will have any exception handler
     * targets linked from it, so they show up in the graph. */
    g->entry                  = spesh_alloc(tc, g, sizeof(MVMSpeshBB));
    g->entry->first_ins       = spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    g->entry->first_ins->info = get_op_info(tc, cu, 0);
    g->entry->last_ins        = g->entry->first_ins;
    g->entry->idx             = 0;
    cur_bb                    = NULL;
    prev_bb                   = g->entry;
    last_ins                  = NULL;
    ins_to_bb                 = calloc(ins_idx, sizeof(MVMSpeshBB *));
    ins_idx                   = 0;
    bb_idx                    = 1;
    for (i = 0; i < sf->body.bytecode_size; i++) {
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
            cur_bb = spesh_alloc(tc, g, sizeof(MVMSpeshBB));
            cur_bb->first_ins = cur_ins;
            cur_bb->idx = bb_idx;
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
            cur_bb->num_succ = 1 + sf->body.num_handlers;
            cur_bb->succ     = spesh_alloc(tc, g, cur_bb->num_succ * sizeof(MVMSpeshBB *));
            cur_bb->succ[0]  = cur_bb->linear_next;
            for (i = 0; i < sf->body.num_handlers; i++) {
                MVMuint32 offset = sf->body.handlers[i].goto_offset;
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
                    cur_bb->succ          = spesh_alloc(tc, g, num_bbs * sizeof(MVMSpeshBB *));
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
                    cur_bb->succ       = spesh_alloc(tc, g, sizeof(MVMSpeshBB *));
                    cur_bb->succ[0]    = tgt;
                    cur_bb->num_succ   = 1;
                }
                break;
                default: {
                    /* Probably conditional branch, so two successors: one from
                     * the instruction, another from fall-through. Or may just be
                     * a non-branch that exits for other reasons. */
                    cur_bb->succ = spesh_alloc(tc, g, 2 * sizeof(MVMSpeshBB *));
                    for (i = 0; i < cur_bb->last_ins->info->num_operands; i++) {
                        if (cur_bb->last_ins->info->operands[i] == MVM_operand_ins) {
                            MVMuint32 offset = cur_bb->last_ins->operands[i].ins_offset;
                            cur_bb->succ[0] = ins_to_bb[byte_to_ins_flags[offset] >> 2];
                            cur_bb->num_succ++;
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

    /* Clear up the temporary arrays. */
    free(byte_to_ins_flags);
    free(ins_flat);
    free(ins_to_bb);
}

/* Eliminates any unreachable basic blocks (that is, dead code). Not having
 * to consider them any further simplifies all that follows. */
static void eliminate_dead(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Iterate to fixed point. */
    MVMint8  *seen     = malloc(g->num_bbs);
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
    free(seen);

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
            MVMSpeshBB **new_pred = spesh_alloc(tc, g,
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
    (*insert_pos)--;
}
static MVMSpeshBB ** reverse_postorder(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB **rpo  = calloc(g->num_bbs, sizeof(MVMSpeshBB *));
    MVMuint8    *seen = calloc(g->num_bbs, 1);
    MVMint32     ins  = g->num_bbs - 1;
    dfs(rpo, &ins, seen, g->entry);
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
static MVMint32 rpo_idx(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo, MVMSpeshBB *bb) {
    MVMint32 i;
    for (i = 0; i < g->num_bbs; i++)
        if (rpo[i] == bb)
            return i;
    MVM_spesh_graph_destroy(tc, g);
    MVM_exception_throw_adhoc(tc, "Spesh: could not find block in reverse postorder");
}
static MVMint32 * compute_dominators(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo) {
    MVMint32 i, j, changed;

    /* Create result list, with all initialized to undefined (use -1, as it's
     * not a valid basic block index). Start node dominates itself. */
    MVMint32 *doms = malloc(g->num_bbs * sizeof(MVMint32));
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
                new_idom = rpo_idx(tc, g, rpo, b->pred[j]);
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
                    MVMint32 p_idx = rpo_idx(tc, g, rpo, b->pred[j]);
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

/* Builds the dominance frontier set for each node. */
static void add_to_frontier_set(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *target, MVMSpeshBB *to_add) {
    MVMSpeshBB **new_df;
    MVMint32 i;

    /* Already in the set? */
    for (i = 0; i < target->num_df; i++)
        if (target->df[i] == to_add)
            return;

    /* Nope, so insert. */
    new_df = spesh_alloc(tc, g, (target->num_df + 1) * sizeof(MVMSpeshBB *));
    memcpy(new_df, target->df, target->num_df * sizeof(MVMSpeshBB *));
    new_df[target->num_df] = to_add;
    target->df = new_df;
    target->num_df++;
}
static void add_dominance_frontiers(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo, MVMint32 *doms) {
    MVMint32 i, j;
    MVMSpeshBB *b = g->entry;
    while (b) {
        if (b->num_pred >= 2) { /* Thus it's a join point */
            for (j = 0; j < b->num_pred; j++) {
                MVMint32 runner      = rpo_idx(tc, g, rpo, b->pred[j]);
                MVMint32 finish_line = doms[rpo_idx(tc, g, rpo, b)];
                while (runner != finish_line) {
                    add_to_frontier_set(tc, g, rpo[runner], b);
                    runner = doms[runner];
                }
            }
        }
        b = b->linear_next;
    }
}

/* Transforms a spesh graph into SSA form. After this, the graph will have all
 * register accesses given an SSA "version", and phi instructions inserted as
 * needed. */
static void ssa(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Compute dominance frontiers. */
    MVMSpeshBB **rpo  = reverse_postorder(tc, g);
    MVMint32    *doms = compute_dominators(tc, g, rpo);
    add_dominance_frontiers(tc, g, rpo, doms);

    /* XXX TODO */

    /* Clean up. */
    free(rpo);
    free(doms);
}

/* Takes a static frame and creates a spesh graph for it. */
MVMSpeshGraph * MVM_spesh_graph_create(MVMThreadContext *tc, MVMStaticFrame *sf) {
    /* Create top-level graph object. */
    MVMSpeshGraph *g = calloc(1, sizeof(MVMSpeshGraph));
    g->sf = sf;

    /* Ensure the frame is validated, since we'll rely on this. */
    if (!sf->body.invoked) {
        MVM_spesh_graph_destroy(tc, g);
        MVM_exception_throw_adhoc(tc, "Spesh: cannot build CFG from unvalidated frame");
    }

    /* Build the CFG out of the static frame, and transform it to SSA. */
    build_cfg(tc, g, sf);
    eliminate_dead(tc, g);
    add_predecessors(tc, g);
    ssa(tc, g);

    /* Hand back the completed graph. */
    return g;
}

/* Destroys a spesh graph, deallocating all its associated memory. */
void MVM_spesh_graph_destroy(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Free all of the allocated node memory. */
    MVMSpeshMemBlock *cur_block = g->mem_block;
    while (cur_block) {
        MVMSpeshMemBlock *prev = cur_block->prev;
        free(cur_block->buffer);
        free(cur_block);
        cur_block = prev;
    }

    /* Free the graph itself. */
    free(g);
}
