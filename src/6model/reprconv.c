#include "moar.h"

/* Representation function convenience accessors. Could potentially be made into
 * macros in the future, but hopefully the compiler is smart enough to inline
 * them anyway. */

void MVM_repr_init(MVMThreadContext *tc, MVMObject *obj) {
    if (REPR(obj)->initialize)
        REPR(obj)->initialize(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMObject * MVM_repr_alloc_init(MVMThreadContext *tc, MVMObject *type) {
    MVMObject *obj = REPR(type)->allocate(tc, STABLE(type));

    if (REPR(obj)->initialize) {
        MVMROOT(tc, obj, {
            REPR(obj)->initialize(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        });
    }

    return obj;
}

MVMObject * MVM_repr_clone(MVMThreadContext *tc, MVMObject *obj) {
    MVMObject *res;

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&obj);
    res = REPR(obj)->allocate(tc, STABLE(obj));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&res);
    REPR(obj)->copy_to(tc, STABLE(obj), OBJECT_BODY(obj), res, OBJECT_BODY(res));
    MVM_gc_root_temp_pop_n(tc, 2);
    return res;
}

void MVM_repr_compose(MVMThreadContext *tc, MVMObject *type, MVMObject *obj) {
    REPR(type)->compose(tc, STABLE(type), obj);
}

MVMint64 MVM_repr_at_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMRegister value;
    REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, &value, MVM_reg_int64);
    return value.i64;
}

MVMnum64 MVM_repr_at_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMRegister value;
    REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, &value, MVM_reg_num64);
    return value.n64;
}

MVMString * MVM_repr_at_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMRegister value;
    REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, &value, MVM_reg_str);
    return value.s;
}

MVMObject * MVM_repr_at_pos_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMRegister value;
    REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, &value, MVM_reg_obj);
    return value.o;
}

void MVM_repr_bind_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMint64 value) {
    MVMRegister val;
    val.i64 = value;
    REPR(obj)->pos_funcs.bind_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, val, MVM_reg_int64);
}

void MVM_repr_bind_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMnum64 value) {
    MVMRegister val;
    val.n64 = value;
    REPR(obj)->pos_funcs.bind_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, val, MVM_reg_num64);
}

void MVM_repr_bind_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMString *value) {
    MVMRegister val;
    val.s = value;
    REPR(obj)->pos_funcs.bind_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, val, MVM_reg_str);
}

void MVM_repr_bind_pos_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMObject *value) {
    MVMRegister val;
    val.o = value;
    REPR(obj)->pos_funcs.bind_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        idx, val, MVM_reg_obj);
}

void MVM_repr_push_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 pushee) {
    MVMRegister value;
    value.i64 = pushee;
    REPR(obj)->pos_funcs.push(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_int64);
}

void MVM_repr_push_n(MVMThreadContext *tc, MVMObject *obj, MVMnum64 pushee) {
    MVMRegister value;
    value.n64 = pushee;
    REPR(obj)->pos_funcs.push(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_num64);
}

void MVM_repr_push_s(MVMThreadContext *tc, MVMObject *obj, MVMString *pushee) {
    MVMRegister value;
    value.s = pushee;
    REPR(obj)->pos_funcs.push(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_str);
}

void MVM_repr_push_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *pushee) {
    MVMRegister value;
    value.o = pushee;
    REPR(obj)->pos_funcs.push(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_obj);
}

void MVM_repr_unshift_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 unshiftee) {
    MVMRegister value;
    value.i64 = unshiftee;
    REPR(obj)->pos_funcs.unshift(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_int64);
}

void MVM_repr_unshift_n(MVMThreadContext *tc, MVMObject *obj, MVMnum64 unshiftee) {
    MVMRegister value;
    value.n64 = unshiftee;
    REPR(obj)->pos_funcs.unshift(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_num64);
}

void MVM_repr_unshift_s(MVMThreadContext *tc, MVMObject *obj, MVMString *unshiftee) {
    MVMRegister value;
    value.s = unshiftee;
    REPR(obj)->pos_funcs.unshift(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_str);
}

