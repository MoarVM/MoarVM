#include "moar.h"

/* macros used in the expression list templates, defined here so they
   don't overwrite other definitions */
#define CONST_PTR(x) ((uintptr_t)(x))
#define QUOTE(x) (x)
#define MSG(...) CONST_PTR(#__VA_ARGS__)
#define SIZEOF_MEMBER(type, member) sizeof(((type*)0)->member)

#include "core_expr_tables.h"


static const MVMJitExprOpInfo expr_op_info[] = {
#define OP_INFO(name, nchild, nargs, vtype, cast) { #name, nchild, nargs, MVM_JIT_ ## vtype, MVM_JIT_ ## cast }
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

static MVMint32 MVM_jit_expr_add_lexaddr(MVMThreadContext *tc, MVMJitExprTree *tree,
                                         MVMuint16 outers, MVMuint16 idx) {
    MVMint32 i;
    MVMint32 num = tree->nodes_num;
    /* (frame) as the root */
    MVM_DYNAR_PUSH(tree->nodes, MVM_JIT_FRAME);
    for (i = 0; i < outers; i++) {
        /* (load (addr $val (&offsetof MVMFrame outer)) (&sizeof MVMFrame*)) */
        MVMJitExprNode template[] = { MVM_JIT_ADDR, num, offsetof(MVMFrame, outer),
                                      MVM_JIT_LOAD, tree->nodes_num, sizeof(MVMFrame*) };
        MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
        num = tree->nodes_num - 3;
    }
    /* (addr (load (addr $frame (&offsetof MVMFrame env)) ptr_sz) ptr_sz*idx) */
    {
        MVMJitExprNode template[] = {
            MVM_JIT_ADDR, num, offsetof(MVMFrame, env), /* (addr $frame (&offsetof MVMFrame env)) */
            MVM_JIT_LOAD, tree->nodes_num, MVM_JIT_PTR_SZ, /* (load $addr ptr_sz) */
            MVM_JIT_ADDR, tree->nodes_num + 3, idx * MVM_JIT_REG_SZ /* (addr $frame_env idx*reg_sz) */
        };
        MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
        num = tree->nodes_num - 3;
    }
    return num;
}


static MVMint32 MVM_jit_expr_add_load(MVMThreadContext *tc, MVMJitExprTree *tree,
                                      MVMint32 addr) {
    MVMint32 num        = tree->nodes_num;
    MVMJitExprNode template[] = { MVM_JIT_LOAD, addr, MVM_JIT_REG_SZ };
    MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num;
}


static MVMint32 MVM_jit_expr_add_store(MVMThreadContext *tc, MVMJitExprTree *tree,
                                       MVMint32 addr, MVMint32 val, MVMint32 sz) {
    MVMint32 num = tree->nodes_num;
    MVMJitExprNode template[] = { MVM_JIT_STORE, addr, val, sz };
    MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num;
}


static MVMint32 MVM_jit_expr_add_cast(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint32 node, MVMint32 size, MVMint32 cast) {
    MVMint32 num = tree->nodes_num;
    MVMJitExprNode template[] = { MVM_JIT_CAST, node, size, cast };
    MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num;
}

static MVMint32 MVM_jit_expr_add_const(MVMThreadContext *tc, MVMJitExprTree *tree,
                                       MVMSpeshOperand opr, MVMuint8 info) {

    MVMJitExprNode template[]  = { MVM_JIT_CONST, 0, 0 };
    MVMint32 num               = tree->nodes_num;
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
        template[1] = MVM_jit_label_before_bb(tc, tree->graph, opr.ins_bb);
        template[2] = sizeof(MVMint32);
        break;
    case MVM_operand_spesh_slot:
        template[1] = opr.lit_i16;
        template[2] = sizeof(MVMuint16);
        break;
    default:
        MVM_oops(tc, "Can't add constant for operand type %d\n", (info & MVM_operand_type_mask) >> 3);
    }
    MVM_DYNAR_APPEND(tree->nodes, template, sizeof(template)/sizeof(MVMJitExprNode));
    return num;
}

