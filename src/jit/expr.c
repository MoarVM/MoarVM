#include "moar.h"
#include "expr.h"
#include "expr_tables.h"

typedef struct {
    MVMJitGraph    *graph;
    MVMJitExprNode *buffer;
    MVMint32       *computed;
    MVMint32       *roots;
    MVMint32        top;
    MVMint32        alloc;
    MVMint32        roots_top;
    MVMint32        roots_alloc;
}  MVMJitTreeBuilder;

static inline void builder_make_space(MVMJitTreeBuilder *builder, MVMint32 space) {
    if (builder->top + space >= builder->alloc) {
        builder->alloc *= 2;
        builder->buffer = MVM_realloc(builder, builder->alloc*sizeof(MVMJitExprNode));
    }
}

static inline void builder_append_direct(MVMJitTreeBuilder *builder, MVMJitExprNode *template,
                                         MVMint32 len) {
    builder_make_space(builder, len);
    memcpy(&builder->buffer[builder->top], template, len*sizeof(MVMJitExprNode));
    builder->top += len;
}

static inline void builder_add_root(MVMJitTreeBuilder *builder, MVMint32 root) {
    /* NYI - this should ensure that the roots of our tree are kept in order */
}

static MVMint32 MVM_jit_expr_add_loadreg(MVMThreadContext *tc, MVMJitTreeBuilder *builder, MVMuint16 reg) {
    MVMJitExprNode template[] = { MVM_JIT_ADDR, MVM_JIT_LOCAL, reg, MVM_JIT_LOAD, builder->top, sizeof(MVMJitExprNode) };
    MVMint32 top        = builder->top;
    builder_append_direct(builder, template, sizeof(template)/sizeof(MVMJitExprNode));
    return top + 3;
}

static MVMint32 MVM_jit_expr_add_const(MVMThreadContext *tc, MVMJitTreeBuilder *builder, MVMSpeshOperand opr, MVMuint8 info) {
    /* TODO implement this properly; this only works correctly for 64 bit values */
    MVMJitExprNode template[]  = { MVM_JIT_CONST, opr.lit_i64, sizeof(MVMint64) };
    MVMint32 top               = builder->top;
    builder_append_direct(builder, template, sizeof(template)/sizeof(MVMJitExprNode));
    return top;
}


void MVM_jit_expr_load_operands(MVMThreadContext *tc, MVMJitTreeBuilder *builder,
                                MVMSpeshIns *ins, MVMint32 *operands) {
    int i;
    for (i = 0; i < ins->info->num_operands; i++) {
        MVMSpeshOperand opr = ins->operands[i];
        if (ins->info->operands[i] & MVM_operand_read_reg) {
            if (builder->computed[opr.reg.orig] > 0) {
                operands[i] = builder->computed[opr.reg.orig];
            } else {
                operands[i] = MVM_jit_expr_add_loadreg(tc, builder, opr.reg.orig);
                builder->computed[opr.reg.orig] = i;
            }
        } else if (ins->info->operands[i] & MVM_operand_literal) {
            operands[i] = MVM_jit_expr_add_const(tc, builder, opr, ins->info->operands[i]);
        } else {
            // hmm... should probably load labels, or do some other clever thing
        }
    }
}

/* Add template to buffer, filling in operands and linking tree nodes. Return template root */
MVMint32 MVM_jit_expr_apply_template(MVMThreadContext *tc, MVMJitTreeBuilder *builder,
                                     const MVMJitExprTemplate *template, MVMint32 *operands) {
    int i, top;
    top = builder->top;
    builder_make_space(builder, template->len);
    /* Loop over string until the end */
    for (i = 0; template->info[i]; i++) {
        switch (template->info[i]) {
        case 'l':
            /* link template-relative to buffer-relative */
            builder->buffer[top+i] = template->code[i] + top;
            break;
        case 'f':
            /* add operand node into the buffer */
            builder->buffer[top+i] = operands[template->code[i]];
            break;
        default:
            /* copy from template to buffer */
            builder->buffer[top+i] = template->code[i];
            break;
        }
    }
    builder->top = top + template->len;
    return top + template->root; /* root relative to buffer */
}

/* TODO add labels to the expression tree */
MVMJitExprTree * MVM_jit_build_expression_tree(MVMThreadContext *tc, MVMJitGraph *jg,
                                               MVMSpeshBB *bb) {
    MVMint32 operands[MVM_MAX_OPERANDS];
    MVMint32 last_root;
    MVMJitTreeBuilder builder;
    MVMJitExprTree *tree = NULL;
    MVMSpeshGraph *sg = jg->sg;
    MVMSpeshIns *ins = bb->first_ins;
    builder.computed = MVM_malloc(sizeof(MVMint32)*sg->num_locals);
    builder.top      = 0;
    builder.alloc    = 32;
    builder.buffer   = MVM_malloc(sizeof(MVMJitExprNode)*builder.alloc);
    /* Initialize computed as -1, indicating they haven't been loaded yet.
     * Note that once we do implement register allocation, we will want to
     * initialize this with values for allocated registers. */
    memset(builder.computed, -1, sizeof(MVMint32)*sg->num_locals);
    /* This is very similar to a code generation algorithm for a RISC
       machine with unlimited registers. (Or at least more registers
       than used in the expression). The basic idea is to keep a
       index to the node that last computed the value of a local.
       Each opcode is translated to the expression using a template,
       which is a): filled with nodes coming from operands and b):
       internally linked together (relative to absolute indexes).
       NB - templates may insert stores internally as needed. */
    while (ins != NULL) {
        /* NB - we probably will want to involve the spesh info in
           selecting a template. And/or add in c function calls to
           them mix.. */
        const MVMJitExprTemplate *templ = MVM_jit_get_template_for_opcode(ins->info->opcode);
        if (templ == NULL) {
            /* we don't have a template for this yet, so we can't
             * convert it to an expression */
            break;
        }
        MVM_jit_expr_load_operands(tc, &builder, ins, operands);
        last_root = MVM_jit_expr_apply_template(tc, &builder, templ, operands);
        /* assign computed value to computed nodes, at least if we've
           written something */
        if ((ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
            builder.computed[ins->operands[0].reg.orig] = last_root;
        } else {
            /* still need to add it to roots */
            builder_add_root(&builder, last_root);
        }
    }
    /* Reached the end correctly? Build a tree */
    if (ins == NULL) {
        /* Hmm */
    }
    MVM_free(builder.buffer);
    MVM_free(builder.computed);
    return tree;
}