void MVM_repr_unshift_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *unshiftee) {
    MVMRegister value;
    value.o = unshiftee;
    REPR(obj)->pos_funcs.unshift(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        value, MVM_reg_obj);
}

MVMint64 MVM_repr_shift_i(MVMThreadContext *tc, MVMObject *obj) {
    MVMRegister value;
    REPR(obj)->pos_funcs.shift(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        &value, MVM_reg_int64);
    return value.i64;
}

MVMnum64 MVM_repr_shift_n(MVMThreadContext *tc, MVMObject *obj) {
    MVMRegister value;
    REPR(obj)->pos_funcs.shift(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        &value, MVM_reg_num64);
    return value.n64;
}

MVMString * MVM_repr_shift_s(MVMThreadContext *tc, MVMObject *obj) {
    MVMRegister value;
    REPR(obj)->pos_funcs.shift(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        &value, MVM_reg_str);
    return value.s;
}

MVMObject * MVM_repr_shift_o(MVMThreadContext *tc, MVMObject *obj) {
    MVMRegister value;
    REPR(obj)->pos_funcs.shift(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        &value, MVM_reg_obj);
    return value.o;
}

MVMObject * MVM_repr_at_key_o(MVMThreadContext *tc, MVMObject *obj, MVMString *key) {
    MVMRegister value;
    REPR(obj)->ass_funcs.at_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        (MVMObject *)key, &value, MVM_reg_obj);
    return value.o;
}

void MVM_repr_bind_key_o(MVMThreadContext *tc, MVMObject *obj, MVMString *key, MVMObject *val) {
    MVMRegister value;
    value.o = val;
    REPR(obj)->ass_funcs.bind_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        (MVMObject *)key, value, MVM_reg_obj);
}

MVMint64 MVM_repr_exists_key(MVMThreadContext *tc, MVMObject *obj, MVMString *key) {
    return REPR(obj)->ass_funcs.exists_key(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), (MVMObject *)key);
}

void MVM_repr_delete_key(MVMThreadContext *tc, MVMObject *obj, MVMString *key) {
    REPR(obj)->ass_funcs.delete_key(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), (MVMObject *)key);
}

MVMuint64 MVM_repr_elems(MVMThreadContext *tc, MVMObject *obj) {
    return REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMint64 MVM_repr_get_int(MVMThreadContext *tc, MVMObject *obj) {
    return REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMnum64 MVM_repr_get_num(MVMThreadContext *tc, MVMObject *obj) {
    return REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMString * MVM_repr_get_str(MVMThreadContext *tc, MVMObject *obj) {
    return REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

void MVM_repr_set_int(MVMThreadContext *tc, MVMObject *obj, MVMint64 val) {
    REPR(obj)->box_funcs.set_int(tc, STABLE(obj), obj, OBJECT_BODY(obj), val);
}

void MVM_repr_set_num(MVMThreadContext *tc, MVMObject *obj, MVMnum64 val) {
    REPR(obj)->box_funcs.set_num(tc, STABLE(obj), obj, OBJECT_BODY(obj), val);
}

void MVM_repr_set_str(MVMThreadContext *tc, MVMObject *obj, MVMString *val) {
    REPR(obj)->box_funcs.set_str(tc, STABLE(obj), obj, OBJECT_BODY(obj), val);
}

MVMObject * MVM_repr_box_int(MVMThreadContext *tc, MVMObject *type, MVMint64 val) {
    MVMObject *res = MVM_repr_alloc_init(tc, type);
    MVM_repr_set_int(tc, res, val);
    return res;
}

MVMObject * MVM_repr_box_num(MVMThreadContext *tc, MVMObject *type, MVMnum64 val) {
    MVMObject *res = MVM_repr_alloc_init(tc, type);
    MVM_repr_set_num(tc, res, val);
    return res;
}

MVMObject * MVM_repr_box_str(MVMThreadContext *tc, MVMObject *type, MVMString *val) {
    MVMObject *res;
    MVMROOT(tc, val, {
        res = MVM_repr_alloc_init(tc, type);
        MVM_repr_set_str(tc, res, val);
    });
    return res;
}
