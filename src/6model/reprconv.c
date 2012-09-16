#include "moarvm.h"

/* Representation function convenience accessors. Make code that needs to make
 * calls where performance isn't terribly important more convenient to write. */

MVMint64 MVM_repr_at_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMRegister value;
    REPR(obj)->pos_funcs->at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, &value, MVM_reg_int64);
    return value.i64;
}

MVMnum64 MVM_repr_at_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMRegister value;
    REPR(obj)->pos_funcs->at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, &value, MVM_reg_num64);
    return value.n64;
}

MVMString * MVM_repr_at_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMRegister value;
    REPR(obj)->pos_funcs->at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, &value, MVM_reg_str);
    return value.s;
}

MVMObject * MVM_repr_at_pos_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMRegister value;
    REPR(obj)->pos_funcs->at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, &value, MVM_reg_obj);
    return value.o;
}
