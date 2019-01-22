#include "moar.h"


struct OpInfo {
    const char *name;
    MVMint8   nchild;
    MVMuint8   nargs;
};


const struct OpInfo OP_INFO_TABLE[] = {
#define OP_INFO(name, nchild, nargs) { #name, nchild, nargs }
    MVM_JIT_EXPR_OPS(OP_INFO)
#undef OP_INFO
};


static const struct OpInfo * get_op_info(enum MVMJitExprOperator operator) {
    assert(operator >= 0 && operator < MVM_JIT_MAX_NODES);
    return OP_INFO_TABLE + operator;
}

const char * MVM_jit_expr_operator_name(MVMThreadContext *tc, enum MVMJitExprOperator operator) {
    return get_op_info(operator)->name;
}


/* Mathematical min and max macro's */
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b));
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b));
#endif


/* macros used in the expression list templates, defined here so they
   don't overwrite other definitions */
#define CAT3(a,b,c) a b c
#define QUOTE(x) (x)
#define SIZEOF_MEMBER(type, member) sizeof(((type*)0)->member)

#include "jit/core_templates.h"


/* Record the node that defines a value */
struct ValueDefinition {
    MVMint32 node;
    MVMint32 root;
    MVMint32 addr;
};

static MVMint32 noop_code[] = { MVM_JIT_NOOP, 0 };

static struct MVMJitExprTemplate noop_template = {
    noop_code, "ns", 2, 0, 0
};

/* Logical negation of comparison operators */
enum MVMJitExprOperator MVM_jit_expr_op_invert_comparison(enum MVMJitExprOperator op) {
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


/* Binary operators of the form: a = op(b,c) */
MVMint32 MVM_jit_expr_op_is_binary(enum MVMJitExprOperator op) {
    switch (op) {
    case MVM_JIT_ADD:
    case MVM_JIT_SUB:
    case MVM_JIT_MUL:
    case MVM_JIT_AND:
    case MVM_JIT_OR:
    case MVM_JIT_XOR:
        /* and DIV, SHIFT, etc */
        return 1;
    default:
        /* ADD, MUL, AND, OR, etc. are commutative */
        return 0;
    }
}

/* Commutative binary operators: op(a,b) == op(b,a) */
MVMint32 MVM_jit_expr_op_is_commutative(enum MVMJitExprOperator op) {
    switch (op) {
    case MVM_JIT_ADD:
    case MVM_JIT_MUL:
    case MVM_JIT_AND:
    case MVM_JIT_OR:
        return 1;
    default:
        return 0;
    }
}



static MVMint32 MVM_jit_expr_add_regaddr(MVMThreadContext *tc, MVMJitExprTree *tree,
                                         MVMuint16 reg) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "nsnsl.",
                                             MVM_JIT_LOCAL, 0,
                                             MVM_JIT_ADDR, 1, 0, reg * MVM_JIT_REG_SZ);
}

static MVMint32 MVM_jit_expr_add_loadframe(MVMThreadContext *tc, MVMJitExprTree *tree) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "nsnsl.nsl.",
                                             MVM_JIT_TC, 0,
                                             MVM_JIT_ADDR, 1, 0, offsetof(MVMThreadContext, cur_frame),
                                             MVM_JIT_LOAD, 1, 2, sizeof(MVMFrame*));
}

static MVMint32 MVM_jit_expr_add_load(MVMThreadContext *tc, MVMJitExprTree *tree,
                                      MVMint32 addr) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "ns..", MVM_JIT_LOAD, 1, addr, MVM_JIT_REG_SZ);
}

static MVMint32 MVM_jit_expr_add_store(MVMThreadContext *tc, MVMJitExprTree *tree,
                                       MVMint32 addr, MVMint32 val, MVMint32 sz) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "ns...", MVM_JIT_STORE, 1, addr, val, sz);
}

static MVMint32 MVM_jit_expr_add_cast(MVMThreadContext *tc, MVMJitExprTree *tree,
                                      MVMint32 cast_mode, MVMint32 node, MVMint32 to_size, MVMint32 from_size) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "ns....", cast_mode, 1, node, to_size, from_size);
}

