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


/* NB - move this to log.c in due course */
void MVM_jit_log_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;
    MVMint32 cur_depth = 0;
    char roots_list[80];
    MVMint32 i,j;
    traverser.policy    = MVM_JIT_TRAVERSER_REPEAT;
    traverser.preorder  = &dump_tree;
    traverser.inorder   = NULL;
    traverser.postorder = &ascend_tree;
    traverser.data      = &cur_depth;
    MVM_jit_log(tc, "Starting dump of JIT expression tree\n"
                    "====================================\n");
    j = 0;
    for (i = 0; i < tree->roots_num; i++) {
        if (j >= sizeof(roots_list))
            break;
        j += snprintf(roots_list+j, sizeof(roots_list)-j, "%d, ", tree->roots[i]);
    }
    roots_list[j] = 0;
    MVM_jit_log(tc, "Tree Roots: [%s]\n", roots_list);
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
    MVM_jit_log(tc, "End dump of JIT expression tree\n"
                    "====================================\n");

}
