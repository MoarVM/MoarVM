#include "moar.h"

/* This is where the spesh stuff all begins. The logic in here takes bytecode
 * and builds a spesh graph from it. This is a CFG in SSA form. Transforming
 * to SSA involves computing dominance frontiers, done by the algorithm found
 * in http://www.cs.rice.edu/~keith/EMBED/dom.pdf. The SSA algorithm itself is
 * from http://www.cs.utexas.edu/~pingali/CS380C/2010/papers/ssaCytron.pdf. */

#define GET_I8(pc, idx)     *((MVMint8 *)((pc) + (idx)))
#define GET_UI8(pc, idx)    *((MVMuint8 *)((pc) + (idx)))
#define GET_I16(pc, idx)    *((MVMint16 *)((pc) + (idx)))
#define GET_UI16(pc, idx)   *((MVMuint16 *)((pc) + (idx)))
#define GET_I32(pc, idx)    *((MVMint32 *)((pc) + (idx)))
#define GET_UI32(pc, idx)   *((MVMuint32 *)((pc) + (idx)))
#define GET_N32(pc, idx)    *((MVMnum32 *)((pc) + (idx)))

/* Allocate a piece of memory from the spesh graph's region
 * allocator. Deallocated when the spesh graph is. */
void * MVM_spesh_alloc(MVMThreadContext *tc, MVMSpeshGraph *g, size_t bytes) {
    return MVM_region_alloc(tc, &g->region_alloc, bytes);
}

/* Grows the spesh graph's deopt table if it is already full, so that we have
 * space for 1 more entry. */