static MVMint32 MVM_jit_expr_add_label(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint32 label) {
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "ns.", MVM_JIT_MARK, 0, label);
}

static MVMint32 MVM_jit_expr_add_lexaddr(MVMThreadContext *tc, MVMJitExprTree *tree,
                                         MVMuint16 outers, MVMuint16 idx) {
    MVMint32 i;
    /* (frame) as the root */
    MVMint32 root = MVM_jit_expr_add_loadframe(tc, tree);
    for (i = 0; i < outers; i++) {
        /* (load (addr $val (&offsetof MVMFrame outer)) (&sizeof MVMFrame*)) */
        root = MVM_jit_expr_apply_template_adhoc(tc, tree, "ns..nsl.",
                                                 MVM_JIT_ADDR, 1, root, offsetof(MVMFrame, outer),
                                                 MVM_JIT_LOAD, 1, 0, sizeof(MVMFrame*));

    }
    /* (addr (load (addr $frame (&offsetof MVMFrame env)) ptr_sz) ptr_sz*idx) */
    return MVM_jit_expr_apply_template_adhoc(tc, tree, "ns..nsl.nsl.",
                                             /* (addr $frame (&offsetof MVMFrame env)) */
                                             MVM_JIT_ADDR, 1, root, offsetof(MVMFrame, env),
                                             /* (load $addr ptr_sz) */
                                             MVM_JIT_LOAD, 1, 0, MVM_JIT_PTR_SZ,
                                             /* (addr $frame_env idx*reg_sz) */
                                             MVM_JIT_ADDR, 1, 4, idx * MVM_JIT_REG_SZ);
}

/* Manage large constants - by the way, no attempt is being made to unify them */
static MVMint32 MVM_jit_expr_add_const_i64(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint64 const_i64) {
    MVM_VECTOR_ENSURE_SPACE(tree->constants, 1);
    {
        MVMint32 t = tree->constants_num++;
        tree->constants[t].i = const_i64;
        return t;
    }
}

static MVMint32 MVM_jit_expr_add_const_n64(MVMThreadContext *tc, MVMJitExprTree *tree, MVMnum64 const_n64) {
    MVM_VECTOR_ENSURE_SPACE(tree->constants, 1);
    {
        MVMint32 t = tree->constants_num++;
        tree->constants[t].n = const_n64;
        return t;
    }
}

static MVMint32 MVM_jit_expr_add_const_ptr(MVMThreadContext *tc, MVMJitExprTree *tree, const void *const_ptr) {
    MVM_VECTOR_ENSURE_SPACE(tree->constants, 1);
    {
        MVMint32 t = tree->constants_num++;
        tree->constants[t].p = const_ptr;
        return t;
    }

}

