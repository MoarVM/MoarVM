#include "moar.h"

/* Mathematical min and max macro's */
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b));
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b));
#endif


/* macros used in the expression list templates, defined here so they
   don't overwrite other definitions */
#define CONST_PTR(x) ((uintptr_t)(x))
#define QUOTE(x) (x)
#define MSG(...) CONST_PTR(#__VA_ARGS__)
#define SIZEOF_MEMBER(type, member) sizeof(((type*)0)->member)

#include "jit/core_templates.h"


static const MVMJitExprOpInfo expr_op_info[] = {
#define OP_INFO(name, nchild, nargs, vtype, cast) { #name, nchild, nargs, MVM_JIT_ ## vtype, MVM_JIT_ ## cast }
    MVM_JIT_EXPR_OPS(OP_INFO)
#undef OP_INFO
};


const MVMJitExprOpInfo * MVM_jit_expr_op_info(MVMThreadContext *tc, MVMint32 op) {
    if (op < 0 || op >= MVM_JIT_MAX_NODES) {
        MVM_oops(tc, "JIT: Expr op index out of bounds: %d", op);
    }
    return &expr_op_info[op];
}

/* Record the node that defines a value */
struct ValueDefinition {
    MVMint32 node;
    MVMint32 root;
    MVMint32 addr;
};



/* Logical negation of MVMJitExprOp flags. */
MVMint32 MVM_jit_expr_op_negate_flag(MVMThreadContext *tc, MVMint32 op) {
    switch(op) {
    case MVM_JIT_LT:
        return MVM_JIT_GE;
    case MVM_JIT_LE:
        return MVM_JIT_GT;
    case MVM_JIT_EQ:
        return MVM_JIT_NE;
    case MVM_JIT_NE:
        return MVM_JIT_EQ;
    case MVM_JIT_GE:
        return MVM_JIT_LT;
    case MVM_JIT_GT:
        return MVM_JIT_LE;
    case MVM_JIT_NZ:
        return MVM_JIT_ZR;
    case MVM_JIT_ZR:
        return MVM_JIT_NZ;
    default:
        break;
    }
    return -1; /* not a flag */
}

MVMint32 MVM_jit_expr_op_is_binary_noncommutative(MVMThreadContext *tc, MVMint32 op) {
    switch (op) {
    case MVM_JIT_SUB:
    case MVM_JIT_XOR:
        /* and DIV, SHIFT, etc */
        return 1;
    default:
        /* ADD, MUL, AND, OR, etc. are commutative */
        return 0;
    }
}


static MVMint32 MVM_jit_expr_add_regaddr(MVMThreadContext *tc, MVMJitExprTree *tree,
                                         MVMuint16 reg) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "nnl.",
                                             MVM_JIT_LOCAL,
                                             MVM_JIT_ADDR, 0, reg * MVM_JIT_REG_SZ) + 1;
}

static MVMint32 MVM_jit_expr_add_loadframe(MVMThreadContext *tc, MVMJitExprTree *tree) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "nnl.nl.",
                                             MVM_JIT_TC,
                                             MVM_JIT_ADDR, 0, offsetof(MVMThreadContext, cur_frame),
                                             MVM_JIT_LOAD, 1, sizeof(MVMFrame*)) + 4;
}

static MVMint32 MVM_jit_expr_add_load(MVMThreadContext *tc, MVMJitExprTree *tree,
                                      MVMint32 addr) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "n..", MVM_JIT_LOAD, addr, MVM_JIT_REG_SZ);
}

static MVMint32 MVM_jit_expr_add_store(MVMThreadContext *tc, MVMJitExprTree *tree,
                                       MVMint32 addr, MVMint32 val, MVMint32 sz) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "n...", MVM_JIT_STORE, addr, val, sz);
}

static MVMint32 MVM_jit_expr_add_cast(MVMThreadContext *tc, MVMJitExprTree *tree,
                                      MVMint32 node, MVMint32 to_size, MVMint32 from_size, MVMint32 is_signed) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "n....", MVM_JIT_CAST, node, to_size, from_size, is_signed);
}