static MVMint32 can_getlex(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshIns *ins) {
    MVMint32 outers = ins->operands[1].lex.outers;
    MVMint32 idx    = ins->operands[1].lex.idx;
    MVMStaticFrame *sf = jg->sg->sf;
    MVMuint16 *lexical_types;
    MVMint32 i;
    for (i = 0; i < outers; i++) {
        sf = sf->body.outer;
    }
    /* Use speshed lexical types, if necessary */
    lexical_types = (outers == 0 && jg->sg->lexical_types != NULL ?
                     jg->sg->lexical_types : sf->body.lexical_types);
    /* can't do getlex yet, if we have an object register */
    return lexical_types[idx] != MVM_reg_obj;
}

void MVM_jit_expr_load_operands(MVMThreadContext *tc, MVMJitExprTree *tree, MVMSpeshIns *ins,
                                MVMint32 *computed, MVMint32 *operands) {
    MVMint32 i;
    for (i = 0; i < ins->info->num_operands; i++) {
        MVMSpeshOperand opr = ins->operands[i];
        switch(ins->info->operands[i] & MVM_operand_rw_mask) {
        case MVM_operand_read_reg:
            if (computed[opr.reg.orig] > 0) {
                operands[i] = computed[opr.reg.orig];
            } else {
                MVMint32 addr = MVM_jit_expr_add_regaddr(tc, tree, opr.reg.orig);
                operands[i] = MVM_jit_expr_add_load(tc, tree, addr);
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
        case MVM_operand_read_lex:
        {
            MVMint32 addr = MVM_jit_expr_add_lexaddr(tc, tree, opr.lex.outers, opr.lex.idx);
            operands[i] = MVM_jit_expr_add_load(tc, tree, addr);
            break;
        }
        case MVM_operand_write_lex:
            operands[i] = MVM_jit_expr_add_lexaddr(tc, tree, opr.lex.outers, opr.lex.idx);
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
 * writing templates, and debugging is hard. */
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
        MVM_oops(tc, "JIT: Template info longer than template length (instruction: %s)",
                 ins->info->name);
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
static void analyze_node(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                         MVMJitExprTree *tree, MVMint32 node) {

    const MVMJitExprOpInfo   *op_info = MVM_jit_expr_op_info(tc, tree->nodes[node]);
    MVMint32   first_child = node + 1;
    MVMint32        nchild = op_info->nchild < 0 ? tree->nodes[first_child++] : op_info->nchild;
    MVMJitExprNode   *args = tree->nodes + first_child + nchild;
    MVMJitExprNodeInfo *node_info = tree->info + node;
    MVMint32 i;

    node_info->op_info   = op_info;
    /* propagate node sizes and assign labels */
    switch (tree->nodes[node]) {
    case MVM_JIT_CONST:
        /* node size is given */
        node_info->size        = args[1];
        break;
    case MVM_JIT_COPY:
        node_info->size = tree->info[tree->nodes[first_child]].size;
        break;
    case MVM_JIT_LOAD:
        node_info->size = args[0];
        break;
    case MVM_JIT_CAST:
        node_info->size = args[0];
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
        node_info->size = MVM_JIT_PTR_SZ;
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
            node_info->size = MAX(tree->info[left].size,
                                         tree->info[right].size);
            break;
        }
    case MVM_JIT_DO:
        /* node size of last child */
        {
            MVMint32 last_child = tree->nodes[first_child + nchild - 1];
            node_info->size = tree->info[last_child].size;
            break;
        }
    case MVM_JIT_IF:
        {
            MVMint32 left  = tree->nodes[first_child+1];
            MVMint32 right = tree->nodes[first_child+2];
            node_info->size = MAX(tree->info[left].size,
                                         tree->info[right].size);
            break;
        }
    case MVM_JIT_CALL:
        if (args[0] == MVM_JIT_VOID)
            node_info->size = 0;
        else if (args[0] == MVM_JIT_INT)
            node_info->size = MVM_JIT_INT_SZ;
        else if (args[0] == MVM_JIT_PTR)
            node_info->size = MVM_JIT_PTR_SZ;
        else
            node_info->size = MVM_JIT_NUM_SZ;
        break;
    default:
        /* all other things, branches, labels, when, arglist, carg,
         * comparisons, etc, have no value size */
        node_info->size = 0;
        break;
    }
    /* Insert casts as necessary */
    if (op_info->cast != MVM_JIT_NO_CAST) {
        for (i = 0; i < nchild; i++) {
            MVMint32 child = tree->nodes[first_child+i];
            if (tree->nodes[child] == MVM_JIT_CONST) {
                /* CONST nodes can always take over their target size, so they never need to be cast */
                tree->info[child].size = tree->info[node].size;
            } else if (tree->info[child].size < node_info->size) {
                /* Widening casts need to be handled explicitly, shrinking casts do not */
                MVMint32 cast = MVM_jit_expr_add_cast(tc, tree, child, node_info->size, op_info->cast);
                /* Because the cast may have grown the backing nodes array, the info array needs to grow as well */
                MVM_DYNAR_ENSURE_SIZE(tree->info, cast);
                /* And because analyze_node is called in postorder,
                   the newly added cast node would be neglected by the
                   traverser. So we traverse it explicitly.. */
                MVM_DYNAR_ENSURE_SIZE(traverser->visits, cast);
                traverser->visits[cast] = 1;
                analyze_node(tc, traverser, tree, cast);
                /* Finally we replace the child with its cast */
                tree->nodes[first_child+i] = cast;
            }
        }
    }
}

static void assign_labels(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                          MVMJitExprTree *tree, MVMint32 node) {
    /* IF has two blocks, the first I call left, the second I call right.
       Regular IF is implemented by the following sequence:

       * test
       * negated conditional jump to label 1
       * left block
       * unconditional jump to label 2
       * label 1
       * right block
       * label 2

       The 'short-circuiting' cases of IF ALL and IF ANY require
       special treatment. IF ALL simply repeats the test+negated
       branch for each of the ALL's children. IF ANY on the other hand
       must short circuit not into the default but into the
       conditional block. So IF ANY must be implemented as:

       (* test
        * conditional jump to label 3) - repeated n times
       * unconditional jump to label 1
       * label 3
       * left block
       * unconditional jump to label 2
       * label 1
       * right block
       * label 2

       NB - the label before the left block has been given the number
       3 for consistency with the regular case.

       Simpilar observations are applicable to WHEN and WHEN ANY/WHEN
       ALL.  Different altogether are the cases of ANY ALL and ALL
       ANY.

       ANY ALL can be implemented as:
       ( test
         negated conditional jump to label 4) - repeated for all in ALL
       * unconditional jump to label 3
       * label 4 (continuing the ANY)

       This way the 'short-circuit' jump of the ALL sequence implies
       the continuation of the ANY sequence, whereas the finishing of
       the ALL sequence implies it succeeded and hence the ANY needs
       to short-circuit.

       ALL ANY can be implemented analogously as:
       ( test
         conditional jump to label 4) repeated for all children of ANY
       * unconditional short-circuit jump to label 1
       * label 4

       Nested ALL in ALL and ANY in ANY all have the same
       short-circuiting behaviour (i.e.  a nested ALL in ALL is
       indistinguishable from inserting all the children of the nested
       ALL into the nesting ALL), so they don't require special
       treatment.

       All this goes to say in that the number of labels required and
       the actual labels assigned to different children depends on the
       structure of the tree, which is why labels are 'pushed down'
       from parents to children, at least when those children are ANY
       and ALL. */

    switch (tree->nodes[node]) {
    case MVM_JIT_WHEN:
        {
            /* WHEN just requires one label in the default case */
            MVMint32 test = tree->nodes[node+1];
            tree->info[node].label = tree->num_labels++;
            if (tree->nodes[test] == MVM_JIT_ANY) {
                /* ANY requires a pre-left-block label */
                tree->info[test].label = tree->num_labels++;
            } else if (tree->nodes[test] == MVM_JIT_ALL) {
                /* ALL takes over the label of its parent */
                tree->info[test].label = tree->info[node].label;
            } else {

            }
        }
        break;
    case MVM_JIT_IF:
    case MVM_JIT_EITHER:
        {
            MVMint32 test = tree->nodes[node+1];
            /* take two labels, one for the left block and one for the right block */
            tree->info[node].label = tree->num_labels;
            tree->num_labels += 2;
            if (tree->nodes[test] == MVM_JIT_ANY) {
                /* assign 'label 3' to the ANY */
                tree->info[test].label = tree->num_labels++;
            } else if (tree->nodes[test] == MVM_JIT_ALL) {
                /* assign 'label 1' to the ALL */
                tree->info[test].label = tree->info[node].label;
            } else {
                /* regular case, no work necessary now */
            }
        }
        break;
    case MVM_JIT_ALL:
        {
            MVMint32 nchild = tree->nodes[node+1];
            MVMint32 i;
            for (i = 0; i < nchild; i++) {
                MVMint32 test = tree->nodes[node+2+i];
                if (tree->nodes[test] == MVM_JIT_ALL) {
                    /* use same label for child as parent */
                    tree->info[test].label = tree->info[node].label;
                } else if (tree->nodes[test] == MVM_JIT_ANY) {
                    /* assign an extra label for ANY to short-circuit into */
                    tree->info[test].label = tree->num_labels++;
                }
            }
        }
        break;
    case MVM_JIT_ANY:
        {
            MVMint32 nchild = tree->nodes[node+1];
            MVMint32 i;
            for (i = 0; i < nchild; i++) {
                MVMint32 test = tree->nodes[node+2+i];
                if (tree->nodes[test] == MVM_JIT_ANY) {
                    tree->info[test].label = tree->info[node].label;
                } else if (tree->nodes[test] == MVM_JIT_ALL) {
                    tree->info[test].label = tree->num_labels++;
                }
            }
        }
        break;
    default:
        break;
    }
}


void MVM_jit_expr_tree_analyze(MVMThreadContext *tc, MVMJitExprTree *tree) {
    /* analyse the tree, calculate usage and destination information */
    MVMJitTreeTraverser traverser;
    MVM_DYNAR_INIT(tree->info, tree->nodes_num);
    traverser.policy    = MVM_JIT_TRAVERSER_ONCE;
    traverser.data      = NULL;
    traverser.preorder  = &assign_labels;
    traverser.inorder   = NULL;
    traverser.postorder = &analyze_node;
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
}

/* TODO add labels to the expression tree */
MVMJitExprTree * MVM_jit_expr_tree_build(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshIterator *iter) {
    MVMSpeshGraph *sg = jg->sg;
    MVMSpeshIns *entry = iter->ins;
    MVMSpeshIns *ins;
    MVMJitExprTree *tree;
    MVMint32 operands[MVM_MAX_OPERANDS];
    MVMint32 *computed;
    MVMint32 root;
    MVMuint16 i;
    /* No instructions, just skip */
    if (!iter->ins)
        return NULL;
    /* Make the tree */
    tree = MVM_malloc(sizeof(MVMJitExprTree));
    MVM_DYNAR_INIT(tree->nodes, 32);
    MVM_DYNAR_INIT(tree->roots, 8);
    tree->graph      = jg;
    tree->info       = NULL;
    tree->num_labels = 0;
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
    for (ins = iter->ins; ins; ins = MVM_spesh_iterator_next_ins(tc, iter)) {
        /* NB - we probably will want to involve the spesh info in selecting a
           template. And for optimisation, I'd like to copy spesh facts (if any)
           to the tree info */
        MVMuint16 opcode = ins->info->opcode;
        MVMSpeshAnn *ann;
        const MVMJitExprTemplate *template;
        if (opcode == MVM_SSA_PHI || opcode == MVM_OP_no_op) {
            continue;
        }

        /* check if this is a getlex and if we can handle it */
        if (opcode == MVM_OP_getlex && !can_getlex(tc, jg, ins)) {
            goto done;
        }
        /* Check annotations that require handling before the expression  */
        for (ann = ins->annotations; ann != NULL; ann = ann->next) {
            switch (ann->type) {
            case MVM_SPESH_ANN_DEOPT_OSR:
                /* If we have a deopt annotation in the middle of the tree, it
                 * breaks the expression because the interpreter is allowed to
                 * jump right in the middle of the block. It is much simpler not
                 * to allow that. On the other hand, if this is the first node
                 * of the block, the graph builder must already have handled
                 * it, and we may continue.. */
                if (tree->nodes_num > 0) {
                    goto done;
                }
            default:
                break;
            }
        }

        template = MVM_jit_get_template_for_opcode(opcode);
        if (template == NULL) {
            /* we don't have a template for this yet, so we can't
             * convert it to an expression */
            MVM_jit_log(tc, "Cannot get template for %s\n", ins->info->name);
            goto done;
        } else {
            check_template(tc, template, ins);
        }

        MVM_jit_expr_load_operands(tc, tree, ins, computed, operands);
        root = MVM_jit_expr_apply_template(tc, tree, template, operands);

        /* if this operation writes a register, it typically yields a value */
        if ((ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg &&
            /* destructive templates are responsible for writing their
               own value to memory, and do not yield an expression */
            (template->flags & MVM_JIT_EXPR_TEMPLATE_DESTRUCTIVE) == 0) {
            MVMuint16 reg = ins->operands[0].reg.orig;
            /* assign computed value to computed nodes */
            computed[reg] = root;
            /* and add a store, which becomes the root */
            root = MVM_jit_expr_add_store(tc, tree, operands[0], root, MVM_JIT_REG_SZ);
        }
        /* TODO implement post-instruction annotation handling (e.g. throwish, invokish) */
        /* Add root to tree to ensure source evaluation order */
        MVM_DYNAR_PUSH(tree->roots, root);
    }

 done:
    if (tree->nodes_num > 0) {
        MVM_jit_expr_tree_analyze(tc, tree);
        MVM_jit_log(tc, "Build tree out of: [");
        for (ins = entry; ins != iter->ins; ins = ins->next) {
            MVM_jit_log(tc, "%s, ", ins->info->name);
        }
        MVM_jit_log(tc, "]\n");
    } else {
        /* Don't return empty trees, nobody wants that */
        MVM_jit_expr_tree_destroy(tc, tree);
        tree = NULL;
    }
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
    if (traverser->policy == MVM_JIT_TRAVERSER_ONCE &&
        traverser->visits[node] >= 1)
        return;

    traverser->visits[node]++;
    /* visiting on the way down - NB want to add visitation information */
    if (traverser->preorder)
        traverser->preorder(tc, traverser, tree, node);
    if (nchild < 0) {
        /* Variadic case: take first child as constant signifying the
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
    MVM_DYNAR_INIT(traverser->visits, tree->nodes_num);
    for (i = 0; i < tree->roots_num; i++) {
        /* TODO deal with nodes with multiple entries */
        walk_tree(tc, tree, traverser, tree->roots[i]);
    }
    MVM_free(traverser->visits);
}


#define FIRST_CHILD(t,x) (t->info[x].op_info->nchild < 0 ? x + 2 : x + 1)
/* Walk tree to get nodes along a path */
MVMint32 MVM_jit_expr_tree_get_nodes(MVMThreadContext *tc, MVMJitExprTree *tree,
                                     MVMint32 node, const char *path,
                                     MVMJitExprNode *buffer) {
    MVMJitExprNode *ptr = buffer;
    while (*path) {
        MVMJitExprNode cur_node = node;
        do {
            MVMint32 first_child = FIRST_CHILD(tree, cur_node) - 1;
            MVMint32 child_nr    = *path++ - '0';
            cur_node = tree->nodes[first_child+child_nr];
        } while (*path != '.');
        /* regs nodes go to values, others to args */
        *ptr++ = cur_node;
        path++;
    }
    return ptr - buffer;
}
#undef FIRST_CHILD
