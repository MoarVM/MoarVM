#include "moar.h"

/** NOTE:
  * these property are from luajit(Including the API)  see: http://wiki.luajit.org/Optimizations
  * */

/* alias case . */
typedef enum {
  ALIAS_NO,	
  ALIAS_MAY,
  ALIAS_MUST
} AliasResult;

static AliasResult aa_escape(MVMSpeshGraph *g, MVMSpeshIns *ins, MVMSpeshIns *stop) {
    MVMint64 operand = ins->operands[0].lit_i64; /* The operand that might be stored. */
    while(ins = ins->next && ins != stop) {
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
    MVMSpeshIns *insa = MVM_spesh_get_facts(tc, g, oa)->writer;
    MVMSpeshIns *insb = MVM_spesh_get_facts(tc, g, ob)->writer;

    int allocates_a = insa->info->allocates;
    int allocates_b = insb->info->allocates;
    if(allocates_a && allocates_b) {
        return ALIAS_NO;  /* Two different allocations never alias. */
    }

    if(allocates_b) {              /* At least one allocation? */
        MVMSpeshIns *tmp = insa;
        insa = insb;
        insb = tmp;
    } else if(!allocates_a) {
        return ALIAS_MAY;  /* Anything else, we just don't know. */
    }

    return aa_escape(g, insa, insb);
}

/* Alias analysis for array and hash access using key-based disambiguation. */
static AliasResult aa_ahref(MVMSpeshGraph *g, MVMSpeshIns *insa, MVMSpeshIns *insb) {
    MVMSpeshOperand oa = insa->operands[2];
    MVMSpeshOperand ob = insb->operands[2];
    MVMSpeshIns *keya, *keyb;
    
    if (insa == insb)
        return ALIAS_MUST;  /* Shortcut for same refs. */

    keya = MVM_spesh_get_facts(tc, g, oa)->writer;
    keyb = MVM_spesh_get_facts(tc, g, ob)->writer;
}
