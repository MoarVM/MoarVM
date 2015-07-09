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




static MVMint32 MVM_jit_expr_add_loadreg(MVMThreadContext *tc, MVMJitExprTree *tree,
                                         MVMuint16 reg) {
    MVMint32 num        = tree->nodes_num;
    MVMJitExprNode template[] = { MVM_JIT_LOCAL,
                                  MVM_JIT_ADDR, num, reg * MVM_JIT_REG_SZ,
                                  MVM_JIT_LOAD, num + 1, MVM_JIT_REG_SZ };
    MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num + 4;
}


static MVMint32 MVM_jit_expr_add_storereg(MVMThreadContext *tc, MVMJitExprTree *tree,
                                          MVMint32 node, MVMuint16 reg) {
    MVMint32 num = tree->nodes_num;
    MVMJitExprNode template[] = { MVM_JIT_LOCAL,
                                  MVM_JIT_ADDR, num, reg * MVM_JIT_REG_SZ,
                                  MVM_JIT_STORE, num + 1, node, MVM_JIT_REG_SZ };
    MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num + 4;
}


static MVMint32 MVM_jit_expr_add_const(MVMThreadContext *tc, MVMJitExprTree *tree,
                                       MVMSpeshOperand opr, MVMuint8 info) {
    /* TODO implement this properly; this only works correctly for 64 bit values */
    MVMJitExprNode template[]  = { MVM_JIT_CONST, opr.lit_i64, sizeof(MVMint64) };
    MVMint32 num               = tree->nodes_num;
    MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num;
}

void MVM_jit_expr_load_operands(MVMThreadContext *tc, MVMJitExprTree *tree, MVMSpeshIns *ins,
                                MVMint32 *computed, MVMint32 *operands) {
    MVMint32 i;
    MVMint32 opcode = ins->info->opcode;
    if (opcode == MVM_OP_inc_i || opcode == MVM_OP_dec_i || opcode == MVM_OP_inc_u || opcode == MVM_OP_dec_u) {
        /* Don't repeat yourself? No special cases? Who are we kidding */
        MVMuint16 reg = ins->operands[i].reg.orig;
        if (computed[reg] > 0) {
            operands[0] = computed[reg];
        } else {
            operands[0] = MVM_jit_expr_add_loadreg(tc, tree, reg);
            computed[reg] = operands[0];
        }
        return;
    }

    for (i = 0; i < ins->info->num_operands; i++) {
        MVMSpeshOperand opr = ins->operands[i];
        switch(ins->info->operands[i] & MVM_operand_rw_mask) {
        case MVM_operand_read_reg:
            if (computed[opr.reg.orig] > 0) {
                operands[i] = computed[opr.reg.orig];
            } else {
                operands[i] = MVM_jit_expr_add_loadreg(tc, tree, opr.reg.orig);
                computed[opr.reg.orig] = operands[i];
            }
            break;
        case MVM_operand_literal:
            operands[i] = MVM_jit_expr_add_const(tc, tree, opr, ins->info->operands[i]);
            break;
        default:
            continue;
        }
        if (operands[i] >= tree->nodes_num || operands[i] < 0) {
            MVM_oops(tc, "JIT: something is wrong with operand loading");
        }
    }
}

/* This function is to check the internal consistency of a template
 * before I apply it.  I need this because I make a lot of mistakes in
 * writing templates, and debugging it is hard. */
static void check_template(MVMThreadContext *tc, const MVMJitExprTemplate *template, MVMSpeshIns *ins) {
    MVMint32 i;
    for (i = 0; i < template->len; i++) {
        switch(template->info[i]) {
        case 0:
            MVM_oops(tc, "JIT: Template info shorter than template length (instruction %s)", ins->info->name);
            break;
        case 'l':
            if (template->code[i] >= i || template->code[i] < 0)
                MVM_oops(tc, "JIT: Template link out of bounds (instruction: %s)", ins->info->name);
            break;
        case 'f':
            if (template->code[i] >= ins->info->num_operands || template->code[i] < 0)
                MVM_oops(tc, "JIT: Operand access out of bounds (instruction: %s)", ins->info->name);
            break;
        default:
            continue;
        }
    }
    if (template->info[i])
        MVM_oops(tc, "JIT: Template info longer than template length (instruction: %s)");
}

