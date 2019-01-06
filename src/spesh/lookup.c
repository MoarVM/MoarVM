/* This file contains a number of functions that do lookups of data held in
 * data structures outside of the realm of spesh. We need to be very careful
 * how we do these, in various cases. For example, in the past we had cases
 * where we tried to look up a method, which in turn triggered method cache
 * deserialization, which acquired a mutex, which would block for GC and so
 * could end up with a collection taking place while spesh was working. This
 * broke the "no GC during spesh" invariant. */

#include "moar.h"

/* Tries to get the HOW (meta-object) of an object - but only if it's already
 * available (e.g. deserialized). In the case it's not, returns NULL. */
MVMObject * MVM_spesh_try_get_how(MVMThreadContext *tc, MVMObject *obj) {
    return STABLE(obj)->HOW;
}

/* Tries to look up the method using the method cache, provided that the
 * method cache has already been deserialized. */
MVMObject * MVM_spesh_try_find_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name) {
    return STABLE(obj)->method_cache
        ? MVM_6model_find_method_cache_only(tc, obj, name)
        : NULL;
}

/* Tries to check if the method exists on the object using the method cache,
 * provided the method cache has already been deserialized. */
MVMint64 MVM_spesh_try_can_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name) {
    return STABLE(obj)->method_cache
        ? MVM_6model_can_method_cache_only(tc, obj, name)
        : -1;
}

MVMint8 MVM_spesh_get_reg_type(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMuint16 reg) {
    return sg->local_types ? sg->local_types[reg] : sg->sf->body.local_types[reg];
}

MVMint8 MVM_spesh_get_lex_type(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMuint16 outers, MVMuint16 idx) {
    if (outers == 0) {
        return sg->lexical_types ? sg->lexical_types[idx] : sg->sf->body.lexical_types[idx];
    } else {
        MVMStaticFrame *sf;
        for (sf = sg->sf; sf != NULL && outers--; sf = sf->body.outer);
        return sf->body.lexical_types[idx];
    }
}

MVMuint8 MVM_spesh_get_opr_type(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMSpeshIns *ins, MVMint32 i) {
    MVMSpeshOperand opr = ins->operands[i];
    MVMuint8 opr_kind = ins->info->operands[i];
    MVMuint8 opr_type = opr_kind & MVM_operand_type_mask;
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
    return opr_type;
}
