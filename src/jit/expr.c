#include "moar.h"
#include "expr_tables.h"


static const MVMJitExprOpInfo expr_op_info[] = {
#define OP_INFO(name, nchild, nargs, vtype) { #name, nchild, nargs, MVM_JIT_ ## vtype }
    MVM_JIT_IR_OPS(OP_INFO)
#undef OP_INFO
};

const MVMJitExprOpInfo * MVM_jit_expr_op_info(MVMThreadContext *tc, MVMJitExprNode op) {
    if (op < 0 || op >= MVM_JIT_MAX_NODES) {
        MVM_oops(tc, "JIT: Expr op index out of bounds: %"PRId64, op);
    }
    return &expr_op_info[op];
}

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

    MVMJitExprNode template[]  = { MVM_JIT_CONST, 0, 0 };
    switch(info & MVM_operand_type_mask) {
    case MVM_operand_int8:
        template[1] = opr.lit_i8;
        template[2] = sizeof(MVMint8);
        break;
    case MVM_operand_int16:
        template[1] = opr.lit_i16;
        template[2] = sizeof(MVMint16);
        break;
    case MVM_operand_int32:
        template[1] = opr.lit_i32;
        template[2] = sizeof(MVMint32);
        break;
    case MVM_operand_int64:
        template[1] = opr.lit_i64;
        template[2] = sizeof(MVMint64);
        break;
    case MVM_operand_num32:
        /* possible endianess issue here */
        template[1] = opr.lit_i32;
        template[2] = sizeof(MVMnum32);
        break;
    case MVM_operand_num64:
        /* use i64 to get the bits */
        template[1] = opr.lit_i64;
        template[2] = sizeof(MVMnum64);
        break;
    case MVM_operand_str:
        /* string index really */
        template[1] = opr.lit_str_idx;
        template[2] = sizeof(MVMuint32);
        break;
    case MVM_operand_ins:
    case MVM_operand_coderef:
        /* use uintptr_t to convert to integer - shouold convert to label */
        template[1] = MVM_jit_graph_get_label_for_bb(tc, tree->graph, opr.ins_bb);
        template[2] = sizeof(MVMint32);
        break;
    case MVM_operand_spesh_slot:
        template[1] = opr.lit_i16;
        template[2] = sizeof(MVMuint16);
        break;
    default:
        MVM_oops(tc, "Can't add constant for operand type %d\n", (info & MVM_operand_type_mask) >> 3);
    }

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
        MVMuint16 reg = ins->operands[0].reg.orig;
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
        case 'r':
            /* add a root */
            MVM_DYNAR_PUSH(tree->roots, num+i);
            /* fall through */
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
MVMJitExprTree * MVM_jit_expr_tree_build(MVMThreadContext *tc, MVMJitGraph *jg,
                                         MVMSpeshBB *bb) {
    MVMint32 operands[MVM_MAX_OPERANDS];
    MVMint32 *computed;
    MVMint32 root;
    MVMSpeshGraph *sg = jg->sg;
    MVMJitExprTree *tree;
    MVMSpeshIns *ins;
    MVMuint16 i;
    if (!bb->first_ins)
        return NULL;
    /* Make the tree */
    tree = MVM_malloc(sizeof(MVMJitExprTree));
    MVM_DYNAR_INIT(tree->nodes, 32);
    MVM_DYNAR_INIT(tree->roots, 8);
    tree->graph = jg;
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
        }
        /* Add it to roots to ensure source evaluation order */
        MVM_DYNAR_PUSH(tree->roots, root);
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

static void walk_tree(MVMThreadContext *tc, MVMJitExprTree *tree,
                 MVMJitTreeTraverser *traverser, MVMint32 node) {
    const MVMJitExprOpInfo *info = MVM_jit_expr_op_info(tc, tree->nodes[node]);
    MVMint32 nchild = info->nchild;
    MVMint32 first_child = node + 1;
    MVMint32 i;
    traverser->visits[node]++;
    /* visiting on the way down - NB want to add visitation information */
    if (traverser->preorder)
        traverser->preorder(tc, traverser, tree, node);
    if (nchild < 0) {
        /* ARGLIST case: take first child as constant signifying the
         * number of children. Increment because the 'real' children now
         * start node later */
        nchild = tree->nodes[first_child++];
    }
    for (i = 0; i < nchild; i++) {
        /* Enter child node */
        walk_tree(tc, tree, traverser, tree->nodes[first_child+i]);
        if (traverser->inorder) {
            traverser->inorder(tc, traverser, tree, node, i);
        }
    }
    if (traverser->postorder) {
        traverser->postorder(tc, traverser, tree, node);
    }
}


void MVM_jit_expr_tree_traverse(MVMThreadContext *tc, MVMJitExprTree *tree,
                                MVMJitTreeTraverser *traverser) {
    MVMint32 i;
    traverser->visits = MVM_calloc(tree->nodes_num, sizeof(MVMint32));
    for (i = 0; i < tree->roots_num; i++) {
        /* TODO deal with nodes with multiple entries */
        walk_tree(tc, tree, traverser, tree->roots[i]);
    }
    MVM_free(traverser->visits);
}