static MVMint32 MVM_jit_expr_add_const(MVMThreadContext *tc, MVMJitExprTree *tree,
                                       MVMSpeshOperand opr, MVMuint8 type) {
    MVMint32 operator = MVM_JIT_CONST, constant = 0, size = 0;
    char *info = "ns..";
    switch(type & MVM_operand_type_mask) {
    case MVM_operand_int8:
        constant = opr.lit_i8;
        size = sizeof(MVMint8);
        break;
    case MVM_operand_int16:
        constant = opr.lit_i16;
        size = sizeof(MVMint16);
        break;
    case MVM_operand_coderef:
        constant = opr.coderef_idx;
        size = sizeof(MVMuint16);
        break;
    case MVM_operand_int32:
        constant = opr.lit_i32;
        size = sizeof(MVMint32);
        break;
    case MVM_operand_int64:
        operator = MVM_JIT_CONST_LARGE;
        constant = MVM_jit_expr_add_const_i64(tc, tree, opr.lit_i64);
        size = MVM_JIT_INT_SZ;
        break;
    case MVM_operand_num32:
        /* possible endianess issue here */
        constant = opr.lit_i32;
        size = sizeof(MVMnum32);
        break;
    case MVM_operand_num64:
        operator = MVM_JIT_CONST_LARGE;
        constant = MVM_jit_expr_add_const_n64(tc, tree, opr.lit_n64);
        size = MVM_JIT_NUM_SZ;
        break;
    case MVM_operand_str:
        /* string index really */
        constant = opr.lit_str_idx;
        size = sizeof(MVMuint32);
        break;
    case MVM_operand_ins:
        operator = MVM_JIT_LABEL;
        constant = MVM_jit_label_before_bb(tc, tree->graph, opr.ins_bb);
        info     = "ns.";
        break;
    case MVM_operand_callsite:
        constant = opr.callsite_idx;
        size = sizeof(MVMuint16);
        break;
    case MVM_operand_spesh_slot:
        constant = opr.lit_i16;
        size = sizeof(MVMuint16);
        break;
    default:
        MVM_oops(tc, "Can't add constant for operand type %d\n", (type & MVM_operand_type_mask) >> 3);
    }
    return MVM_jit_expr_apply_template_adhoc(tc, tree, info, operator, 0, constant, size);
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


/* Add template to nodes, filling in operands and linking tree nodes. Return template root */
static MVMint32 apply_template(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint32 len, char *info,
                               MVMint32 *code, MVMint32 *operands) {
    MVMint32 i, j, root = 0, base = tree->nodes_num;
    MVM_VECTOR_ENSURE_SPACE(tree->nodes, len);
    /* Loop over string until the end */
    for (i = 0, j = base; info[i]; i++, j++) {
        switch (info[i]) {
        case 'l':
            /* template contained a node */
            assert(info[code[i]] ==  'n');
            /* link template-relative to nodes-relative */
            tree->nodes[j] = code[i] + base;
            break;
        case 'i':
            /* insert input operand node */
            tree->nodes[j] = operands[code[i]];
            break;
        case 'c':
            tree->nodes[j] = MVM_jit_expr_add_const_ptr(tc, tree, MVM_jit_expr_template_constants[code[i]]);
            break;
        case 'n':
            /* next node should contain size */
            tree->nodes[j] = code[i];
            assert(i + 1 < len && info[i+1] == 's');
            root = j;
            break;
        case 's':
            /* Install operator info and read size argument for variadic nodes */
            assert(i > 0 && info[i-1] == 'n');
        {
            const struct OpInfo *op_info = get_op_info(code[i-1]);
            MVMJitExprInfo *expr_info = MVM_JIT_EXPR_INFO(tree, j-1);
            expr_info->num_links = op_info->nchild < 0 ? code[i] : op_info->nchild;
            expr_info->num_args  = op_info->nargs;
            break;
        }
        case '.':
        default:
            /* copy constant from template */
            tree->nodes[j] = code[i];
            break;
        }
    }
    assert(i == len);
    tree->nodes_num = base + len;
    return root;
}

MVMint32 MVM_jit_expr_apply_template(MVMThreadContext *tc, MVMJitExprTree *tree,
                                     const MVMJitExprTemplate *template, MVMint32 *operands) {
    return apply_template(tc, tree, template->len, (char*)template->info, (MVMint32*)template->code, operands);
}

/* this will fail with more than 16 nodes, which is just as fine */
MVMint32 MVM_jit_expr_apply_template_adhoc(MVMThreadContext *tc, MVMJitExprTree *tree,
                                           char *info, ...) {
    MVMint32 code[16];
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

    const struct OpInfo *op_info = get_op_info(tree->nodes[node]);

    MVMint32   first_child = MVM_JIT_EXPR_FIRST_CHILD(tree, node);
    MVMint32        nchild = MVM_JIT_EXPR_NCHILD(tree, node);
    MVMint32        *links = MVM_JIT_EXPR_LINKS(tree, node);
    MVMint32         *args = MVM_JIT_EXPR_ARGS(tree, node);
    MVMint32     cast_mode = MVM_JIT_NOOP;
    MVMint32 node_size = 0;
    MVMint32 i;


    /* propagate node sizes */
    switch (tree->nodes[node]) {
    case MVM_JIT_CONST:
    case MVM_JIT_CONST_LARGE:
        /* node size is given */
        node_size        = args[1];
        break;
    case MVM_JIT_CONST_PTR:
        node_size       = MVM_JIT_PTR_SZ;
        break;
    case MVM_JIT_COPY:
        node_size = MVM_JIT_EXPR_INFO(tree, links[0])->size;
        break;
    case MVM_JIT_LOAD:
        node_size = args[0];
        break;
    case MVM_JIT_SCAST:
    case MVM_JIT_UCAST:
        node_size = args[0];
        break;
    case MVM_JIT_LABEL:
    case MVM_JIT_TC:
    case MVM_JIT_CU:
    case MVM_JIT_LOCAL:
    case MVM_JIT_STACK:
        /* addresses result in pointers */
        node_size = MVM_JIT_PTR_SZ;
        break;
    case MVM_JIT_ADDR:
    case MVM_JIT_IDX:
        node_size = MVM_JIT_PTR_SZ;
        cast_mode = MVM_JIT_UCAST;
        break;
        /* signed binary operations */
    case MVM_JIT_ADD:
    case MVM_JIT_SUB:
    case MVM_JIT_MUL:
    case MVM_JIT_LT:
    case MVM_JIT_LE:
    case MVM_JIT_GE:
    case MVM_JIT_GT:
    case MVM_JIT_EQ:
    case MVM_JIT_NE:
        {
            /* arithmetic nodes use their largest operand */
            node_size = MAX(MVM_JIT_EXPR_INFO(tree, links[0])->size,
                            MVM_JIT_EXPR_INFO(tree, links[1])->size);
            cast_mode = MVM_JIT_SCAST;
            break;
        }
       /* unsigned binary operations */
    case MVM_JIT_AND:
    case MVM_JIT_OR:
    case MVM_JIT_XOR:
    case MVM_JIT_NOT:
        {
            node_size = MAX(MVM_JIT_EXPR_INFO(tree, links[0])->size,
                            MVM_JIT_EXPR_INFO(tree, links[1])->size);
            cast_mode = MVM_JIT_UCAST;
            break;
        }
   case MVM_JIT_FLAGVAL:
        /* XXX THIS IS A HACK
         *
         * The true size of 'flagval' is a single byte.  But that would mean it
         * had to be upcast to be used as a 64-bit word, and that subtly
         * doesn't work if the value is STORE-d to memory. */
        node_size = 4;
        break;
    case MVM_JIT_DO:
        /* node size of last child */
        {
            node_size = MVM_JIT_EXPR_INFO(tree, links[nchild-1])->size;
            break;
        }
    case MVM_JIT_IF:
        {
            node_size = MAX(MVM_JIT_EXPR_INFO(tree, links[1])->size,
                            MVM_JIT_EXPR_INFO(tree, links[2])->size);
            break;
        }
    case MVM_JIT_CALL:
        node_size = args[0];
        break;
    case MVM_JIT_NZ:
    case MVM_JIT_ZR:
        node_size = MVM_JIT_EXPR_INFO(tree, links[0])->size;
        break;
    default:
        /* all other things, branches, labels, when, arglist, carg,
         * comparisons, etc, have no value size */
        node_size = 0;
        break;
    }
    MVM_JIT_EXPR_INFO(tree, node)->size = node_size;

    /* Insert casts as necessary */
    if (cast_mode != MVM_JIT_NOOP) {
        for (i = 0; i < nchild; i++) {
            MVMint32 child = tree->nodes[first_child+i];
            MVMuint8 child_size = MVM_JIT_EXPR_INFO(tree, child)->size;
            if (tree->nodes[child] == MVM_JIT_CONST) {
                /* CONST nodes can always take over their target size, so they never need to be cast */
                MVM_JIT_EXPR_INFO(tree, child)->size = node_size;
            } else if (child_size < node_size) {
                /* Widening casts need to be handled explicitly, shrinking casts do not */
                MVMint32 cast = MVM_jit_expr_add_cast(tc, tree, cast_mode, child, node_size, child_size);
#if MVM_JIT_DEBUG
                fprintf(stderr, "Inserting %s cast from %d to %d for operator %s (child %s)\n",
                        (cast_mode == MVM_JIT_CAST_UNSIGNED ? "unsigned" : "signed"),
                        child_size, node_size,
                        MVM_jit_expr_operator_name(tc, tree->nodes[node]),
                        MVM_jit_expr_operator_name(tc, tree->nodes[child])
                    );
#endif
                /* Because the cast may have grown the backing nodes array, the info array needs to grow as well */
                MVM_JIT_EXPR_INFO(tree, cast)->size = node_size;
                /* Finally we replace the child with its cast */
                tree->nodes[first_child+i] = cast;
            }
        }
    }
}



void MVM_jit_expr_tree_analyze(MVMThreadContext *tc, MVMJitExprTree *tree) {
    /* analyse the tree, calculate usage and destination information */
    MVMJitTreeTraverser traverser;
    traverser.policy    = MVM_JIT_TRAVERSER_ONCE;
    traverser.data      = NULL;
    traverser.preorder  = NULL;
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

static MVMint32 tree_is_empty(MVMThreadContext *tc, MVMJitExprTree *tree) {
    return tree->nodes_num == 0;
}

MVMJitExprTree * MVM_jit_expr_tree_build(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshIterator *iter) {
    MVMSpeshGraph *sg = jg->sg;
    MVMSpeshIns *entry = iter->ins;
    MVMSpeshIns *ins;
    MVMJitExprTree *tree;
    MVMint32 operands[MVM_MAX_OPERANDS];
    struct ValueDefinition *values;
    MVMuint16 i;
    /* No instructions, just skip */
    if (!iter->ins)
        return NULL;

    /* Make the tree */
    tree = MVM_malloc(sizeof(MVMJitExprTree));
    MVM_VECTOR_INIT(tree->nodes, 256);
    MVM_VECTOR_INIT(tree->constants, 16);
    MVM_VECTOR_INIT(tree->roots, 16);



    tree->graph      = jg;
    /* Hold indices to the node that last computed a value belonging
     * to a register. Initialized as -1 to indicate that these
     * values are empty. */
    values = MVM_malloc(sizeof(struct ValueDefinition)*sg->num_locals);
    memset(values, -1, sizeof(struct ValueDefinition)*sg->num_locals);

#define BAIL(x, ...) do { if (x) { MVM_spesh_graph_add_comment(tc, iter->graph, iter->ins, "expr bail: " __VA_ARGS__); goto done; } } while (0)


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
        MVMint32 before_label = -1, after_label = -1, store_directly = 0, root = 0;

        struct ValueDefinition *defined_value = NULL;

        /* check if this is a getlex and if we can handle it */
        BAIL(opcode == MVM_OP_getlex && getlex_needs_autoviv(tc, jg, ins), "getlex with autoviv");
        BAIL(opcode == MVM_OP_bindlex && bindlex_needs_write_barrier(tc, jg, ins), "Can't compile write-barrier bindlex");

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
                break;
            case MVM_SPESH_ANN_DEOPT_ONE_INS:
                /* we should only see this in guards, which we don't do just
                 * yet, although we will. At the very least, this implies a flush. */
                switch (opcode) {
                case MVM_OP_sp_guard:
                case MVM_OP_sp_guardconc:
                case MVM_OP_sp_guardtype:
                case MVM_OP_sp_guardsf:
                    BAIL(1, "Cannot handle DEOPT_ONE (ins=%s)", ins->info->name);
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
            /* No template here, but we may have to emit labels */
            if (after_label < 0 && (before_label < 0 || tree_is_empty(tc, tree)))
                continue;
            goto emit;
        }

        template = MVM_jit_get_template_for_opcode(opcode);
        BAIL(template == NULL, "Cannot get template for: %s", ins->info->name);
        if (tree_is_empty(tc, tree)) {
            /* start with a no-op so every valid reference is nonzero */
            MVM_jit_expr_apply_template(tc, tree, &noop_template, NULL);
            MVM_spesh_graph_add_comment(tc, jg->sg, iter->ins, "start of exprjit tree");
        }

        MVM_jit_expr_load_operands(tc, tree, ins, values, operands);
        root = MVM_jit_expr_apply_template(tc, tree, template, operands);

        /* mark operand types */
        for (i = 0; i < ins->info->num_operands; i++) {
            MVMuint8 opr_kind = ins->info->operands[i];
            MVMuint8 opr_type = opr_kind & MVM_operand_type_mask;
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
                MVM_JIT_EXPR_INFO(tree, operands[i])->type = opr_type >> 3;
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
                    BAIL(i != 0, "Write reg operand %d", i);
                    MVM_JIT_EXPR_INFO(tree, root)->type = opr_type >> 3;
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
                    BAIL(i != 0, "Write lex operand %d", i);
                    MVM_JIT_EXPR_INFO(tree, root)->type = opr_type >> 3;
                    /* insert the store to lexicals directly, do not record as value */
                    root = MVM_jit_expr_add_store(tc, tree, operands[i], root, MVM_JIT_REG_SZ);
                }
                break;
            }
            assert(MVM_JIT_EXPR_INFO(tree, operands[i])->type >= 0);
        }

        if (ins->info->jittivity & (MVM_JIT_INFO_THROWISH | MVM_JIT_INFO_INVOKISH)) {
            /* NB: we should make this a template-level flag; should be possible
             * to replace an invokish version with a non-invokish version (but
             * perhaps best if that is opt-in so people don't accidentally
             * forget to set it). */
            active_values_flush(tc, tree, values, sg->num_locals);
            store_directly = 1;
        }

        /* Add root to tree to ensure source evaluation order, wrapped with
         * labels if necessary. */
    emit:
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

        if (root != 0)
            MVM_VECTOR_PUSH(tree->roots, root);

        if (after_label >= 0 && MVM_jit_label_is_for_ins(tc, jg, after_label)) {
            MVM_VECTOR_PUSH(tree->roots, MVM_jit_expr_add_label(tc, tree, after_label));
        }
    }

 done:
    if (tree->roots_num > 0) {
        active_values_flush(tc, tree, values, sg->num_locals);
        MVM_jit_expr_tree_analyze(tc, tree);
    } else {
        /* Don't return empty trees, nobody wants that */
        MVM_jit_expr_tree_destroy(tc, tree);
        tree = NULL;
    }
    MVM_free(values);
    return tree;
}

