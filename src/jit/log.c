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
    char * filename = MVM_malloc(strlen(tc->instance->jit_bytecode_dir) + 25);
    FILE * out;
    sprintf(filename, "%s/moar-jit-%04d.bin", tc->instance->jit_bytecode_dir, code->seq_nr);
    out = fopen(filename, "w");
    if (out) {
        fwrite(code->func_ptr, sizeof(char), code->size, out);
        fclose(out);
        if (tc->instance->jit_bytecode_map) {
            char *frame_name         = MVM_string_utf8_encode_C_string(tc, code->sf->body.name);
            char *frame_cuuid        = MVM_string_utf8_encode_C_string(tc, code->sf->body.cuuid);
            /* I'd like to add linenumber and filename information, but it's really a lot of work at this point */
            fprintf(tc->instance->jit_bytecode_map, "%s\t%s\t%s\n", filename, frame_name, frame_cuuid);
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
    const MVMJitExprOpInfo *op = info->op_info;
    MVMint32 *depth            = traverser->data;
    MVMint32 i, j;
    char indent[64];
    char nargs[80];

    (*depth)++;

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
                nargs, info->value.size);
}

static void ascend_tree(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                        MVMJitExprTree *tree, MVMint32 node) {
    MVMint32 *depth = traverser->data;
    (*depth)--;
}


static void write_graphviz_node(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                                MVMJitExprTree *tree, MVMint32 node) {
    FILE *graph_file            = traverser->data;
    const MVMJitExprOpInfo *op_info = tree->info[node].op_info;
    MVMint32 first_child        = node + 1;
    MVMint32 nchild             = op_info->nchild < 0 ? tree->nodes[first_child++] : op_info->nchild;
    MVMint32 first_arg          = first_child + nchild;
    MVMint32 i;
    fprintf(graph_file, "  n_%04d [label=\"%s\"];\n", node, op_info->name);
    for (i = 0; i < nchild; i++) {
        fprintf(graph_file, "    n_%04d -> n_%04d;\n", node, (MVMint32)tree->nodes[first_child+i]);
    }
    for (i = 0; i < op_info->nargs; i++) {
        fprintf(graph_file, "  n_%04d_a_%d [label=%ld];\n", node, i, tree->nodes[first_arg+i]);
        fprintf(graph_file, "    n_%04d -> n_%04d_a_%d;\n", node, node, i);
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
    MVMJitTile *tile;
    MVM_jit_log(tc, "Start log of JIT tile list\n"
                    "__________________________\n");
    for (tile = list->first; tile != NULL; tile = tile->next) {
        if (tile->template) {
            MVM_jit_log(tc, "normal tile %d %s\n", tile->order_nr, tile->template->expr);
        } else {
            MVM_jit_log(tc, "pseudo tile (node %d/%s)\n", tile->node,
                        list->tree->info[tile->node].op_info->name);
        }
    }
    MVM_jit_log(tc, "End log of JIT tile list\n"
                    "________________________\n");
}
