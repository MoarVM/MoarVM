#include "moar.h"

/* macros used in the expression list templates, defined here so they
   don't overwrite other definitions */
#define CONST_PTR(x) ((uintptr_t)(x))
#define QUOTE(x) (x)
#define MSG(...) CONST_PTR(#__VA_ARGS__)
#define SIZEOF_MEMBER(type, member) sizeof(((type*)0)->member)

#include "core_expr_tables.h"


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

static MVMint32 MVM_jit_expr_add_regaddr(MVMThreadContext *tc, MVMJitExprTree *tree,
                                         MVMuint16 reg) {
    MVMint32 num  = tree->nodes_num;
    MVMJitExprNode template[] = { MVM_JIT_LOCAL,
                                  MVM_JIT_ADDR, num, reg * MVM_JIT_REG_SZ };
    MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num + 1;
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


static MVMint32 MVM_jit_expr_add_store(MVMThreadContext *tc, MVMJitExprTree *tree,
                                       MVMint32 addr, MVMint32 val, MVMint32 sz) {
    MVMint32 num = tree->nodes_num;
    MVMJitExprNode template[] = { MVM_JIT_STORE, addr, val, sz };
    MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num;
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
    case MVM_operand_coderef:
        template[1] = opr.coderef_idx;
        template[2] = sizeof(MVMuint16);
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
        case MVM_operand_write_reg:
            /* get address of register to write */
            operands[i] = MVM_jit_expr_add_regaddr(tc, tree, opr.reg.orig);
            break;
        case MVM_operand_literal:
            operands[i] = MVM_jit_expr_add_const(tc, tree, opr, ins->info->operands[i]);
            break;
        default:
            /* TODO implement readlex and writelex */
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
    MVM_DYNAR_ENSURE_SPACE(tree->nodes, template->len);
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



/* Collect tree analysis information, add stores of computed values */
static void analyze_tree(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                         MVMJitExprTree *tree, MVMint32 node) {
    MVMSpeshIns **node_ins = traverser->data;
    const MVMJitExprOpInfo   *op = MVM_jit_expr_op_info(tc, tree->nodes[node]);
    MVMint32   first_child = node + 1;
    MVMint32        nchild = op->nchild < 0 ? tree->nodes[first_child++] : op->nchild;
    MVMJitExprNode   *args = tree->nodes + first_child + nchild;
    MVMJitExprNodeInfo *node_info = tree->info + node;
    MVMint32 i;
    if (traverser->visits[node] > 1)
        return;

    node_info->op_info   = op;
    node_info->spesh_ins = node_ins[node];
    node_info->first_use = INT32_MAX;
    node_info->last_use  = -1;
    node_info->num_use   = 0;
    if (node_info->spesh_ins &&
        (node_info->spesh_ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
        node_info->local_addr = node_info->spesh_ins->operands[0].reg.orig;
    } else {
        node_info->local_addr = -1;
    }
    /* propagate node sizes */
    switch (tree->nodes[node]) {
    case MVM_JIT_CONST:
        /* node size is given */
        node_info->value.size = args[1];
        break;
    case MVM_JIT_COPY:
        node_info->value.size = tree->info[tree->nodes[first_child]].value.size;
        break;
    case MVM_JIT_LOAD:
        node_info->value.size = args[0];
        break;
    case MVM_JIT_ADDR:
    case MVM_JIT_IDX:
    case MVM_JIT_LABEL:
    case MVM_JIT_TC:
    case MVM_JIT_CU:
    case MVM_JIT_FRAME:
    case MVM_JIT_LOCAL:
    case MVM_JIT_STACK:
    case MVM_JIT_VMNULL:
        /* addresses result in pointers */
        node_info->value.size = MVM_JIT_PTR_SZ;
        break;
    case MVM_JIT_ADD:
    case MVM_JIT_SUB:
    case MVM_JIT_AND:
    case MVM_JIT_OR:
    case MVM_JIT_XOR:
    case MVM_JIT_NOT:
        {
            /* arithmetic nodes use their largest operand */
            MVMint32 left  = tree->nodes[first_child];
            MVMint32 right = tree->nodes[first_child+1];
            node_info->value.size = MAX(tree->info[left].value.size,
                                         tree->info[right].value.size);
            break;
        }
    case MVM_JIT_DO:
        /* node size of last child */
        {
            MVMint32 last_child = tree->nodes[first_child + nchild - 1];
            node_info->value.size = tree->info[last_child].value.size;
            break;
        }
    case MVM_JIT_IF:
        {
            MVMint32 left  = tree->nodes[first_child+1];
            MVMint32 right = tree->nodes[first_child+2];
            node_info->value.size = MAX(tree->info[left].value.size,
                                         tree->info[right].value.size);
            break;
        }
    case MVM_JIT_CALL:
        if (args[0] == MVM_JIT_VOID)
            node_info->value.size = 0;
        else if (args[0] == MVM_JIT_INT)
            node_info->value.size = MVM_JIT_INT_SZ;
        else if (args[0] == MVM_JIT_PTR)
            node_info->value.size = MVM_JIT_PTR_SZ;
        else
            node_info->value.size = MVM_JIT_NUM_SZ;
        break;
    default:
        /* all other things, branches, labels, when, arglist, carg,
         * comparisons, etc, have no value size */
        node_info->value.size = 0;
        break;
    }

    for (i = 0; i < nchild; i++) {
        MVMint32 child  = tree->nodes[first_child+i];
        MVMJitExprNodeInfo *child_info = tree->info + child;
        child_info->first_use = MIN(child_info->first_use, node);
        child_info->last_use  = MAX(child_info->last_use, node);
        child_info->num_use  += 1;
    }
}


void MVM_jit_expr_tree_analyze(MVMThreadContext *tc, MVMJitExprTree *tree, MVMSpeshIns **node_ins) {
    /* analyse the tree, calculate usage and destination information */
    MVMJitTreeTraverser traverser;
    MVM_DYNAR_INIT(tree->info, tree->nodes_num);
    traverser.data      = node_ins;
    traverser.preorder  = NULL;
    traverser.inorder   = NULL;
    traverser.postorder = &analyze_tree;
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
}

/* TODO add labels to the expression tree */
MVMJitExprTree * MVM_jit_expr_tree_build(MVMThreadContext *tc, MVMJitGraph *jg,
                                         MVMSpeshBB *bb) {
    MVMint32 operands[MVM_MAX_OPERANDS];
    MVMint32 *computed;
    MVMint32 root;
    MVMSpeshGraph *sg = jg->sg;
    MVMSpeshIns *ins;
    MVMJitExprTree *tree;
    MVM_DYNAR_DECL(MVMSpeshIns*, node_ins);
    MVMuint16 i;


    if (!bb->first_ins)
        return NULL;
    /* Make the tree */
    tree = MVM_malloc(sizeof(MVMJitExprTree));
    MVM_DYNAR_INIT(tree->nodes, 32);
    MVM_DYNAR_INIT(tree->roots, 8);
    tree->graph = jg;
    tree->info  = NULL;
    /* Hold indices to the node that last computed a value belonging
     * to a register. Initialized as -1 to indicate that these
     * values are empty. */
    computed = MVM_malloc(sizeof(MVMint32)*sg->num_locals);
    memset(computed, -1, sizeof(MVMint32)*sg->num_locals);
    /* Hold a mapping of nodes to spesh instructions. This is presumably
       useful in optimization and code generation */
    MVM_DYNAR_INIT(node_ins, tree->nodes_alloc);

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

        /* map to spesh ins */
        MVM_DYNAR_ENSURE_SIZE(node_ins, tree->nodes_num);
        node_ins[root] = ins;

        /* if this operation writes a register, it typically yields a value */
        if ((ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg &&
            /* destructive templates are responsible for writing their
               own value to memory, and do not yield an expression */
            (templ->flags & MVM_JIT_EXPR_TEMPLATE_DESTRUCTIVE) == 0) {
            MVMuint16 reg = ins->operands[0].reg.orig;
            /* assign computed value to computed nodes */
            computed[reg] = root;
            /* and add a store, which becomes the root */
            root = MVM_jit_expr_add_store(tc, tree, operands[0], root, MVM_JIT_REG_SZ);
        }
        /* Add root to tree to ensure source evaluation order */
        MVM_DYNAR_PUSH(tree->roots, root);
    }

    if (ins == NULL) {
        MVM_jit_expr_tree_analyze(tc, tree, node_ins);
    } else {
        MVM_jit_log(tc, "Could not build an expression tree, stuck at instruction %s\n",
                    ins->info->name);
        MVM_jit_expr_tree_destroy(tc, tree);
        tree = NULL;
    }
    MVM_free(node_ins);
    MVM_free(computed);
    return tree;
}

void MVM_jit_expr_tree_destroy(MVMThreadContext *tc, MVMJitExprTree *tree) {
    if (tree->info)
        MVM_free(tree->info);
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

/* TODO specify revisiting policy */
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