static MVMint32 MVM_jit_expr_add_label(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint32 label) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "n.", MVM_JIT_MARK, label);
}

static MVMint32 MVM_jit_expr_add_lexaddr(MVMThreadContext *tc, MVMJitExprTree *tree,
                                         MVMuint16 outers, MVMuint16 idx) {
    MVMint32 i;
    /* (frame) as the root */
    MVMint32 num = MVM_jit_expr_add_loadframe(tc, tree);
    for (i = 0; i < outers; i++) {
        /* (load (addr $val (&offsetof MVMFrame outer)) (&sizeof MVMFrame*)) */
        num = MVM_jit_expr_apply_template_adhoc(tc, tree, "n..nl.",
                                                MVM_JIT_ADDR, num, offsetof(MVMFrame, outer),
                                                MVM_JIT_LOAD, 0, sizeof(MVMFrame*)) + 3;

    }
    /* (addr (load (addr $frame (&offsetof MVMFrame env)) ptr_sz) ptr_sz*idx) */
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "n..nl.nl.",
                                             /* (addr $frame (&offsetof MVMFrame env)) */
                                             MVM_JIT_ADDR, num, offsetof(MVMFrame, env),
                                             /* (load $addr ptr_sz) */
                                             MVM_JIT_LOAD, 0, MVM_JIT_PTR_SZ,
                                             /* (addr $frame_env idx*reg_sz) */
                                             MVM_JIT_ADDR, 3, idx * MVM_JIT_REG_SZ) + 6;
}


static MVMint32 MVM_jit_expr_add_const(MVMThreadContext *tc, MVMJitExprTree *tree,
                                       MVMSpeshOperand opr, MVMuint8 info) {

    MVMJitExprNode template[] = { MVM_JIT_CONST, 0, 0 };
    MVMint32 num        = tree->nodes_num;
    MVMint32 size       = 3;
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
        template[0] = MVM_JIT_LABEL;
        template[1] = MVM_jit_label_before_bb(tc, tree->graph, opr.ins_bb);
        size        = 2;
        break;
    case MVM_operand_callsite:
        template[1] = opr.callsite_idx;
        template[2] = sizeof(MVMuint16);
        break;
    case MVM_operand_spesh_slot:
        template[1] = opr.lit_i16;
        template[2] = sizeof(MVMuint16);
        break;
    default:
        MVM_oops(tc, "Can't add constant for operand type %d\n", (info & MVM_operand_type_mask) >> 3);
    }
    MVM_VECTOR_APPEND(tree->nodes, template, size);
    return num;
}

static MVMint32 getlex_needs_autoviv(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshIns *ins) {
    MVMSpeshOperand opr = ins->operands[1];
    MVMuint16 lexical_type = MVM_spesh_get_lex_type(tc, jg->sg, opr.lex.outers, opr.lex.idx);
    return lexical_type == MVM_reg_obj;
}

static MVMint32 bindlex_needs_write_barrier(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshIns *ins) {
    MVMSpeshOperand opr = ins->operands[0];
    MVMuint16 lexical_type = MVM_spesh_get_lex_type(tc, jg->sg, opr.lex.outers, opr.lex.idx);
    /* need to hit a write barrier if we bindlex to a string */
    return lexical_type == MVM_reg_obj || lexical_type == MVM_reg_str;
}


static MVMint32 ins_has_single_input_output_operand(MVMSpeshIns *ins) {
    switch (ins->info->opcode) {
    case MVM_OP_inc_i:
    case MVM_OP_inc_u:
    case MVM_OP_dec_i:
    case MVM_OP_dec_u:
        return 1;
      default:
          break;
    }
    return 0;
}

