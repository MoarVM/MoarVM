#include "moar.h"

void MVM_jit_dump_bytecode(MVMThreadContext *tc, MVMJitCode *code) {
    char filename[1024];
    FILE * dump;
    snprintf(filename, sizeof(filename), "%s/moar-jit-%04d.bin",
             tc->instance->jit_bytecode_dir, code->seq_nr);
    dump = MVM_platform_fopen(filename, "w");
    if (dump) {
        fwrite(code->func_ptr, sizeof(char), code->size, dump);
        fclose(dump);
        if (MVM_spesh_debug_enabled(tc))
            MVM_spesh_debug_printf(tc, "JIT: Dumped bytecode to %s\n\n", filename);
    } else if (tc->instance->jit_debug_enabled) {
        FILE *file = tc->instance->spesh_log_fh ?
            tc->instance->spesh_log_fh : stderr;
        fprintf(file, "JIT ERROR: could not dump bytecode: %s",
                strerror(errno));
    }
}


static void dump_tree(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                      MVMJitExprTree *tree, MVMint32 node) {
    MVMJitExprInfo *info   = MVM_JIT_EXPR_INFO(tree, node);
    const char *op_name = MVM_jit_expr_operator_name(tc, tree->nodes[node]);
    MVMint32 *links = MVM_JIT_EXPR_LINKS(tree, node);
    MVMint32 *depth            = traverser->data;
    MVMint32 i, j;
    char indent[64];
    char nargs[80];

    (*depth)++;
#define MVM_MIN(a,b) ((a) < (b) ? (a) : (b))
    i = MVM_MIN(*depth*2, sizeof(indent)-1);
    memset(indent, ' ', i);
    indent[i] = 0;
    j = 0;
    for (i = 0; i < info->num_args; i++) {
        MVMint32 arg = links[i];
        j += snprintf(nargs + j, sizeof(nargs)-j-3, "%"PRId32, arg);
        if (i+1 < info->num_args && j < sizeof(nargs)-3) {
            j += sprintf(nargs + j, ", ");
        }
    }
    nargs[j] = 0;
    fprintf(traverser->data, "%04d%s%s (%s; sz=%d)\n",
            node, indent, op_name, nargs, info->size);
}

static void ascend_tree(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                        MVMJitExprTree *tree, MVMint32 node) {
    MVMint32 *depth = traverser->data;
    (*depth)--;
}


static void write_graphviz_node(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                                MVMJitExprTree *tree, MVMint32 node) {
    FILE *graph_file       = traverser->data;
    const char *op_name    = MVM_jit_expr_operator_name(tc, tree->nodes[node]);
    MVMint32 *links        = MVM_JIT_EXPR_LINKS(tree, node);
    MVMint32 *args         = MVM_JIT_EXPR_ARGS(tree, node);
    MVMJitExprInfo *info   = MVM_JIT_EXPR_INFO(tree, node);
    MVMint32 i;
    /* maximum length of op name is 'invokish' at 8 characters, let's allocate
     * 16; maximum number of parameters is 4, and 64 bits; printing them in
     * hexadecimal would require at most 8 characters, plus 4 for the '0x' and
     * the ', '; minus 2 for the last one, plus 2 for the ampersands, plus 0 for
     * the terminus, gives us 16 + 4*12 + 3 = 67; 80 should be plenty */
    char node_label[80];
    char *ptr = node_label + sprintf(node_label, "%s%s", op_name,
                                     info->num_args ? "(" : "");
    for (i = 0; i < info->num_args; i++) {
        ptr += sprintf(ptr, "%" PRId32 "%s", args[i],
                       (i + 1 < info->num_args) ? ", "  : ")");
    }

    fprintf(graph_file, "  n_%04d [label=\"%s\"];\n", node, node_label);
    for (i = 0; i < info->num_links; i++) {
        fprintf(graph_file, "    n_%04d -> n_%04d;\n", node, links[i]);
    }
}


void MVM_jit_dump_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;
    FILE *log = tc->instance->spesh_log_fh;
    if (!log)
        return;
    traverser.policy    = MVM_JIT_TRAVERSER_ONCE;
    traverser.preorder  = NULL;
    traverser.inorder   = NULL;
    traverser.postorder = &write_graphviz_node;
    traverser.data      = log;

    fprintf(log, "JIT: Starting dump of JIT expression tree\n"
                 "=========================================\n\n");
    fprintf(log, "digraph {\n");
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
    fprintf(log, "}\n\n");
    fprintf(log, "End dump of JIT expression tree\n"
                 "===============================\n\n");
}

void MVM_jit_dump_tile_list(MVMThreadContext *tc, MVMJitTileList *list) {
    MVMint32 i, j;
    FILE *f = tc->instance->spesh_log_fh;
    if (!f)
        return;
    fprintf(f, "JIT: Starting tile list log\n"
               "===========================\n\n");
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
    fprintf(f, "\nEnd of tile list log\n"
                 "====================\n\n");

}
