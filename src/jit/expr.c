#include "moar.h"

static MVMJitExprTemplate* get_template(MVMint16 opcode) {
    /* TODO: read the template from a table. I may need to build
     * this table from a description, because templates may be
     * arbitrarily long. This is suspiciously close to the modus
     * operandi of DynASM */
    return NULL;
}

static void fill_template(MVMJitExprTemplate *tmpl, MVMint32 *buffer, MVMint32 idx,
                          MVMint32 *operands) {
    /* Fill a template using the operands provided. Provide internal
       linking, insertion of operand indices. Because all operands
       are 32 bit indices, this should be nice and simple. */
}

MVMJitExpr * build_expr_tree(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMSpeshBB *bb) {
    MVMJitExpr * expr = null;
    MVMSpeshIns *ins = bb->first_ins;
    MVMint32 computed_nodes[sg->num_locals]; /* array to renember where we computed a local */
    MVMint32 tree_buffer_alloc = 32;
    MVMint32 *tree_buffer = MVM_malloc(sizeof(MVMint32)*tree_buffer_alloc);
    MVMint32 idx = 0;

    /* Initialize computed as -1, indicating they haven't been loaded yet.
     * Note that once we do implement register allocation, we will want to
     * initialize this with values for allocated registers. */
    memset(computed_nodes, -1, sizeof(MVMint32)*sg->num_locals);
    /* This is very similar to a code generation algorithm for a RISC
       machine with unlimited registers. (Or at least more registers
       than used in the expression). The basic idea is to keep a
       index to the node that last computed the value of a local.
       Each opcode is translated to the expression using a template,
       which is a): filled with nodes coming from operands and b):
       internally linked together (relative to absolute indexes).
       NB - templates may insert stores internally as needed.
 */
    while (ins != NULL) {
        MVMint32 operand_nodees[ins->info->num_operands];
        /* NB - we probably will want to involve the spesh info in
           selecting a template. And/or add in c function calls to
           them mix.. */
        MVMJItExprTemplate *templ = get_template(ins->info->opcode);
        if (templ == NULL) {
            /* we don't have a template for this yet, so we can't
             * convert it to an expression */
            break;
        }
        for (int i = 0; i < ins->info->num_operands; i++) {
            if (ins->info->operands[i] & MVM_operand_read_reg) {
                if (computed[i] > 0) {
                    operands_nodes[i-1] = computed_nodes[i];
                } else {
                    /* add load node to computed_nodes */
                }
            } else if (ins->info->operands[i] & MVM_operand_literal) {
                /* add immediate node, insert into computed_nodes and operand_nodes */
            }
        }
        if (templ->size + idx >= tree_buffer_alloc) {
            MVMint32 new_size = tree_buffer_alloc * 2;
            tree_buffer = MVM_realloc(sizeof(MVMint32)*new_size);
            tree_buffer_alloc = new_size;
        }
        fill_template(templ, tree_buffer, idx);
        /* assign computed value to computed nodes, at least if we've
           written something */
        if (ins->info->operands[0] & MVM_operand_write_reg) {
            computed_nodes[ins->operands[0].reg.orig] = idx;
        }
        idx += templ->size;
    }
    for (int i = 0; i < sg->num_locals; i++) {
        if (computed_nodes[i] > 0) {
            /* add a store, add to roots */
        }
    }
    if (ins == NULL) {
        expr = MVM_spesh_alloc(tc, sg, sizeof(MVMJitExpr) + sizeof(MVMint32) * idx);
        expr->tree = (MVMint32*)(((char*)expr)+sizeof(MVMJitExpr));
        memcpy(expr->tree, tree_buffer, sizeof(MVMint32)*idx);
        expr->roots = NULL; /* something similar, i presume */
    }
    MVM_free(tree_buffer);
    return expr;
}