void MVM_spesh_graph_grow_deopt_table(MVMThreadContext *tc, MVMSpeshGraph *g) {
    if (g->num_deopt_addrs == g->alloc_deopt_addrs) {
        g->alloc_deopt_addrs += 8;
        if (g->deopt_addrs)
            g->deopt_addrs = MVM_realloc(g->deopt_addrs,
                g->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        else
            g->deopt_addrs = MVM_malloc(g->alloc_deopt_addrs * sizeof(MVMint32) * 2);
    }
}

/* Records a de-optimization annotation and mapping pair. */
MVMint32 MVM_spesh_graph_add_deopt_annotation(MVMThreadContext *tc, MVMSpeshGraph *g,
                                          MVMSpeshIns *ins_node, MVMuint32 deopt_target,
                                          MVMint32 type) {
    /* Add an annotations. */
    MVMSpeshAnn *ann      = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
    ann->type             = type;
    ann->data.deopt_idx   = g->num_deopt_addrs;
    ann->next             = ins_node->annotations;
    ins_node->annotations = ann;

    /* Record PC in the deopt entries table. */
    MVM_spesh_graph_grow_deopt_table(tc, g);
    g->deopt_addrs[2 * g->num_deopt_addrs] = deopt_target;
    g->num_deopt_addrs++;
    return ann->data.deopt_idx;
}

MVM_FORMAT(printf, 4, 5)
MVM_PUBLIC void MVM_spesh_graph_add_comment(MVMThreadContext *tc, MVMSpeshGraph *g,
    MVMSpeshIns *ins, const char *fmt, ...) {
    size_t size;
    char *comment;
    va_list ap;
    MVMSpeshAnn *ann;

    if (!MVM_spesh_debug_enabled(tc))
        return;

    va_start(ap, fmt);

    size = vsnprintf(NULL, 0, fmt, ap);
    comment = MVM_spesh_alloc(tc, g, ++size);

    va_end(ap);

    ann               = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
    ann->type         = MVM_SPESH_ANN_COMMENT;
    ann->next         = ins->annotations;
    ins->annotations  = ann;

    ann->data.comment  = comment;
    ann->order = g->next_annotation_idx++;

    va_start(ap, fmt);
    vsnprintf(comment, size, fmt, ap);
    va_end(ap);
}

/* Records the current bytecode position as a logged annotation. Used for
 * resolving logged values. */
static void add_logged_annotation(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins_node,
                                  MVMuint8 *pc) {
    MVMSpeshAnn *ann = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
    ann->type = MVM_SPESH_ANN_LOGGED;
    ann->data.bytecode_offset = pc - g->bytecode;
    ann->next = ins_node->annotations;
    ins_node->annotations = ann;
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

/* Checks if a handler is a catch handler or a control handler. */
static MVMint32 is_catch_handler(MVMThreadContext *tc, MVMSpeshGraph *g, MVMint32 handler_idx) {
    return g->handlers[handler_idx].category_mask & MVM_EX_CAT_CATCH;
}

/* Checks if a basic block already has a particular successor. */
static MVMint32 already_succs(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshBB *succ) {
    MVMint32 i = 0;
    for (i = 0; i < bb->num_succ; i++)
        if (bb->succ[i] == succ)
            return 1;
    return 0;
}

/* Builds the control flow graph, populating the passed spesh graph structure
 * with it. This also makes nodes for all of the instruction. */
#define MVM_CFG_BB_START    1
#define MVM_CFG_BB_END      2
#define MVM_CFG_BB_JUMPLIST 4
static void build_cfg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMStaticFrame *sf,
                      MVMint32 *existing_deopts, MVMint32 num_existing_deopts,
                      MVMint32 *deopt_usage_info, MVMSpeshIns ***deopt_usage_ins_out) {
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
     *    shifting it by 3 bits to the left. We will use this to do fixups.
     * B) The first bit is "I have an incoming branch" - that is, start of
     *    a basic block. The second bit is "I can branch" - that is, end of
     *    a basic block. It's possible to have both bits set. If it's part
     *    of a jumplist, it gets the third bit set also.
     * Anything that's just a zero has no instruction starting there. */
    MVMuint32 *byte_to_ins_flags = MVM_calloc(g->bytecode_size, sizeof(MVMuint32));

    /* Instruction to basic block mapping. Initialized later. */
    MVMSpeshBB **ins_to_bb = NULL;

    /* Which handlers are active; used for placing edges from blocks covered
     * by exception handlers. */
    MVMuint8 *active_handlers = MVM_calloc(1, g->num_handlers);
    MVMint32 num_active_handlers = 0;

    /* Make first pass through the bytecode. In this pass, we make MVMSpeshIns
     * nodes for each instruction and set the start/end of block bits. Also
     * set handler targets as basic block starters. */
    MVMCompUnit *cu       = sf->body.cu;
    MVMuint8    *pc       = g->bytecode;
    MVMuint8    *end      = g->bytecode + g->bytecode_size;
    MVMuint32    ins_idx  = 0;
    MVMuint8     next_bbs = 1; /* Next iteration (here, first) starts a BB. */
    MVMuint32    num_osr_points = 0;

    MVMBytecodeAnnotation *ann_ptr = MVM_bytecode_resolve_annotation(tc, &sf->body, sf->body.bytecode - pc);

    for (i = 0; i < g->num_handlers; i++) {
        if (g->handlers[i].start_offset != (MVMuint32)-1 && g->handlers[i].goto_offset != (MVMuint32)-1) {
            byte_to_ins_flags[g->handlers[i].start_offset] |= MVM_CFG_BB_START;
            byte_to_ins_flags[g->handlers[i].end_offset] |= MVM_CFG_BB_START;
            byte_to_ins_flags[g->handlers[i].goto_offset] |= MVM_CFG_BB_START;
        }
    }
    while (pc < end) {
        /* Look up op info. */
        MVMuint16  opcode     = *(MVMuint16 *)pc;
        MVMuint8  *args       = pc + 2;
        MVMuint8   arg_size   = 0;
        const MVMOpInfo *info = MVM_bytecode_get_validated_op_info(tc, cu, opcode);

        /* Create an instruction node, add it, and record its position. */
        MVMSpeshIns *ins_node = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        ins_flat[ins_idx] = ins_node;
        byte_to_ins_flags[pc - g->bytecode] |= ins_idx << 3;

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

        /* Store opcode. */
        ins_node->info = info;

        /* If this is a pre-instruction deopt point opcode, annotate. */
        if (!existing_deopts && (info->deopt_point & MVM_DEOPT_MARK_ONE_PRE))
            MVM_spesh_graph_add_deopt_annotation(tc, g, ins_node,
                pc - g->bytecode, MVM_SPESH_ANN_DEOPT_ONE_INS);

        /* Let's see if we have a line-number annotation. */
        if (ann_ptr && pc - sf->body.bytecode == ann_ptr->bytecode_offset) {
            MVMSpeshAnn *lineno_ann = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
            lineno_ann->next = ins_node->annotations;
            lineno_ann->type = MVM_SPESH_ANN_LINENO;
            lineno_ann->data.lineno.filename_string_index = ann_ptr->filename_string_heap_index;
            lineno_ann->data.lineno.line_number = ann_ptr->line_number;
            ins_node->annotations = lineno_ann;

            MVM_bytecode_advance_annotation(tc, &sf->body, ann_ptr);
        }

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
                case MVM_operand_uint32:
                    ins_node->operands[i].lit_ui32 = GET_UI32(args, arg_size);
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
                    MVM_oops(tc,
                        "Spesh: unknown operand type %d in graph building (op %s)",
                        (int)type, ins_node->info->name);
                }
                break;
            }
            default:
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
                byte_to_ins_flags[(pc - g->bytecode) + 12 + i * 6] |=
                    MVM_CFG_BB_START | MVM_CFG_BB_JUMPLIST;
            byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_END;
        }

        /* Invoke and return end a basic block. Anything that is marked as
         * invokish and throwish are also basic block ends. OSR points are
         * basic block starts. */
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
            byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_END;
            next_bbs = 1;
            break;
        case MVM_OP_osrpoint:
            byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_START;
            if (pc - g->bytecode > 0) {
                MVMuint32 prev = pc - g->bytecode;
                while (!byte_to_ins_flags[--prev]);
                byte_to_ins_flags[prev] |= MVM_CFG_BB_END;
            }
            num_osr_points++;
            break;
        default:
            if (info->jittivity & (MVM_JIT_INFO_THROWISH | MVM_JIT_INFO_INVOKISH)) {
                byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_END;
                next_bbs = 1;
            }
            break;
        }

        /* Final instruction is basic block end. */
        if (pc + 2 + arg_size == end)
            byte_to_ins_flags[pc - g->bytecode] |= MVM_CFG_BB_END;

        /* If the instruction is logged, store its program counter so we can
         * associate it with a static value later. */
        if (info->logged)
            add_logged_annotation(tc, g, ins_node, pc);

        /* Caculate next instruction's PC. */
        pc += 2 + arg_size;

        /* If this is a post-instruction deopt point opcode... */
        if (!existing_deopts && (info->deopt_point & MVM_DEOPT_MARK_ONE))
            MVM_spesh_graph_add_deopt_annotation(tc, g, ins_node,
                pc - g->bytecode, MVM_SPESH_ANN_DEOPT_ONE_INS);
        if (!existing_deopts && (info->deopt_point & MVM_DEOPT_MARK_ALL))
            MVM_spesh_graph_add_deopt_annotation(tc, g, ins_node,
                pc - g->bytecode, MVM_SPESH_ANN_DEOPT_ALL_INS);
        if (!existing_deopts && (info->deopt_point & MVM_DEOPT_MARK_OSR))
            MVM_spesh_graph_add_deopt_annotation(tc, g, ins_node,
                pc - g->bytecode, MVM_SPESH_ANN_DEOPT_OSR);

        /* Go to next instruction. */
        ins_idx++;
    }

    /* Annotate instructions that are handler-significant. */
    for (i = 0; i < g->num_handlers; i++) {
        /* Start or got may be -1 if the code the handler covered became
         * dead. If so, mark the handler as removed. Ditto if end is
         * before start (would never match). */
        if (g->handlers[i].start_offset == (MVMuint32)-1 || g->handlers[i].goto_offset == (MVMuint32)-1 ||
                g->handlers[i].start_offset > g->handlers[i].end_offset) {
            if (!g->unreachable_handlers)
                g->unreachable_handlers = MVM_spesh_alloc(tc, g, g->num_handlers);
            g->unreachable_handlers[i] = 1;
        }
        else {
            MVMSpeshIns *start_ins = ins_flat[byte_to_ins_flags[g->handlers[i].start_offset] >> 3];
            MVMSpeshIns *end_ins   = ins_flat[byte_to_ins_flags[g->handlers[i].end_offset] >> 3];
            MVMSpeshAnn *start_ann = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
            MVMSpeshAnn *end_ann   = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
            MVMSpeshIns *goto_ins  = ins_flat[byte_to_ins_flags[g->handlers[i].goto_offset] >> 3];
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
    }

    /* Annotate instructions that are inline start/end points. */
    for (i = 0; i < g->num_inlines; i++) {
        if (!g->inlines[i].unreachable) {
            MVMSpeshIns *start_ins = ins_flat[byte_to_ins_flags[g->inlines[i].start] >> 3];
            MVMSpeshIns *end_ins   = ins_flat[byte_to_ins_flags[g->inlines[i].end] >> 3];
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
    }

    /* Now for the second pass, where we assemble the basic blocks. Also we
     * build a lookup table of instructions that start a basic block to that
     * basic block, for the final CFG construction. We make the entry block a
     * special one, containing a noop; it will have any catch exception
     * handler targets linked from it, so they show up in the graph. For any
     * control exceptions, we will insert  */
    g->entry                  = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB));
    g->entry->first_ins       = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    g->entry->first_ins->info = MVM_bytecode_get_validated_op_info(tc, cu, 0);
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
        cur_ins = ins_flat[byte_to_ins_flags[i] >> 3];

        /* Start of a basic block? */
        if (byte_to_ins_flags[i] & MVM_CFG_BB_START) {
            /* Should not already be in a basic block. */
            if (cur_bb) {
                MVM_spesh_graph_destroy(tc, g);
                MVM_oops(tc, "Spesh: confused during basic block analysis (in block)");
            }

            /* Create it, and set first instruction and index. */
            cur_bb = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB));
            cur_bb->first_ins = cur_ins;
            cur_bb->idx = bb_idx;
            cur_bb->initial_pc = i;
            cur_bb->jumplist = byte_to_ins_flags[i] & MVM_CFG_BB_JUMPLIST;
            bb_idx++;

            /* Record instruction -> BB start mapping. */
            ins_to_bb[ins_idx] = cur_bb;

            /* Link it to the previous one. */
            prev_bb->linear_next = cur_bb;
        }

        /* Should always be in a BB at this point. */
        if (!cur_bb) {
            MVM_spesh_graph_destroy(tc, g);
            MVM_oops(tc, "Spesh: confused during basic block analysis (no block)");
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
     * the instruction operands get the target BB stored. This is where we
     * link basic blocks covered by control exception handlers to the goto
     * block of the handler also. */
    cur_bb = g->entry;
    while (cur_bb) {
        /* If it's the first block, it's a special case; successors are the
         * real successor, all catch exception handlers, and all OSR points.
         */
        if (cur_bb == g->entry) {
            MVMint32 num_bbs = 1 + g->num_handlers + num_osr_points;
            MVMint32 insert_pos = 1;
            cur_bb->succ     = MVM_spesh_alloc(tc, g, num_bbs * sizeof(MVMSpeshBB *));
            cur_bb->handler_succ = MVM_spesh_alloc(tc, g, g->num_handlers * sizeof(MVMSpeshBB *));
            cur_bb->succ[0]  = cur_bb->linear_next;
            for (i = 0; i < g->num_handlers; i++) {
                if (is_catch_handler(tc, g, i)) {
                    MVMuint32 offset = g->handlers[i].goto_offset;
                    if (offset != (MVMuint32)-1)
                        cur_bb->succ[insert_pos++] = ins_to_bb[byte_to_ins_flags[offset] >> 3];
                }
            }
            if (num_osr_points > 0) {
                MVMSpeshBB *search_bb = cur_bb->linear_next;
                while (search_bb) {
                    if (search_bb->first_ins->info->opcode == MVM_OP_osrpoint)
                        cur_bb->succ[insert_pos++] = search_bb;
                    search_bb = search_bb->linear_next;
                }
            }
            cur_bb->num_succ = insert_pos;
        }

        /* Otherwise, non-entry basic block. */
        else {
            /* If this is the start of a frame handler that is not a catch,
             * mark it as an active handler. Unmark those where we see the
             * end of the handler. */
            if (cur_bb->first_ins->annotations) {
                /* Process them in two passes in case we have two on the
                 * same instruction and disordered. */
                MVMuint32 has_end = 0;
                MVMSpeshAnn *ann = cur_bb->first_ins->annotations;
                while (ann) {
                    switch (ann->type) {
                        case MVM_SPESH_ANN_FH_START:
                            if (!is_catch_handler(tc, g, ann->data.frame_handler_index)) {
                                active_handlers[ann->data.frame_handler_index] = 1;
                                num_active_handlers++;
                            }
                            break;
                        case MVM_SPESH_ANN_FH_END:
                            has_end = 1;
                            break;
                    }
                    ann = ann->next;
                }
                if (has_end) {
                    ann = cur_bb->first_ins->annotations;
                    while (ann) {
                        switch (ann->type) {
                            case MVM_SPESH_ANN_FH_END:
                                if (!is_catch_handler(tc, g, ann->data.frame_handler_index)) {
                                    active_handlers[ann->data.frame_handler_index] = 0;
                                    num_active_handlers--;
                                }
                                break;
                        }
                        ann = ann->next;
                    }
                }
            }

            /* Consider the last instruction, to see how we leave the BB. */
            switch (cur_bb->last_ins->info->opcode) {
                case MVM_OP_jumplist: {
                    /* Jumplist, so successors are next N+1 basic blocks. */
                    MVMint64 jump_bbs = cur_bb->last_ins->operands[0].lit_i64 + 1;
                    MVMint64 num_bbs = jump_bbs + num_active_handlers;
                    MVMSpeshBB *bb_to_add = cur_bb->linear_next;
                    cur_bb->succ = MVM_spesh_alloc(tc, g, num_bbs * sizeof(MVMSpeshBB *));
                    for (i = 0; i < jump_bbs; i++) {
                        cur_bb->succ[i] = bb_to_add;
                        bb_to_add = bb_to_add->linear_next;
                    }
                    cur_bb->num_succ = jump_bbs;
                }
                break;
                case MVM_OP_goto: {
                    /* Unconditional branch, so one successor. */
                    MVMint64 num_bbs = 1 + num_active_handlers;
                    MVMuint32   offset = cur_bb->last_ins->operands[0].ins_offset;
                    MVMSpeshBB *tgt    = ins_to_bb[byte_to_ins_flags[offset] >> 3];
                    cur_bb->succ       = MVM_spesh_alloc(tc, g, num_bbs * sizeof(MVMSpeshBB *));
                    cur_bb->succ[0]    = tgt;
                    cur_bb->num_succ   = 1;
                    cur_bb->last_ins->operands[0].ins_bb = tgt;
                }
                break;
                default: {
                    /* Probably conditional branch, so two successors: one from
                     * the instruction, another from fall-through. Or may just be
                     * a non-branch that exits for other reasons. */
                    MVMint64 num_bbs = 2 + num_active_handlers;
                    cur_bb->succ = MVM_spesh_alloc(tc, g, num_bbs * sizeof(MVMSpeshBB *));
                    for (i = 0; i < cur_bb->last_ins->info->num_operands; i++) {
                        if (cur_bb->last_ins->info->operands[i] == MVM_operand_ins) {
                            MVMuint32 offset = cur_bb->last_ins->operands[i].ins_offset;
                            cur_bb->succ[0] = ins_to_bb[byte_to_ins_flags[offset] >> 3];
                            cur_bb->num_succ++;
                            cur_bb->last_ins->operands[i].ins_bb = cur_bb->succ[0];
                        }
                    }
                    if (cur_bb->num_succ > 1) {
                        /* If we ever get instructions with multiple targets, this
                         * area of the code needs an update. */
                        MVM_spesh_graph_destroy(tc, g);
                        MVM_oops(tc, "Spesh: unhandled multi-target branch");
                    }
                    if (cur_bb->linear_next) {
                        cur_bb->succ[cur_bb->num_succ] = cur_bb->linear_next;
                        cur_bb->num_succ++;
                    }
                }
                break;
            }

            /* Attach this block to the goto block of any active handlers. */
            if (
                num_active_handlers
                && (
                    cur_bb->last_ins->info->jittivity & (MVM_JIT_INFO_THROWISH | MVM_JIT_INFO_INVOKISH)
                    || cur_bb->last_ins->info->opcode == MVM_OP_invoke_v
                    || cur_bb->last_ins->info->opcode == MVM_OP_invoke_i
                    || cur_bb->last_ins->info->opcode == MVM_OP_invoke_n
                    || cur_bb->last_ins->info->opcode == MVM_OP_invoke_s
                    || cur_bb->last_ins->info->opcode == MVM_OP_invoke_o
                )
            ) {
                cur_bb->handler_succ = MVM_spesh_alloc(tc, g, num_active_handlers * sizeof(MVMSpeshBB *));
                for (i = 0; i < g->num_handlers; i++) {
                    if (active_handlers[i]) {
                        MVMuint32 offset = g->handlers[i].goto_offset;
                        MVMSpeshBB *target = ins_to_bb[byte_to_ins_flags[offset] >> 3];
                        if (!already_succs(tc, cur_bb, target)) {
                            cur_bb->succ[cur_bb->num_succ] = target;
                            cur_bb->num_succ++;
                            cur_bb->handler_succ[cur_bb->num_handler_succ++] = target;
                        }
                    }
                }
            }
            else
                cur_bb->handler_succ = NULL;
        }

        /* Move on to next block. */
        cur_bb = cur_bb->linear_next;
    }

    /* If we're building the graph for optimized bytecode, insert existing
     * deopt points. */
    if (existing_deopts) {
        for (i = 0; i < num_existing_deopts; i ++) {
            if (existing_deopts[2 * i + 1] >= 0) {
                MVMSpeshIns *post_ins     = ins_flat[byte_to_ins_flags[existing_deopts[2 * i + 1]] >> 3];
                MVMSpeshIns *deopt_ins    = post_ins->prev ? post_ins->prev :
                    MVM_spesh_graph_linear_prev(tc, g,
                        ins_to_bb[byte_to_ins_flags[existing_deopts[2 * i + 1]] >> 3])->last_ins;
                MVMSpeshAnn *deopt_ann    = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
                deopt_ann->next           = deopt_ins->annotations;
                deopt_ann->type           = MVM_SPESH_ANN_DEOPT_INLINE;
                deopt_ann->data.deopt_idx = i;
                deopt_ins->annotations    = deopt_ann;
            }
        }
    }

    /* We may also need to reconstruct deopt usage info, to allow optimizing of
     * inlinees. That is persisted using bytecode offsets, and this is the last
     * place we can map those into instructions, so do it here if needed. */
    if (deopt_usage_ins_out && deopt_usage_info) {
        MVM_VECTOR_DECL(MVMSpeshIns *, usage_ins);
        MVMuint32 idx = 0;
        MVM_VECTOR_INIT(usage_ins, 32);
        while (1) {
            MVMint32 offset = deopt_usage_info[idx];
            if (offset == -1)
                break;
            MVM_VECTOR_PUSH(usage_ins, ins_flat[byte_to_ins_flags[offset] >> 3]);
            idx++;
            idx += deopt_usage_info[idx] + 1; /* Skip over deopt indices */
        }
        *deopt_usage_ins_out = usage_ins;
    }

    /* Clear up the temporary arrays. */
    MVM_free(byte_to_ins_flags);
    MVM_free(ins_flat);
    MVM_free(ins_to_bb);
    MVM_free(ann_ptr);
    MVM_free(active_handlers);
}

