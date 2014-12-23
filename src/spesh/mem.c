#include "moar.h"

/* alias case . */
typedef enum {
  ALIAS_NO,	
  ALIAS_MAY,
  ALIAS_MUST
} AliasResult;

static AliasResult aa_escape(MVMSpeshGraph *g, MVMSpeshIns *ins, MVMSpeshIns *stop) {
    MVMint64 operand = ins->operands[0].lit_i64; /* The operand that might be stored. */
    while(ins = ins->next) {
        if(ins->info->esc) {
            MVMuint8 num_operands = ins->info->num_operands;
            MVMSpeshOperand *operands = ins->operands;
            MVMuint8 i;
            for(i =0; i < num_operands; i++) {
                if(operands[i].lit_i64 == operand)
                    return ALIAS_MAY; /* operand was stored and might alias. */
            }
        }
    }
    return ALIAS_NO;    /* operand was not stored. */
}

/* Alias analysis for two different object operand. */
static AliasResult aa_object(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand oa, MVMSpeshOperand ob) {
    MVMSpeshIns *ins_a = MVM_spesh_get_facts(tc, g, oa)->writer;
    MVMSpeshIns *ins_b = MVM_spesh_get_facts(tc, g, ob)->writer;

    int allocates_a = ins_a->info->allocates;
    int allocates_b = ins_b->info->allocates;
    if(allocates_a && allocates_b) {
            return ALIAS_NO;  /* Two different allocations never alias. */
    }

    if(allocates_b) {              /* At least one allocation? */
        MVMSpeshIns *tmp = ins_a;
        ins_a = ins_b;
        ins_b = tmp;
    } else if(!allocates_a) {
        return ALIAS_MAY;  /* Anything else, we just don't know. */
    }

    return aa_escape(g, ins_a, ins_b);
}