/* Add template to nodes, filling in operands and linking tree nodes. Return template root */
MVMint32 MVM_jit_expr_apply_template(MVMThreadContext *tc, MVMJitExprTree *tree,
                                     const MVMJitExprTemplate *template, MVMint32 *operands) {
    MVMint32 i, num;
    num = tree->nodes_num;
    MVM_DYNAR_ENSURE(tree->nodes, template->len);
    /* Loop over string until the end */
    for (i = 0; template->info[i]; i++) {
        switch (template->info[i]) {
        case 'l':
            /* link template-relative to nodes-relative */
            tree->nodes[num+i] = template->code[i] + num;
            break;
        case 'f':
            /* add operand node into the nodes */
            tree->nodes[num+i] = operands[template->code[i]];
            break;
        default:
            /* copy from template to nodes */
            tree->nodes[num+i] = template->code[i];
            break;
        }
    }
    tree->nodes_num = num + template->len;
    return num + template->root; /* root relative to nodes */
}

/* TODO add labels to the expression tree */
MVMJitExprTree * MVM_jit_expr_tree_build(MVMThreadContext *tc, MVMSpeshGraph *sg,
                                         MVMSpeshBB *bb) {
    MVMint32 operands[MVM_MAX_OPERANDS];
    MVMint32 *computed;
    MVMint32 root;
    MVMJitExprTree *tree;
    MVMSpeshIns *ins;
    MVMuint16 i;
    if (!bb->first_ins)
        return NULL;
    /* Make the tree */
    tree = MVM_malloc(sizeof(MVMJitExprTree));
    MVM_DYNAR_INIT(tree->nodes, 32);
    MVM_DYNAR_INIT(tree->roots, 8);

    /* Hold indices to the node that last computed a value belonging
     * to a register. Initialized as -1 to indicate that these
     * values are empty. */
    computed = MVM_malloc(sizeof(MVMint32)*sg->num_locals);
    memset(computed, -1, sizeof(MVMint32)*sg->num_locals);
    /* Generate a tree based on templates. The basic idea is to keep a
       index to the node that last computed the value of a local.
       Each opcode is translated to the expression using a template,
       which is a): filled with nodes coming from operands and b):
       internally linked together (relative to absolute indexes).
       Afterwards stores are inserted for computed values. */

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
        } else {
            check_template(tc, templ, ins);
        }

        MVM_jit_expr_load_operands(tc, tree, ins, computed, operands);
        root = MVM_jit_expr_apply_template(tc, tree, templ, operands);
        /* assign computed value to computed nodes */
        if ((ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
            computed[ins->operands[0].reg.orig] = root;
        } else {
            /* Terminal, add it to roots */
            MVM_DYNAR_PUSH(tree->roots, root);
        }
    }

    if (ins == NULL) {
        /* Add stores for final values */
        for (i = 0; i < sg->num_locals; i++) {
            if (computed[i] >= 0) {
                /* NB - this adds a store for variables that have only
                 * loaded. Eliminating this correctly probably requires CSE */
                MVMint32 root = MVM_jit_expr_add_storereg(tc, tree, computed[i], i);
                MVM_DYNAR_PUSH(tree->roots, root);
            }
        }
    } else {
        MVM_jit_log(tc, "Could not build an expression tree, stuck at instruction %s\n",
                    ins->info->name);
        MVM_jit_expr_tree_destroy(tc, tree);
        tree = NULL;
    }
    MVM_free(computed);
    return tree;
}

void MVM_jit_expr_tree_destroy(MVMThreadContext *tc, MVMJitExprTree *tree) {
    MVM_free(tree->nodes);
    MVM_free(tree->roots);
    MVM_free(tree);
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


void MVM_jit_expr_tree_traverse(MVMThreadContext *tc, MVMJitExprTree *tree,
                                MVMJitTreeTraverser *traverser) {
    MVMint32 i;
    for (i = 0; i < tree->roots_num; i++) {
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
void MVM_jit_expr_tree_dump(MVMThreadContext *tc, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;
    MVMint32 cur_depth = 0;
    traverser.visit = &visit_dump;
    traverser.data  = &cur_depth;
    MVM_jit_log(tc, "Starting dump of JIT expression tree\n"
                "=========================================\n");
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
    MVM_jit_log(tc, "End dump of JIT expression tree\n");
}