/* Inserts nulling of object reigsters. A later stage of the optimizer will
 * throw out any that are unrequired, leaving only those that cover (rare)
 * "register read before assigned" cases. (We can thus just start off with
 * them NULL, since zeroed memory is cheaper than copying a VMNull in to
 * place). */
static MVMint32 is_handler_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 reg) {
    MVMuint32 num_handlers = g->num_handlers;
    MVMuint32 i;
    for (i = 0; i < num_handlers; i++)
        if (g->handlers[i].action == MVM_EX_ACTION_INVOKE)
            if (g->handlers[i].block_reg == reg)
                return 1;
    return 0;
}
static void insert_object_null_instructions(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *insert_bb = g->entry->linear_next;
    MVMuint16 *local_types = g->sf->body.local_types;
    MVMuint16  num_locals = g->sf->body.num_locals;
    MVMuint16 i;
    MVMSpeshIns *insert_after = NULL;
    if (insert_bb->first_ins && insert_bb->first_ins->info->opcode == MVM_OP_prof_enter) {
        insert_after = insert_bb->first_ins;
    }
    for (i = 0; i < num_locals; i++) {
        if (local_types[i] == MVM_reg_obj && !is_handler_reg(tc, g, i)) {
            MVMSpeshIns *null_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
            null_ins->info = MVM_op_get_op(MVM_OP_null);
            null_ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand));
            null_ins->operands[0].reg.orig = i;
            MVM_spesh_manipulate_insert_ins(tc, insert_bb, insert_after, null_ins);
            insert_after = null_ins;
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
            if (tgt->num_pred)
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
MVMSpeshBB ** MVM_spesh_graph_reverse_postorder(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB **rpo  = MVM_calloc(g->num_bbs, sizeof(MVMSpeshBB *));
    MVMuint8    *seen = MVM_calloc(g->num_bbs, 1);
    MVMint32     ins  = g->num_bbs - 1;
    dfs(rpo, &ins, seen, g->entry);
    MVM_free(seen);
    if (ins != -1) {
        char *dump_msg = MVM_spesh_dump(tc, g);
        printf("%s", dump_msg);
        MVM_free(dump_msg);
        MVM_spesh_graph_destroy(tc, g);
        MVM_oops(tc, "Spesh: reverse postorder calculation failed");
    }
    return rpo;
}

/* 2-finger intersection algorithm, to find new immediate dominator. */
static void iter_check(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo, MVMint32 *doms, MVMint32 iters) {
    if (iters > 100000) {
#ifdef NDEBUG
        MVMuint32 k;
        char *dump_msg = MVM_spesh_dump(tc, g);
        printf("%s", dump_msg);
        MVM_free(dump_msg);
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
        MVM_oops(tc, "Spesh: dominator intersection went infinite");
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
    MVMuint32 i, j, changed;

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
                MVM_oops(tc, "Spesh: could not find processed initial dominator");
            }
            for (j = 0; j < b->num_pred; j++) {
                if (j != (MVMuint32)chosen_pred) {
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
    if (target->num_children)
        memcpy(new_children, target->children, target->num_children * sizeof(MVMSpeshBB *));
    new_children[target->num_children] = to_add;
    target->children = new_children;
    target->num_children++;
}
static void add_children(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo, MVMint32 *doms) {
    MVMuint32 i;
    for (i = 0; i < g->num_bbs; i++) {
        MVMSpeshBB *bb   = rpo[i];
        MVMuint32   idom = doms[i];
        if ((MVMuint32)idom != i)
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
    if (target->num_df)
        memcpy(new_df, target->df, target->num_df * sizeof(MVMSpeshBB *));
    new_df[target->num_df] = to_add;
    target->df = new_df;
    target->num_df++;
}
static void add_dominance_frontiers(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB **rpo, MVMint32 *doms) {
    MVMint32 j;
    MVMSpeshBB *b = g->entry;
    while (b) {
        if (b->num_pred >= 2) { /* Thus it's a join point. */
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
    SSAVarInfo *var_info = MVM_calloc(g->num_locals, sizeof(SSAVarInfo));
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
     * we have a sparse array through which we must search. */
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
            worklist[worklist_top++] = bb; /* Algo unions, but ass_nodes unique. */
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
    MVM_oops(tc, "Spesh: which_pred failed to find x");
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
    MVMSpeshBB **rpo  = MVM_spesh_graph_reverse_postorder(tc, g);
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
MVMSpeshGraph * MVM_spesh_graph_create(MVMThreadContext *tc, MVMStaticFrame *sf,
        MVMuint32 cfg_only, MVMuint32 insert_object_nulls) {
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
        MVM_oops(tc, "Spesh: cannot build CFG from unvalidated frame");
    }

    /* Build the CFG out of the static frame, and transform it to SSA. */
    build_cfg(tc, g, sf, NULL, 0, NULL, NULL);
    if (insert_object_nulls)
        insert_object_null_instructions(tc, g);
    if (!cfg_only) {
        MVM_spesh_eliminate_dead_bbs(tc, g, 0);
        add_predecessors(tc, g);
        ssa(tc, g);
    }

    /* Hand back the completed graph. */
    return g;
}

/* Takes a static frame and creates a spesh graph for it. */
MVMSpeshGraph * MVM_spesh_graph_create_from_cand(MVMThreadContext *tc, MVMStaticFrame *sf,
                                                 MVMSpeshCandidate *cand, MVMuint32 cfg_only,
                                                 MVMSpeshIns ***deopt_usage_ins_out) {
    /* Create top-level graph object. */
    MVMSpeshGraph *g     = MVM_calloc(1, sizeof(MVMSpeshGraph));
    g->sf                = sf;
    g->bytecode          = cand->bytecode;
    g->bytecode_size     = cand->bytecode_size;
    g->handlers          = cand->handlers;
    g->num_handlers      = cand->num_handlers;
    g->num_locals        = cand->num_locals;
    g->num_lexicals      = cand->num_lexicals;
    g->inlines           = cand->inlines;
    g->num_inlines       = cand->num_inlines;
    g->deopt_addrs       = cand->deopts;
    g->num_deopt_addrs   = cand->num_deopts;
    g->alloc_deopt_addrs = cand->num_deopts;
    g->deopt_named_used_bit_field = cand->deopt_named_used_bit_field;
    g->deopt_pea         = cand->deopt_pea;
    g->local_types       = cand->local_types;
    g->lexical_types     = cand->lexical_types;
    g->num_spesh_slots   = cand->num_spesh_slots;
    g->alloc_spesh_slots = cand->num_spesh_slots;
    g->phi_infos         = MVM_spesh_alloc(tc, g, MVMPhiNodeCacheSize * sizeof(MVMOpInfo));
    g->cand              = cand;

    g->spesh_slots       = MVM_malloc(g->alloc_spesh_slots * sizeof(MVMCollectable *));

    memcpy(g->spesh_slots, cand->spesh_slots, sizeof(MVMCollectable *) * g->num_spesh_slots);

    /* Ensure the frame is validated, since we'll rely on this. */
    if (sf->body.instrumentation_level == 0) {
        MVM_spesh_graph_destroy(tc, g);
        MVM_oops(tc, "Spesh: cannot build CFG from unvalidated frame");
    }

    /* Build the CFG out of the static frame, and transform it to SSA. */
    build_cfg(tc, g, sf, cand->deopts, cand->num_deopts, cand->deopt_usage_info,
            deopt_usage_ins_out);
    if (!cfg_only) {
        MVM_spesh_eliminate_dead_bbs(tc, g, 0);
        add_predecessors(tc, g);
        ssa(tc, g);
    }

    /* Hand back the completed graph. */
    return g;
}

/* Recomputes the dominance tree, after modifications to the CFG. */
void MVM_spesh_graph_recompute_dominance(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB **rpo;
    MVMint32 *doms;

    /* First, clear away all existing dominance tree information; we also toss
     * out all of the predecessors, in case they got out of sync (should try
     * and fix things up to not need this in the future). */
    MVMSpeshBB *cur_bb = g->entry;
    while (cur_bb) {
        cur_bb->children = NULL;
        cur_bb->num_children = 0;
        cur_bb->pred = NULL;
        cur_bb->num_pred = 0;
        cur_bb = cur_bb->linear_next;
    }

    /* Now form the new dominance tree. */
    add_predecessors(tc, g);
    rpo = MVM_spesh_graph_reverse_postorder(tc, g);
    doms = compute_dominators(tc, g, rpo);
    add_children(tc, g, rpo, doms);
    MVM_free(rpo);
    MVM_free(doms);
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

    /* Mark spesh slots. */
    for (i = 0; i < g->num_spesh_slots; i++)
        MVM_gc_worklist_add(tc, worklist, &(g->spesh_slots[i]));

    /* Mark inlines. */
    for (i = 0; i < g->num_inlines; i++)
        MVM_gc_worklist_add(tc, worklist, &(g->inlines[i].sf));
}

/* Describes GCables for a heap snapshot. */
void MVM_spesh_graph_describe(MVMThreadContext *tc, MVMSpeshGraph *g, MVMHeapSnapshotState *snapshot) {
    MVMuint16 i, j, num_locals, num_facts, *local_types;

    /* Mark static frame. */
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot, (MVMCollectable *)g->sf, "Static frame");

    /* Mark facts. */
    num_locals = g->num_locals;
    local_types = g->local_types ? g->local_types : g->sf->body.local_types;
    for (i = 0; i < num_locals; i++) {
        num_facts = g->fact_counts[i];
        for (j = 0; j < num_facts; j++) {
            MVMint32 flags = g->facts[i][j].flags;
            if (flags & MVM_SPESH_FACT_KNOWN_TYPE)
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot, (MVMCollectable *)g->facts[i][j].type, "Known Type");
            if (flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE)
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot, (MVMCollectable *)g->facts[i][j].decont_type, "Known Decont Type");
            if (flags & MVM_SPESH_FACT_KNOWN_VALUE) {
                if (local_types[i] == MVM_reg_obj)
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot, (MVMCollectable *)g->facts[i][j].value.o, "Known Value");
                else if (local_types[i] == MVM_reg_str)
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot, (MVMCollectable *)g->facts[i][j].value.s, "Known String Value");
            }
        }
    }

    /* Mark spesh slots. */
    for (i = 0; i < g->num_spesh_slots; i++)
        MVM_profile_heap_add_collectable_rel_idx(tc, snapshot, g->spesh_slots[i], i);

    /* Mark inlines. */
    for (i = 0; i < g->num_inlines; i++)
        MVM_profile_heap_add_collectable_rel_idx(tc, snapshot, (MVMCollectable *)g->inlines[i].sf, i);
}

/* Destroys a spesh graph, deallocating all its associated memory. */
void MVM_spesh_graph_destroy(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Free all of the allocated node memory. */
    MVM_region_destroy(tc, &g->region_alloc);
    /* If there is a candidate that we either generated or that this graph was
     * generated from, it has ownership of the malloc'd memory. If not, then we
     * need to clean up */
    if (g->spesh_slots && (!g->cand || g->cand->spesh_slots != g->spesh_slots))
        MVM_free(g->spesh_slots);
    if (g->deopt_addrs && (!g->cand || g->cand->deopts != g->deopt_addrs))
        MVM_free(g->deopt_addrs);
    if (g->inlines && (!g->cand || g->cand->inlines != g->inlines))
        MVM_free(g->inlines);
    if (g->local_types &&  (!g->cand || g->cand->local_types != g->local_types))
        MVM_free(g->local_types);
    if (g->lexical_types &&  (!g->cand || g->cand->lexical_types != g->lexical_types))
        MVM_free(g->lexical_types);

    /* Handlers can come directly from static frame, from spesh candidate, and
     * from malloc/realloc. We only free it in the last case */
    if (g->handlers && g->handlers != g->sf->body.handlers &&
        (!g->cand || g->cand->handlers != g->handlers))
        MVM_free(g->handlers);

    /* Free the graph itself. */
    MVM_free(g);
}
