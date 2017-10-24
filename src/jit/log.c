#include "moar.h"

/* inline this? maybe */
void MVM_jit_log(MVMThreadContext *tc, const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (tc->instance->jit_log_fh) {
        vfprintf(tc->instance->jit_log_fh, fmt, args);
    }
    va_end(args);
}

void MVM_jit_log_bytecode(MVMThreadContext *tc, MVMJitCode *code) {
    /* Filename format: moar-jit-%d.bin. number can consume at most 10
     * bytes, moar-jit-.bin is 13 bytes, one byte for the zero at the
     * end, one byte for the directory separator is 25 bytes, plus the
     * length of the bytecode directory itself */
    size_t filename_size = strlen(tc->instance->jit_bytecode_dir) + 25;
    char * filename = MVM_malloc(filename_size);
    FILE * out;
    snprintf(filename, filename_size, "%s/moar-jit-%04d.bin", tc->instance->jit_bytecode_dir, code->seq_nr);
    out = fopen(filename, "w");
    if (out) {
        fwrite(code->func_ptr, sizeof(char), code->size, out);
        fclose(out);
        if (tc->instance->jit_bytecode_map) {
            char *frame_name   = code->sf
                ? MVM_string_utf8_encode_C_string(tc, code->sf->body.name)
                : NULL;
            char *frame_cuuid  = code->sf
                ? MVM_string_utf8_encode_C_string(tc, code->sf->body.cuuid)
                : NULL;
            /* I'd like to add linenumber and filename information, but it's really a lot of work at this point */
            fprintf(
                tc->instance->jit_bytecode_map,
                "%s\t%s\t%s\n",
                filename,
                frame_name ? frame_name : "(unknown)",
                frame_cuuid ? frame_cuuid : "(unknown)"
            );
            fflush(tc->instance->jit_bytecode_map);
            MVM_free(frame_name);
            MVM_free(frame_cuuid);
        }
    } else {
        MVM_jit_log(tc, "ERROR: could dump bytecode in %s\n", filename);
    }
    MVM_free(filename);
}


static void dump_tree(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                      MVMJitExprTree *tree, MVMint32 node) {
    MVMJitExprNodeInfo *info   = &tree->info[node];
    const MVMJitExprOpInfo *op = MVM_jit_expr_op_info(tc, tree->nodes[node]);
    MVMint32 *depth            = traverser->data;
    MVMint32 i, j;
    char indent[64];
    char nargs[80];

    (*depth)++;
#define MIN(a,b) ((a) < (b) ? (a) : (b))
    i = MIN(*depth*2, sizeof(indent)-1);
    memset(indent, ' ', i);
    indent[i] = 0;
    j = 0;
    for (i = 0; i < op->nargs; i++) {
        MVMint64 arg = tree->nodes[node+op->nchild+i+1];
        j += snprintf(nargs + j, sizeof(nargs)-j-3, "%"PRId64, arg);
        if (i+1 < op->nargs && j < sizeof(nargs)-3) {
            j += sprintf(nargs + j, ", ");
        }
    }
    nargs[j] = 0;
    MVM_jit_log(tc, "%04d%s%s (%s; sz=%d)\n", node, indent, op->name,
                nargs, info->size);
}

static void ascend_tree(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                        MVMJitExprTree *tree, MVMint32 node) {
    MVMint32 *depth = traverser->data;
    (*depth)--;
}


static void write_graphviz_node(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                                MVMJitExprTree *tree, MVMint32 node) {
    FILE *graph_file            = traverser->data;
    const MVMJitExprOpInfo *op_info = MVM_jit_expr_op_info(tc, tree->nodes[node]);
    MVMint32 first_child        = node + 1;
    MVMint32 nchild             = op_info->nchild < 0 ? tree->nodes[first_child++] : op_info->nchild;
    MVMint32 first_arg          = first_child + nchild;
    MVMint32 i;
    /* maximum length of op name is 'invokish' at 8 characters, let's allocate
     * 16; maximum number of parameters is 4, and 64 bits; printing them in
     * hexadecimal would require at most 8 characters, plus 4 for the '0x' and
     * the ', '; minus 2 for the last one, plus 2 for the ampersands, plus 0 for
     * the terminus, gives us 16 + 4*12 + 3 = 67; 80 should be plenty */
    char node_label[80];
    char *ptr = node_label + sprintf(node_label, "%s%s", op_info->name,
                                     op_info->nargs ? "(" : "");
    for (i = 0; i < op_info->nargs; i++) {
        ptr += sprintf(ptr, "%#" PRIx64 "%s", tree->nodes[first_arg+i],
                       (i + 1 < op_info->nargs) ? ", "  : ")");
    }

    fprintf(graph_file, "  n_%04d [label=\"%s\"];\n", node, node_label);
    for (i = 0; i < nchild; i++) {
        fprintf(graph_file, "    n_%04d -> n_%04d;\n", node, (MVMint32)tree->nodes[first_child+i]);
    }
}


void MVM_jit_log_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;
    if (!tc->instance->jit_log_fh)
        return;
    traverser.policy    = MVM_JIT_TRAVERSER_ONCE;
    traverser.preorder  = NULL;
    traverser.inorder   = NULL;
    traverser.postorder = &write_graphviz_node;
    traverser.data      = tc->instance->jit_log_fh;

    MVM_jit_log(tc, "Starting dump of JIT expression tree\n"
                    "====================================\n");
    MVM_jit_log(tc, "digraph {\n");
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
    MVM_jit_log(tc, "}\n");
    MVM_jit_log(tc, "End dump of JIT expression tree\n"
                    "====================================\n");
}

void MVM_jit_log_tile_list(MVMThreadContext *tc, MVMJitTileList *list) {
    MVMint32 i, j;
    FILE *f = tc->instance->jit_log_fh;
    if (!f)
        return;
    fprintf(f, "Starting tile list log\n"
              "======================\n");
    for (i = 0; i < list->blocks_num; i++) {
        MVMint32 start = list->blocks[i].start, end = list->blocks[i].end;
        fprintf(f, "Block{%d} [%d-%d)\n", i, start, end);
        for (j = start; j < end; j++) {
            MVMJitTile *tile = list->items[j];
            fprintf(f, "    %d: %s\n", j, tile->debug_name ? tile->debug_name : "");
        }
        if (list->blocks[i].num_succ == 2) {
            fprintf(f, "-> { %d, %d }\n", list->blocks[i].succ[0], list->blocks[i].succ[1]);
        } else if (list->blocks[i].num_succ == 1) {
            fprintf(f, "-> { %d }\n", list->blocks[i].succ[0]);
        } else {
            fprintf(f, "-> {}\n");
        }

    }
    fprintf(f, "End of tile list log\n"
              "======================\n");

}
