#include "moar.h"
#include "expr.h"
#include "expr_tables.h"

typedef struct {
    const char      *name;
    MVMint32        nchild;
    MVMint32         nargs;
    enum MVMJitExprVtype vtype;
} MVMJitExprOpInfo;

static MVMJitExprOpInfo expr_op_info[] = {
#define OP_INFO(name, nchild, nargs, vtype) { #name, nchild, nargs, MVM_JIT_##vtype }
    MVM_JIT_IR_OPS(OP_INFO)
#undef OP_INFO
};

typedef struct {
    MVMJitGraph    *graph;
    MVMJitExprNode *nodes;
    MVMint32       *computed;
    MVMint32       *roots;
    MVMint32        num;
    MVMint32        alloc;
    MVMint32        roots_num;
    MVMint32        roots_alloc;
}  MVMJitTreeBuilder;

static inline void builder_make_space(MVMJitTreeBuilder *builder, MVMint32 space) {
    if (builder->num + space >= builder->alloc) {
        builder->alloc *= 2;
        builder->nodes = MVM_realloc(builder->nodes, builder->alloc*sizeof(MVMJitExprNode));
    }
}

static inline void builder_append_direct(MVMJitTreeBuilder *builder, MVMJitExprNode *template,
                                         MVMint32 len) {
    builder_make_space(builder, len);
    memcpy(builder->nodes + builder->num, template, len*sizeof(MVMJitExprNode));
    builder->num += len;
}

static inline void builder_add_root(MVMJitTreeBuilder *builder, MVMint32 node) {
    if (builder->roots_num + 1 >= builder->roots_alloc) {
        builder->roots_alloc *= 2;
        builder->roots = MVM_realloc(builder->roots, builder->roots_alloc*sizeof(MVMint32));
    }
    builder->roots[builder->roots_num++] = node;
}

