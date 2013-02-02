#include "moarvm.h"

/* Representation function convenience accessors. Could potentially be made into
 * macros in the future, but hopefully the compiler is smart enough to inline
 * them anyway. */

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

void MVM_repr_push_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 pushee) {
    MVMRegister value;
    value.i64 = pushee;
    REPR(obj)->pos_funcs->push(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_int64);
}

void MVM_repr_push_n(MVMThreadContext *tc, MVMObject *obj, MVMnum64 pushee) {
    MVMRegister value;
    value.n64 = pushee;
    REPR(obj)->pos_funcs->push(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_num64);
}

void MVM_repr_push_s(MVMThreadContext *tc, MVMObject *obj, MVMString *pushee) {
    MVMRegister value;
    value.s = pushee;
    REPR(obj)->pos_funcs->push(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_str);
}

void MVM_repr_push_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *pushee) {
    MVMRegister value;
    value.o = pushee;
    REPR(obj)->pos_funcs->push(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_obj);
}

MVMint64 MVM_repr_get_int(MVMThreadContext *tc, MVMObject *obj) {
    return REPR(obj)->box_funcs->get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMnum64 MVM_repr_get_num(MVMThreadContext *tc, MVMObject *obj) {
    return REPR(obj)->box_funcs->get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMString * MVM_repr_get_str(MVMThreadContext *tc, MVMObject *obj) {
    return REPR(obj)->box_funcs->get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}