void MVM_jit_expr_tree_destroy(MVMThreadContext *tc, MVMJitExprTree *tree) {
    MVM_free(tree->nodes);
    MVM_free(tree->roots);
    MVM_free(tree->constants);
    MVM_free(tree);
}

static void walk_tree(MVMThreadContext *tc, MVMJitExprTree *tree,
                 MVMJitTreeTraverser *traverser, MVMint32 node) {
    MVMint32 first_child = MVM_JIT_EXPR_FIRST_CHILD(tree, node);
    MVMint32 nchild      = MVM_JIT_EXPR_NCHILD(tree, node);
    MVMint32 i;
    assert(node < tree->nodes_num);
    if (traverser->policy == MVM_JIT_TRAVERSER_ONCE &&
        traverser->visits[node] >= 1)
        return;

    traverser->visits[node]++;
    /* visiting on the way down - NB want to add visitation information */
    if (traverser->preorder)
        traverser->preorder(tc, traverser, tree, node);
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


/* Walk tree to get nodes along a path */
MVMint32 MVM_jit_expr_tree_get_nodes(MVMThreadContext *tc, MVMJitExprTree *tree,
                                     MVMint32 node, const char *path,
                                     MVMint32 *buffer) {
    MVMint32 *ptr = buffer;
    while (*path) {
        MVMint32 cur_node = node;
        do {
            MVMint32 first_child = MVM_JIT_EXPR_FIRST_CHILD(tree, cur_node);
            MVMint32 child_nr    = *path++ - '1'; /* child offset is 1 in expr-template-compiler.pl */
            cur_node = tree->nodes[first_child+child_nr];
        } while (*path != '.');
        /* regs nodes go to values, others to args */
        *ptr++ = cur_node;
        path++;
    }
    return ptr - buffer;
}
