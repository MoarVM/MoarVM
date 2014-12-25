#include "moar.h"

/**
 * NOTE: these basic algorithms are from luajit(Including the API)
 * see: http://wiki.luajit.org/Optimizations for more infomation.
 **/

/**
 * Memory access optimizations.
 * AA: Alias Analysis using high-level semantic disambiguation.
 * FWD: Load Forwarding (L2L) + Store Forwarding (S2L).
 * DSE: Dead-Store Elimination.
 **/



/* alias case . */
typedef enum {
  ALIAS_NO,
  ALIAS_MAY,
  ALIAS_MUST
} AliasResult;

#define Operand_is_eq(a, b) (a.reg.orig == b.reg.orig && a.reg.i == b.reg.i)
#define Operand_is_not_eq(a, b) (a.reg.orig != b.reg.orig || a.reg.i != b.reg.i)

/* Simplified escape analysis: check for intervening stores. */
static AliasResult aa_escape(MVMSpeshGraph *g, MVMSpeshIns *ins, MVMSpeshIns *stop) {
    MVMSpeshOperand operand = ins->operands[0]; /* The operand that might be stored. */
    while ((ins = ins->next) && ins != stop) {
        const MVMuint8 *esc = ins->info->esc;
        if (esc) {
            MVMuint8 num_operands = ins->info->num_operands;
            MVMSpeshOperand *operands = ins->operands;
            MVMuint8 i;
            for (i =0; i < num_operands; i++) {
                if ((esc[i] == MVM_ESCAPE_YES || (esc[i] & MVM_ESCAPE_KIND) == MVM_ESCAPE_INTO)
                    && Operand_is_eq(operands[i], operand))
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

    assert(Operand_is_eq(oa, ob));
    assert((MVM_spesh_get_facts(tc, g, oa)->flags & MVM_SPESH_FACT_CONCRETE)
        && (MVM_spesh_get_facts(tc, g, ob)->flags & MVM_SPESH_FACT_CONCRETE));

    if (allocates_a && allocates_b) {
        return ALIAS_NO;  /* Two different allocations never alias. */
    }

    if (allocates_b) {              /* At least one allocation? */
        MVMSpeshIns *tmp = insa;
        insa = insb;
        insb = tmp;
    } else if(!allocates_a) {
        return ALIAS_MAY;  /* Anything else, we just don't know. */
    }

    return aa_escape(g, insa, insb);
}

/* Alias analysis for array and hash and object get
 * using index-based/key-based/attribute-name-based disambiguation. */
static AliasResult aa_aho_get(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *insa, MVMSpeshIns *insb) {
    MVMSpeshOperand obja = insa->operands[1], objb = insb->operands[1];
    MVMuint16     opcode = insa->info->opcode;

    if (insa == insb)
        return ALIAS_MUST;  /* Shortcut for same refs. */

    if (opcode != insb->info->opcode)
        return ALIAS_NO;      /* Different opcode types of array and hash access. */

    if (opcode = MVM_OP_atpos_o) { /* The object is from a array value */
        MVMSpeshOperand keya = insa->operands[2], keyb = insb->operands[2];
        MVMSpeshIns *keya_writer, *keyb_writer;
        MVMint32 offseta = 0, offsetb = 0;
        MVMSpeshOperand basea = keya, baseb = keyb;
        assert(insb->info->opcode == MVM_OP_atpos_o);

        if (Operand_is_eq(keya, keyb)) {
            if (Operand_is_eq(obja, objb))
                return ALIAS_MUST;  /* Same key, same object. */
            else
                return aa_object(tc, g, obja, objb);  /* Same key, possibly different object. */
        }

        /* Disambiguate array references based on index arithmetic. */
        keya_writer = MVM_spesh_get_facts(tc, g, keya)->writer;
        opcode = keya_writer->info->opcode;

        /* Gather base and offset from array[base] or array[base +/-/x/÷/% offset]. */
        if ((opcode >= MVM_OP_add_i && opcode <= MVM_OP_sub_i)
          || (opcode >= MVM_OP_band_i && opcode <= MVM_OP_pow_i))  {
            MVMSpeshFacts *keya_writer_facts = MVM_spesh_get_facts(tc, g, keya_writer->operands[2]);
            basea = keya_writer->operands[1];

            /* test offset if has a value and not zero */
            /* XXX: this looks like is not smart enough */
            offseta = (keya_writer_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) && keya_writer_facts->value.i64 != 0;

            if (Operand_is_eq(basea, keyb) && offseta)
                return ALIAS_NO;  /* array[base +/-/x/÷/% offset] vs. array[base]. */
        }

        keyb_writer = MVM_spesh_get_facts(tc, g, keyb)->writer;
        opcode = keyb_writer->info->opcode;
        if ((opcode >= MVM_OP_add_i && opcode <= MVM_OP_sub_i)
          || (opcode >= MVM_OP_band_i && opcode <= MVM_OP_pow_i))  {
            MVMSpeshFacts *keyb_writer_facts = MVM_spesh_get_facts(tc, g, keyb_writer->operands[2]);
            baseb = keyb_writer->operands[1];

            /* test offset if has a value and not zero */
            /* XXX: this looks like is not smart enough */
            offsetb = (keyb_writer_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) && keyb_writer_facts->value.i64 != 0;

            if (Operand_is_eq(keya, baseb) && offsetb)
                return ALIAS_NO;  /* t[base] vs. t[base +/-/x/÷/% offset]. */
        }

        if (Operand_is_eq(basea, baseb)) {
            MVMSpeshOperand *keya_writer_operands = keya_writer->operands;
            MVMSpeshOperand *keyb_writer_operands = keyb_writer->operands;
            if (Operand_is_not_eq(keya_writer_operands[1], keyb_writer_operands[1])
              || Operand_is_not_eq(keya_writer_operands[2], keyb_writer_operands[2]))
                return ALIAS_NO;  /* t[base +/- offseta] vs. t[base +/- offsetb] and offseta != offsetb. */
        }
    } else if (opcode == MVM_OP_atkey_o) {
        MVMSpeshOperand keya = insa->operands[2], keyb = insb->operands[2];
        assert(insb->info->opcode == MVM_OP_atkey_o);

        /* Disambiguate hash references based on the type of their keys. */
        if (Operand_is_eq(keya, keyb)) {
            if (Operand_is_eq(obja, objb))
                return ALIAS_MUST;  /* Same key, same object. */
            else
                return aa_object(tc, g, obja, objb);  /* Same key, possibly different object. */
        }
    } else if (opcode == MVM_OP_getattr_o || opcode == MVM_OP_getattrs_o) {
        MVMSpeshOperand obja = insa->operands[1], objb = insb->operands[1];
        MVMSpeshOperand classnamea = insa->operands[2], classnameb = insb->operands[2];
        MVMSpeshOperand attrnamea = insa->operands[3], attrnameb = insb->operands[3];

        assert(insb->info->opcode == MVM_OP_atkey_o);

        if (Operand_is_eq(attrnamea, attrnameb)) {
            if (Operand_is_eq(classnamea, classnameb)) {
                if (Operand_is_eq(obja, objb))
                    return ALIAS_MUST;  /* Same attribute, same class, same object. */
            } else {
                /* class name always come from wval/wval_wide op */
                MVMSpeshIns *wvala =  MVM_spesh_get_facts(tc, g, classnamea)->writer;
                MVMSpeshIns *wvalb =  MVM_spesh_get_facts(tc, g, classnameb)->writer;
                MVMSpeshOperand depa = wvala->operands[1], depb = wvalb->operands[1];
                MVMSpeshOperand idxa = wvala->operands[2], idxb = wvalb->operands[2];

                assert((wvala->info->opcode == MVM_OP_wval
                    || wvala->info->opcode == MVM_OP_wval_wide)
                  && (wvalb->info->opcode == MVM_OP_wval
                    || wvalb->info->opcode == MVM_OP_wval_wide));

                /* if wval has the same Operand, means the class name is the same */
                if (Operand_is_eq(depa, depb) && Operand_is_eq(idxa, idxb)) {
                    if (Operand_is_eq(obja, objb))
                        return ALIAS_MUST;  /* Same attribute, same class, same object. */
                }
            }

            return aa_object(tc, g, obja, objb);  /* only Same attribute, possibly different object. */
        }
    }

    if (Operand_is_eq(obja, objb))
        return ALIAS_MAY;  /* Same object, cannot disambiguate keys. */
    else
        return aa_object(tc, g, obja, obja);  /* Try to disambiguate objects. */
}


/* Array and hash value and object attribute get forwarding. */
static MVMSpeshOperand aho_get_forward(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand operand) {
  MVMSpeshIns *operand_writer = MVM_spesh_get_facts(tc, g, operand)->writer;
  MVMSpeshOperand limit = operand;  /* Search limit. */
  MVMSpeshOperand ref;
}