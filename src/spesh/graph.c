#include "moar.h"

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
     * nodes for each instruction and set the start/end of block bits. */
    MVMCompUnit *cu       = sf->body.cu;
    MVMuint8    *pc       = sf->body.bytecode;
    MVMuint8    *end      = sf->body.bytecode + sf->body.bytecode_size;
    MVMuint32    ins_idx  = 0;
    MVMuint8     next_bbs = 1; /* Next iteration (here, first) starts a BB. */
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
     * basic block, for the final CFG construction. */
    cur_bb    = NULL;
    prev_bb   = NULL;
    last_ins  = NULL;
    ins_to_bb = calloc(ins_idx, sizeof(MVMSpeshBB *));
    ins_idx   = 0;
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

            /* Create it, and set first instruction. */
            cur_bb = spesh_alloc(tc, g, sizeof(MVMSpeshBB));
            cur_bb->first_ins = cur_ins;

            /* Record instruction -> BB start mapping. */
            ins_to_bb[ins_idx] = cur_bb;

            /* If it's the first instruction, we have the entry BB. If not,
             * link it to the previous one. */
            if (prev_bb)
                prev_bb->linear_next = cur_bb;
            else
                g->entry = cur_bb;
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

    /* Finally, link the basic blocks up to form a CFG. Along the way, any of
     * the instruction operands get the target BB stored. */
    cur_bb = g->entry;
    while (cur_bb) {
        /* Consider the last instruction, to see how we leave the BB. */
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
                    MVM_exception_throw_adhoc(tc, "Spesh: missing instruction in conditional branch");
                }
                if (cur_bb->linear_next) {
                    cur_bb->succ[cur_bb->num_succ] = cur_bb->linear_next;
                    cur_bb->num_succ++;
                }
            }
            break;
        }

        /* Move on to next block. */
        cur_bb = cur_bb->linear_next;
    }

    /* Clear up the temporary arrays. */
    free(byte_to_ins_flags);
    free(ins_flat);
    free(ins_to_bb);
}

/* Transforms a spesh graph into SSA form. After this, the graph will have all
 * register accesses given an SSA "version", and phi instructions inserted as
 * needed. */
static void ssa(MVMThreadContext *tc, MVMSpeshGraph *g) {
}

/* Takes a static frame and creates a spesh graph for it. */
MVMSpeshGraph * MVM_spesh_graph_create(MVMThreadContext *tc, MVMStaticFrame *sf) {
    /* Create top-level graph object. */
    MVMSpeshGraph *g = calloc(1, sizeof(MVMSpeshGraph));

    /* Ensure the frame is validated, since we'll rely on this. */
    if (!sf->body.invoked) {
        MVM_spesh_graph_destroy(tc, g);
        MVM_exception_throw_adhoc(tc, "Spesh: cannot build CFG from unvalidated frame");
    }

    /* Build the CFG out of the static frame, and transform it to SSA. */
    build_cfg(tc, g, sf);
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