static MVMint32 MVM_jit_expr_add_loadreg(MVMThreadContext *tc, MVMJitTreeBuilder *builder,
                                         MVMuint16 reg) {
    MVMint32 num        = builder->num;
    MVMJitExprNode template[] = { MVM_JIT_LOCAL,
                                  MVM_JIT_ADDR, num, reg * MVM_JIT_REG_SZ,
                                  MVM_JIT_LOAD, num + 1, MVM_JIT_REG_SZ };
    builder_append_direct(builder, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num + 4;
}


static MVMint32 MVM_jit_expr_add_storereg(MVMThreadContext *tc, MVMJitTreeBuilder *builder,
                                          MVMint32 node, MVMuint16 reg) {
    MVMint32 num = builder->num;
    MVMJitExprNode template[] = { MVM_JIT_LOCAL,
                                  MVM_JIT_ADDR, num, reg * MVM_JIT_REG_SZ,
                                  MVM_JIT_STORE, num + 1, node, MVM_JIT_REG_SZ };
    builder_append_direct(builder, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num + 4;
}


static MVMint32 MVM_jit_expr_add_const(MVMThreadContext *tc, MVMJitTreeBuilder *builder,
                                       MVMSpeshOperand opr, MVMuint8 info) {
    /* TODO implement this properly; this only works correctly for 64 bit values */
    MVMJitExprNode template[]  = { MVM_JIT_CONST, opr.lit_i64, sizeof(MVMint64) };
    MVMint32 num               = builder->num;
    builder_append_direct(builder, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num;
}

void MVM_jit_expr_load_operands(MVMThreadContext *tc, MVMJitTreeBuilder *builder,
                                MVMSpeshIns *ins, MVMint32 *operands) {
    MVMint32 i;
    for (i = 0; i < ins->info->num_operands; i++) {
        MVMSpeshOperand opr = ins->operands[i];
        switch(ins->info->operands[i] & MVM_operand_rw_mask) {
        case MVM_operand_read_reg:
            if (builder->computed[opr.reg.orig] > 0) {
                operands[i] = builder->computed[opr.reg.orig];
            } else {
                operands[i] = MVM_jit_expr_add_loadreg(tc, builder, opr.reg.orig);
                builder->computed[opr.reg.orig] = operands[i];
            }
            break;
        case MVM_operand_literal:
            operands[i] = MVM_jit_expr_add_const(tc, builder, opr, ins->info->operands[i]);
            break;
        }
    }
}

/* Add template to nodes, filling in operands and linking tree nodes. Return template root */
MVMint32 MVM_jit_expr_apply_template(MVMThreadContext *tc, MVMJitTreeBuilder *builder,
                                     const MVMJitExprTemplate *template, MVMint32 *operands) {
    int i, num;
    num = builder->num;
    builder_make_space(builder, template->len);
    /* Loop over string until the end */
    for (i = 0; template->info[i]; i++) {
        switch (template->info[i]) {
        case 'l':
            /* link template-relative to nodes-relative */
            builder->nodes[num+i] = template->code[i] + num;
            break;
        case 'f':
            /* add operand node into the nodes */
            builder->nodes[num+i] = operands[template->code[i]];
            break;
        default:
            /* copy from template to nodes */
            builder->nodes[num+i] = template->code[i];
            break;
        }
    }
    builder->num = num + template->len;
    return num + template->root; /* root relative to nodes */
}

/* TODO add labels to the expression tree */
MVMJitExprTree * MVM_jit_build_expression_tree(MVMThreadContext *tc, MVMSpeshGraph *sg,
                                               MVMSpeshBB *bb) {
    MVMint32 operands[MVM_MAX_OPERANDS];
    MVMint32 root;
    MVMJitTreeBuilder builder;
    MVMJitExprTree *tree = NULL;
    MVMSpeshIns *ins;
    MVMuint16 i;
    /* Hold the tree */
    builder.num      = 0;
    builder.alloc    = 64;
    builder.nodes   = MVM_malloc(sizeof(MVMJitExprNode)*builder.alloc);
    /* Hold it's roots (terminal statements) */
    builder.roots_alloc = 16;
    builder.roots_num   = 0;
    builder.roots       = MVM_malloc(sizeof(MVMint32)*builder.roots_alloc);
    /* Hold indices to the node that last computed a value belonging
     * to a register. Initialized as -1 to indicate that these
     * values are empty. */
    builder.computed = MVM_malloc(sizeof(MVMint32)*sg->num_locals);
    memset(builder.computed, -1, sizeof(MVMint32)*sg->num_locals);
    /* This is very similar to a code generation algorithm for a RISC
       machine with unlimited registers. (Or at least more registers
       than used in the expression). The basic idea is to keep a
       index to the node that last computed the value of a local.
       Each opcode is translated to the expression using a template,
       which is a): filled with nodes coming from operands and b):
       internally linked together (relative to absolute indexes).
       NB - templates may insert stores internally as needed. */
    for (ins = bb->first_ins; ins != NULL; ins = ins->next) {
        /* NB - we probably will want to involve the spesh info in
           selecting a template. And/or add in c function calls to
           them mix.. */
        MVMuint16 opcode = ins->info->opcode;
        if (opcode == MVM_SSA_PHI || opcode == MVM_OP_no_op) {
            continue;
        }
        const MVMJitExprTemplate *templ = MVM_jit_get_template_for_opcode(opcode);
        if (templ == NULL) {
            /* we don't have a template for this yet, so we can't
             * convert it to an expression */
            break;
        }
        MVM_jit_expr_load_operands(tc, &builder, ins, operands);
        root = MVM_jit_expr_apply_template(tc, &builder, templ, operands);
        /* assign computed value to computed nodes */
        if ((ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
            builder.computed[ins->operands[0].reg.orig] = root;
        } else {
            /* Terminal, add it to roots */
            builder_add_root(&builder, root);
        }
    }

    /* Reached the end correctly? Build a tree */
    if (ins == NULL) {
        /* Add stores for final values */
        for (i = 0; i < sg->num_locals; i++) {
            if (builder.computed[i] >= 0) {
                /* NB - this adds a store for a just-loaded variable.
                 * Need proper CSE to elininate this correctly. */
                MVMint32 root = MVM_jit_expr_add_storereg(tc, &builder, builder.computed[i], i);
                builder_add_root(&builder, root);
            }
        }

        tree = MVM_spesh_alloc(tc, sg, sizeof(MVMJitExprTree));
        tree->nodes = MVM_spesh_alloc(tc, sg, builder.num*sizeof(MVMJitExprNode));
        tree->num_nodes = builder.num;
        memcpy(tree->nodes, builder.nodes, builder.num*sizeof(MVMJitExprNode));
        tree->roots = MVM_spesh_alloc(tc, sg, builder.roots_num*sizeof(MVMint32));
        tree->num_roots = builder.roots_num;
        memcpy(tree->roots, builder.roots, builder.roots_num*sizeof(MVMint32));
    } else {
        MVM_jit_log(tc, "Could not build an expression tree, stuck at instruction %s\n",
                    ins->info->name);
    }
    MVM_free(builder.nodes);
    MVM_free(builder.computed);
    return tree;
}

static void walk(MVMThreadContext *tc, MVMJitExprTree *tree,
                 MVMJitTreeTraverser *traverser, MVMint32 i) {
    MVMJitExprOpInfo *info = &expr_op_info[tree->nodes[i]];
    MVMint32 nchild = info->nchild;
    MVMint32 j;
    /* visiting on the way down - NB want to add visitation information */
    traverser->visit(tc, tree, traverser->data, i, MVM_JIT_TREE_DOWN);
    if (nchild < 0) {
        /* take first child as constant signifying the number of children; increment
         * to take offset into account */
        nchild = tree->nodes[++i];
    }
    for (j = 0; j < nchild; j++) {
        /* Enter child node */
        walk(tc, tree, traverser, tree->nodes[i+j+1]);
    }
    traverser->visit(tc, tree, traverser->data, i, MVM_JIT_TREE_UP);
}


void MVM_jit_traverse_tree(MVMThreadContext *tc, MVMJitExprTree *tree,
                           MVMJitTreeTraverser *traverser) {
    MVMint32 i;
    for (i = 0; i < tree->num_roots; i++) {
        /* TODO deal with nodes with multiple entries */
        walk(tc, tree, traverser, tree->roots[i]);
    }
}

static void visit_dump(MVMThreadContext *tc, MVMJitExprTree *tree, void *data,
                       MVMint32 position, MVMint32 direction) {
    MVMJitExprNode node = tree->nodes[position];
    MVMint32 *depth = data;
    MVMJitExprOpInfo *info = &expr_op_info[node];
    MVMint32 i, j;
    char indent[64];
    char nargs[80];

    if (direction == MVM_JIT_TREE_DOWN) {
        (*depth)++;

        i = MIN(*depth*2, sizeof(indent)-1);
        memset(indent, ' ', i);
        indent[i] = 0;
        j = 0;
        for (i = 0; i < info->nargs; i++) {
            MVMint32 arg = tree->nodes[position+info->nchild+i+1];
            j += snprintf(nargs + j, sizeof(nargs)-j-3, "%d", arg);
            if (i+1 < info->nargs && j < sizeof(nargs)-3) {
                j += sprintf(nargs + j, ", ");
            }
        }
        nargs[j++] = 0;
        MVM_jit_log(tc, "%s%s (%s)\n", indent, info->name, nargs);
    } else {
        (*depth)--;
    }
}

/* NB - move this to log.c in due course */
void MVM_jit_dump_tree(MVMThreadContext *tc, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;
    MVMint32 cur_depth = 0;
    traverser.visit = &visit_dump;
    traverser.data  = &cur_depth;
    MVM_jit_log(tc, "Starting dump of JIT expression tree\n"
                "=========================================\n");
    MVM_jit_traverse_tree(tc, tree, &traverser);
    MVM_jit_log(tc, "End dump of JIT expression tree\n");
}
