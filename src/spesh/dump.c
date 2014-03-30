#include "moar.h"

/* Auto-growing buffer. */
typedef struct {
    char   *buffer;
    size_t  alloc;
    size_t  pos;
} DumpStr;
static void append(DumpStr *ds, char *to_add) {
    size_t len = strlen(to_add);
    if (ds->pos + len >= ds->alloc) {
        ds->alloc *= 4;
        if (ds->pos + len >= ds->alloc)
            ds->alloc += len;
        ds->buffer = realloc(ds->buffer, ds->alloc);
    }
    memcpy(ds->buffer + ds->pos, to_add, len);
    ds->pos += len;
}

/* Formats a string and then appends it. */
static void appendf(DumpStr *ds, const char *fmt, ...) {
    char *c_message = malloc(1024);
    va_list args;
    va_start(args, fmt);
    c_message[vsnprintf(c_message, 1023, fmt, args)] = 0;
    append(ds, c_message);
    free(c_message);
    va_end(args);
}

/* Turns a MoarVM string into a C string and appends it. */
static void append_str(MVMThreadContext *tc, DumpStr *ds, MVMString *s) {
    MVMuint8 *cs = MVM_string_utf8_encode_C_string(tc, s);
    append(ds, cs);
    free(cs);
}

/* Dumps a basic block. */
static void dump_bb(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g, MVMSpeshBB *bb) {
    MVMSpeshIns *cur_ins;
    MVMint64     i;

    /* Heading. */
    appendf(ds, "  BB %d:\n", bb->idx);

    /* Instructions. */
    append(ds, "    Instructions:\n");
    cur_ins = bb->first_ins;
    while (cur_ins) {
        MVMSpeshAnn *ann = cur_ins->annotations;
        while (ann) {
            switch (ann->type) {
                case MVM_SPESH_ANN_FH_START:
                    appendf(ds, "      [Annotation: FH Start (%d)]\n",
                        ann->data.frame_handler_index);
                    break;
                case MVM_SPESH_ANN_FH_END:
                    appendf(ds, "      [Annotation: FH End (%d)]\n",
                        ann->data.frame_handler_index);
                    break;
                case MVM_SPESH_ANN_FH_GOTO:
                    appendf(ds, "      [Annotation: FH Goto (%d)]\n",
                        ann->data.frame_handler_index);
                    break;
                default:
                    appendf(ds, "      [Annotation: %d (unknown)]\n", ann->type);
            }
            ann = ann->next;
        }

        appendf(ds, "      %s ", cur_ins->info->name);
        if (cur_ins->info->opcode == MVM_SSA_PHI) {
            for (i = 0; i < cur_ins->info->num_operands; i++) {
                if (i)
                    append(ds, ", ");
                appendf(ds, "r%d(%d)", cur_ins->operands[i].reg.orig,
                    cur_ins->operands[i].reg.i);
            }
        }
        else {
            for (i = 0; i < cur_ins->info->num_operands; i++) {
                if (i)
                    append(ds, ", ");
                switch (cur_ins->info->operands[i] & MVM_operand_rw_mask) {
                    case MVM_operand_read_reg:
                    case MVM_operand_write_reg:
                        appendf(ds, "r%d(%d)", cur_ins->operands[i].reg.orig,
                            cur_ins->operands[i].reg.i);
                        break;
                    default:
                        append(ds, "<nyi>");
                }
            }
        }
        append(ds, "\n");
        cur_ins = cur_ins->next;
    }

    /* Predecessors and successors. */
    append(ds, "    Successors: ");
    for (i = 0; i < bb->num_succ; i++)
        appendf(ds, (i == 0 ? "%d" : ", %d"), bb->succ[i]->idx);
    append(ds, "\n    Predeccessors: ");
    for (i = 0; i < bb->num_pred; i++)
        appendf(ds, (i == 0 ? "%d" : ", %d"), bb->pred[i]->idx);
    append(ds, "\n    Dominance children: ");
    for (i = 0; i < bb->num_children; i++)
        appendf(ds, (i == 0 ? "%d" : ", %d"), bb->children[i]->idx);
    append(ds, "\n\n");
}

/* Dump a spesh graph into string form, for debugging purposes. */
char * MVM_spesh_dump(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *cur_bb;

    /* Allocate buffer. */
    DumpStr ds;
    ds.alloc  = 8192;
    ds.buffer = malloc(ds.alloc);
    ds.pos    = 0;

    /* Dump name and CUID. */
    append(&ds, "Spesh of '");
    append_str(tc, &ds, g->sf->body.name);
    append(&ds, "' (cuid: ");
    append_str(tc, &ds, g->sf->body.cuuid);
    append(&ds, ")\n\n");

    /* Go over all the basic blocks and dump them. */
    cur_bb = g->entry;
    while (cur_bb) {
        dump_bb(tc, &ds, g, cur_bb);
        cur_bb = cur_bb->linear_next;
    }

    append(&ds, "\n\0");
    return ds.buffer;
}