void MVM_jit_expr_load_operands(MVMThreadContext *tc, MVMJitExprTree *tree, MVMSpeshIns *ins,
                                struct ValueDefinition *values, MVMint32 *operands) {
    MVMint32 i;
    for (i = 0; i < ins->info->num_operands; i++) {
        MVMSpeshOperand opr = ins->operands[i];
        MVMint8    opr_kind = ins->info->operands[i];
        switch(opr_kind & MVM_operand_rw_mask) {
        case MVM_operand_read_reg:
            if (values[opr.reg.orig].node >= 0) {
                operands[i] = values[opr.reg.orig].node;
            } else {
                MVMint32 addr = MVM_jit_expr_add_regaddr(tc, tree, opr.reg.orig);
                operands[i] = MVM_jit_expr_add_load(tc, tree, addr);
                values[opr.reg.orig].node = operands[i];
                values[opr.reg.orig].addr = addr;
                values[opr.reg.orig].root = -1; /* load is not part of a root */
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

    /* A HACK.
     *
     * dec_i and inc_i have a single operand that acts both as input and output.
     * This is marked only as an output operand, though. Thus, we load the
     * address here, and define the value later. However, if we have multiple of
     * these in sequence, each will load the old value from memory, disregarding
     * the value that an earlier operator has defined, i.e. losing the update.
     * That's a bug, and this tries to fix it, by forcing a 'split' between the
     * input and the output operand.
     */
    if (ins_has_single_input_output_operand(ins)) {
        MVMuint16 reg = ins->operands[0].reg.orig;
        if (values[reg].node >= 0) {
            operands[1] = values[reg].node;
        } else {
            /* operands[0] has the address */
            operands[1] = MVM_jit_expr_add_load(tc, tree, operands[0]);
            /* no need to insert it in the table since it will be directly
             * overwritten */
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
            if (template->code[i] < 0 ||
                (template->code[i] >= ins->info->num_operands &&
                 !ins_has_single_input_output_operand(ins)))
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
static MVMint32 apply_template(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint32 len, char *info,
                               MVMJitExprNode *code, MVMint32 *operands) {
    MVMint32 i, num;
    num = tree->nodes_num;
    MVM_VECTOR_ENSURE_SPACE(tree->nodes, len);
    /* Loop over string until the end */
    for (i = 0; info[i]; i++) {
        switch (info[i]) {
        case 'l':
            /* link template-relative to nodes-relative */
            tree->nodes[num+i] = code[i] + num;
            break;
        case 'f':
            /* add operand node into the nodes */
            tree->nodes[num+i] = operands[code[i]];
            break;
        default:
            /* copy from template to nodes (./n) */
            tree->nodes[num+i] = code[i];
            break;
        }
    }
    tree->nodes_num = num + len;
    return num;
}

MVMint32 MVM_jit_expr_apply_template(MVMThreadContext *tc, MVMJitExprTree *tree,
                                     const MVMJitExprTemplate *template, MVMint32 *operands) {
    return apply_template(tc, tree, template->len, (char*)template->info,
                          (MVMJitExprNode*)template->code, operands) + template->root;
}

/* this will fail with more than 16 nodes, which is just as fine */
MVMint32 MVM_jit_expr_apply_template_adhoc(MVMThreadContext *tc, MVMJitExprTree *tree,
                                           char *info, ...) {
    MVMJitExprNode code[16];
    MVMint32 i;
    va_list args;
    va_start(args, info);
    for (i = 0; info[i] != 0; i++) {
        code[i] = va_arg(args, MVMint32);
    }
    va_end(args);
    return apply_template(tc, tree, i, info, code, NULL);
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
    case MVM_JIT_LOCAL:
    case MVM_JIT_STACK:
        /* addresses result in pointers */
        node_info->size = MVM_JIT_PTR_SZ;
        break;
        /* binary operations */
    case MVM_JIT_ADD:
    case MVM_JIT_SUB:
    case MVM_JIT_AND:
    case MVM_JIT_OR:
    case MVM_JIT_XOR:
    case MVM_JIT_NOT:
        /* comparisons */
    case MVM_JIT_NE:
    case MVM_JIT_LT:
    case MVM_JIT_LE:
    case MVM_JIT_EQ:
    case MVM_JIT_GE:
    case MVM_JIT_GT:
        {
            /* arithmetic nodes use their largest operand */
            MVMint32 left  = tree->nodes[first_child];
            MVMint32 right = tree->nodes[first_child+1];
            node_info->size = MAX(tree->info[left].size,
                                  tree->info[right].size);
            break;
        }
    case MVM_JIT_FLAGVAL:
        /* XXX THIS IS A HACK
         *
         * The true size of 'flagval' is a single byte.  But that would mean it
         * had to be upcast to be used as a 64-bit word, and that subtly
         * doesn't work if the value is STORE-d to memory. */
        node_info->size = 4;
        break;
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
        node_info->size = args[0];
        break;
    case MVM_JIT_NZ:
    case MVM_JIT_ZR:
        node_info->size = tree->info[tree->nodes[first_child]].size;
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
                MVMint32 cast = MVM_jit_expr_add_cast(tc, tree, child, node_info->size, tree->info[child].size, op_info->cast);
                /* Because the cast may have grown the backing nodes array, the info array needs to grow as well */
                MVM_VECTOR_ENSURE_SIZE(tree->info, cast);
                /* And because analyze_node is called in postorder,
                   the newly added cast node would be neglected by the
                   traverser. So we traverse it explicitly.. */
                MVM_VECTOR_ENSURE_SIZE(traverser->visits, cast);
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

       The 'short-circuiting' cases of IF ALL and IF ANY require special
       treatment. IF ALL simply repeats the test+negated branch for each of the
       ALL's children. IF ANY on the other hand must short circuit not into the
       default (right) but into the (left) conditional block. So IF ANY must be
       implemented as:

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
            }
        }
        break;
    case MVM_JIT_IF:
    case MVM_JIT_IFV:
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
    MVM_VECTOR_ENSURE_SIZE(tree->info, tree->nodes_num);
    traverser.policy    = MVM_JIT_TRAVERSER_ONCE;
    traverser.data      = NULL;
    traverser.preorder  = &assign_labels;
    traverser.inorder   = NULL;
    traverser.postorder = &analyze_node;
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
}



/* insert stores for all the active unstored values */
static void active_values_flush(MVMThreadContext *tc, MVMJitExprTree *tree,
                                struct ValueDefinition *values, MVMint32 num_values) {
    MVMint32 i;
    for (i = 0; i < num_values; i++) {
        if (values[i].root >= 0) {
            tree->roots[values[i].root] = MVM_jit_expr_add_store(
                tc, tree, values[i].addr,
                values[i].node, MVM_JIT_REG_SZ
            );
        }
        if (values[i].node >= 0) {
            memset(values + i, -1, sizeof(struct ValueDefinition));
        }
    }
}


/* TODO add labels to the expression tree */
MVMJitExprTree * MVM_jit_expr_tree_build(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshIterator *iter) {
    MVMSpeshGraph *sg = jg->sg;
    MVMSpeshIns *entry = iter->ins;
    MVMSpeshIns *ins;
    MVMJitExprTree *tree;
    MVMint32 operands[MVM_MAX_OPERANDS];
    struct ValueDefinition *values;
    MVMint32 root, node;
    MVMuint16 i;
    /* No instructions, just skip */
    if (!iter->ins)
        return NULL;

    /* Make the tree */
    tree = MVM_malloc(sizeof(MVMJitExprTree));
    MVM_VECTOR_INIT(tree->nodes, 256);
    MVM_VECTOR_INIT(tree->info,  256);
    MVM_VECTOR_INIT(tree->roots, 16);

    /* ensure that all references are nonzero */
    MVM_VECTOR_PUSH(tree->nodes, MVM_JIT_NOOP);

    tree->graph      = jg;
    tree->num_labels = 0;
    /* Hold indices to the node that last computed a value belonging
     * to a register. Initialized as -1 to indicate that these
     * values are empty. */
    values = MVM_malloc(sizeof(struct ValueDefinition)*sg->num_locals);
    memset(values, -1, sizeof(struct ValueDefinition)*sg->num_locals);

#define BAIL(x, ...) do { if (x) { MVM_jit_log(tc, __VA_ARGS__); goto done; } } while (0)

    /* Generate a tree based on templates. The basic idea is to keep a
       index to the node that last computed the value of a local.
       Each opcode is translated to the expression using a template,
       which is a): filled with nodes coming from operands and b):
       internally linked together (relative to absolute indexes).
       Afterwards stores are inserted for computed values. */

    for (ins = iter->ins; ins != NULL; ins = MVM_spesh_iterator_next_ins(tc, iter)) {
        /* NB - we probably will want to involve the spesh info in selecting a
           template. And for optimisation, I'd like to copy spesh facts (if any)
           to the tree info */
        MVMuint16 opcode = ins->info->opcode;
        MVMSpeshAnn *ann;
        const MVMJitExprTemplate *template;
        MVMint32 before_label = -1, after_label = -1, store_directly = 0;

        struct ValueDefinition *defined_value = NULL;

        /* check if this is a getlex and if we can handle it */
        BAIL(opcode == MVM_OP_getlex && getlex_needs_autoviv(tc, jg, ins), "Can't compile object getlex\n");
        BAIL(opcode == MVM_OP_bindlex && bindlex_needs_write_barrier(tc, jg, ins), "Can't compile write-barrier bindlex\n");

        /* Check annotations that may require handling or wrapping the expression */
        for (ann = ins->annotations; ann != NULL; ann = ann->next) {
            MVMint32 idx;
            switch (ann->type) {
            case MVM_SPESH_ANN_FH_START:
                /* start of a frame handler (inclusive). We need to mark this
                 * instruction with a label so that we know the handler covers
                 * this code */
                before_label = MVM_jit_label_before_ins(tc, jg, iter->bb, ins);
                jg->handlers[ann->data.frame_handler_index].start_label = before_label;
                break;
            case MVM_SPESH_ANN_FH_END:
                /* end of the frame handler (exclusive), funnily enough not
                 * necessarily the end of a basic block. */
                before_label = MVM_jit_label_before_ins(tc, jg, iter->bb, ins);
                jg->handlers[ann->data.frame_handler_index].end_label = before_label;
                break;
            case MVM_SPESH_ANN_FH_GOTO:
                /* A label to jump to for when a handler catches an
                 * exception. Thus, this can be a control flow entry point (and
                 * should be the start of a basic block, but I'm not sure if it
                 * always is).  */
                before_label = MVM_jit_label_before_ins(tc, jg, iter->bb, ins);
                jg->handlers[ann->data.frame_handler_index].goto_label = before_label;
                active_values_flush(tc, tree, values, sg->num_locals);
                break;
            case MVM_SPESH_ANN_DEOPT_OSR:
                /* A label the OSR can jump into to 'start running', so to
                 * speak. As it breaks the basic-block assumption, arguably,
                 * this should only ever be at the start of a basic block. But
                 * it's not. So we have to insert the label and compute it. */
                before_label = MVM_jit_label_before_ins(tc, jg, iter->bb, ins);
                /* OSR reuses the deopt label mechanism */
                MVM_VECTOR_ENSURE_SIZE(jg->deopts, idx = jg->deopts_num++);
                jg->deopts[idx].label = before_label;
                jg->deopts[idx].idx   = ann->data.deopt_idx;
                /* possible entrypoint, so flush intermediates */
                active_values_flush(tc, tree, values, sg->num_locals);
                break;
            case MVM_SPESH_ANN_INLINE_START:
                /* start of an inline, used for reconstructing state when deoptimizing */
                before_label = MVM_jit_label_before_ins(tc, jg, iter->bb, ins);
                jg->inlines[ann->data.inline_idx].start_label = before_label;
                break;
            case MVM_SPESH_ANN_INLINE_END:
                /* end of the inline (inclusive), so we need to add a label,
                 * which should be the end of the basic block. */
                after_label = MVM_jit_label_after_ins(tc, jg, iter->bb, ins);
                jg->inlines[ann->data.inline_idx].end_label = after_label;
                break;
            case MVM_SPESH_ANN_DEOPT_INLINE:
                MVM_jit_log(tc, "Not sure if we can handle DEOPT_INLINE on instruction %s\n", ins->info->name);
                break;
            case MVM_SPESH_ANN_DEOPT_ONE_INS:
                /* we should only see this in guards, which we don't do just
                 * yet, although we will. At the very least, this implies a flush. */
                switch (opcode) {
                case MVM_OP_sp_guard:
                case MVM_OP_sp_guardconc:
                case MVM_OP_sp_guardtype:
                case MVM_OP_sp_guardsf:
                    BAIL(1, "Cannot handle DEOPT_ONE (ins=%s)\n", ins->info->name);
                    break;
                }
                break;
            case MVM_SPESH_ANN_DEOPT_ALL_INS:
                /* don't expect to be handling these, either, but these also
                 * might need a label-after-the-fact */
                after_label = MVM_jit_label_after_ins(tc, jg, iter->bb, ins);
                /* ensure a consistent state for deoptimization */
                active_values_flush(tc, tree, values, sg->num_locals);
                /* add deopt idx */
                MVM_VECTOR_ENSURE_SIZE(jg->deopts, idx = jg->deopts_num++);
                jg->deopts[idx].label = after_label;
                jg->deopts[idx].idx   = ann->data.deopt_idx;
                break;
            }
        }


        if (opcode == MVM_SSA_PHI || opcode == MVM_OP_no_op) {
            /* By definition, a PHI node can only occur at the start of a basic
             * block. (A no_op instruction only seems to happen as the very
             * first instruction of a frame, and I'm not sure why).
             *
             * Thus, if it happens that we've processed annotations on those
             * instructions (which probably means they migrated there from
             * somewhere else), they always refer to the start of the basic
             * block, which is already assigned a label and
             * dynamic-control-handler.
             *
             * So we never need to do anything with this label and wrapper, but
             * we do need to process the annotation to setup the frame handler
             * correctly.
             */
            BAIL(after_label >= 0, "A PHI node should not have an after label\n");
            continue;
        }

        template = MVM_jit_get_template_for_opcode(opcode);
        BAIL(template == NULL, "Cannot get template for: %s\n", ins->info->name);

        check_template(tc, template, ins);

        MVM_jit_expr_load_operands(tc, tree, ins, values, operands);
        root = MVM_jit_expr_apply_template(tc, tree, template, operands);

        /* root is highest node by construction, so we don't have to check the size of info later */
        MVM_VECTOR_ENSURE_SIZE(tree->info, root);
        tree->info[root].spesh_ins = ins;


        /* mark operand types */
        for (i = 0; i < ins->info->num_operands; i++) {
            MVMint8 opr_kind = ins->info->operands[i];
            MVMint8 opr_type = opr_kind & MVM_operand_type_mask;
            MVMSpeshOperand opr = ins->operands[i];
            if (opr_type == MVM_operand_type_var) {
                switch (opr_kind & MVM_operand_rw_mask) {
                case MVM_operand_read_reg:
                case MVM_operand_write_reg:
                    opr_type = MVM_spesh_get_reg_type(tc, sg, opr.reg.orig) << 3; /* shift up 3 to match operand type */
                    break;
                case MVM_operand_read_lex:
                case MVM_operand_write_lex:
                    opr_type = MVM_spesh_get_lex_type(tc, sg, opr.lex.outers, opr.lex.idx) << 3;
                    break;
                }
            }
            switch(opr_kind & MVM_operand_rw_mask) {
            case MVM_operand_read_reg:
            case MVM_operand_read_lex:
                tree->info[operands[i]].opr_type = opr_type;
                break;
            case MVM_operand_write_reg:
                /* for write_reg and write_lex, operands[i] is the *address*,
                 * the *value* is the root, but this is only valid if the
                 * operand index is 0 */
                if (template->flags & MVM_JIT_EXPR_TEMPLATE_DESTRUCTIVE) {
                    /* overrides any earlier definition of this local variable */
                    memset(values + opr.reg.orig, -1, sizeof(struct ValueDefinition));
                } else {
                    /* record this value, should be only one for the root */
                    BAIL(i != 0, "Write reg operand %d\n", i);
                    tree->info[root].opr_type  = opr_type;
                    defined_value = values + opr.reg.orig;
                    defined_value->addr = operands[i];
                    defined_value->node = root;
                    /* this overwrites any previous definition */
                    defined_value->root = -1;
                }
                break;
            case MVM_operand_write_lex:
                /* does not define a value we can look up, but we may need to
                 * insert a store */
                if (!(template->flags & MVM_JIT_EXPR_TEMPLATE_DESTRUCTIVE)) {
                    BAIL(i != 0, "Write lex operand %d\n", i);
                    tree->info[root].opr_type = opr_type;
                    /* insert the store to lexicals directly, do not record as value */
                    root = MVM_jit_expr_add_store(tc, tree, operands[i], root, MVM_JIT_REG_SZ);
                }
                break;
            }
        }




        if (ins->info->jittivity & (MVM_JIT_INFO_THROWISH | MVM_JIT_INFO_INVOKISH)) {
            /* NB: we should make this a template-level flag; should be possible
             * to replace an invokish version with a non-invokish version (but
             * perhaps best if that is opt-in so people don't accidentally
             * forget to set it). */
            MVM_jit_log(tc, "EXPR: adding throwish guard to op (%s)\n", ins->info->name);
            active_values_flush(tc, tree, values, sg->num_locals);
            store_directly = 1;
        }


        /* Add root to tree to ensure source evaluation order, wrapped with
         * labels if necessary. */
        if (before_label >= 0 && MVM_jit_label_is_for_ins(tc, jg, before_label)) {
            MVM_VECTOR_PUSH(tree->roots, MVM_jit_expr_add_label(tc, tree, before_label));
        }

        /* NB: GUARD only wraps void nodes. Currently, we replace any
         * value-yielding node with it's STORE (and thereby make sure it is
         * flushed directly) */
        if (store_directly && defined_value != NULL) {
            /* If we're wrapping this template and it defines a value, we
             * had maybe better flush it directly */
            root = MVM_jit_expr_add_store(tc, tree, defined_value->addr, root, MVM_JIT_REG_SZ);
            defined_value = NULL;
        }

        if (defined_value != NULL) {
            defined_value->root = tree->roots_num;
        }

        MVM_VECTOR_PUSH(tree->roots, root);
        if (after_label >= 0 && MVM_jit_label_is_for_ins(tc, jg, after_label)) {
            MVM_VECTOR_PUSH(tree->roots, MVM_jit_expr_add_label(tc, tree, after_label));
        }
    }

 done:
    if (tree->roots_num > 0) {
        active_values_flush(tc, tree, values, sg->num_locals);
        MVM_jit_expr_tree_analyze(tc, tree);
        MVM_jit_log(tc, "Build tree out of: [");
        for (ins = entry; ins != iter->ins; ins = ins->next) {
            if (ins->info->opcode == MVM_SSA_PHI)
                continue;
            MVM_jit_log(tc, "%s, ", ins->info->name);
        }
        MVM_jit_log(tc, "]\n");
    } else {
        /* Don't return empty trees, nobody wants that */
        MVM_jit_expr_tree_destroy(tc, tree);
        tree = NULL;
    }
    MVM_free(values);
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
    MVM_VECTOR_INIT(traverser->visits, tree->nodes_num);
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
