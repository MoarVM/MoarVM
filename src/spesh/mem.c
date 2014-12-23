#include "moar.h"

/* alias case . */
typedef enum {
  ALIAS_NO,	
  ALIAS_MAY,
  ALIAS_MUST
} AliasResult;

static AliasResult escape(MVMSpeshGraph *g, MVMSpeshIns *ins, MVMSpeshIns *stop) {
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