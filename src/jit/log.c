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

static char * jitcode_name(MVMThreadContext *tc, MVMJitCode *code) {
    MVMuint64 cuuid_len;
    MVMuint64 name_len;
    char *cuuid = MVM_string_ascii_encode(tc, code->sf->body.cuuid,
                                              &cuuid_len);
    char *name  = MVM_string_ascii_encode(tc, code->sf->body.name,
                                              &name_len);
    MVMuint64 dirname_len = strlen(tc->instance->jit_bytecode_dir);
    // 4 chars for prefix, 3 chars for the separators, 4 for the postfix, 1 for the 0
    char *filename = MVM_malloc(dirname_len + name_len + cuuid_len + 12);
    char *dst = filename;
    memcpy(dst, tc->instance->jit_bytecode_dir, dirname_len);
    dst[dirname_len] = '/';
    dst += dirname_len + 1;
    memcpy(dst, "jit-", 4);
    dst += 4;
    memcpy(dst, cuuid, cuuid_len);
    dst[cuuid_len] = '.';
    dst += cuuid_len + 1;
    memcpy(dst, name, name_len);
    dst += name_len;
    memcpy(dst, ".bin", 5);
    MVM_free(name);
    MVM_free(cuuid);
    return filename;
}

void MVM_jit_log_bytecode(MVMThreadContext *tc, MVMJitCode *code) {
    char * filename = jitcode_name(tc, code);
    FILE * f = fopen(filename, "w");
    if (f) {
        fwrite(code->func_ptr, sizeof(char), code->size, f);
        fclose(f);
        MVM_jit_log(tc, "Dump bytecode in %s\n", filename);

    } else {
        MVM_jit_log(tc, "Could not dump bytecode in %s\n", filename);
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
                nargs, info->result_size);
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
