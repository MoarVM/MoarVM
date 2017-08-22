#include "moar.h"

/* Representation function convenience accessors. Could potentially be made into
 * macros in the future, but hopefully the compiler is smart enough to inline
 * them anyway. */

void MVM_repr_init(MVMThreadContext *tc, MVMObject *obj) {
    if (REPR(obj)->initialize)
        REPR(obj)->initialize(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMObject * MVM_repr_alloc(MVMThreadContext *tc, MVMObject *type) {
    return REPR(type)->allocate(tc, STABLE(type));
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
    if (IS_CONCRETE(obj)) {
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&obj);
        res = REPR(obj)->allocate(tc, STABLE(obj));
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&res);
        REPR(obj)->copy_to(tc, STABLE(obj), OBJECT_BODY(obj), res, OBJECT_BODY(res));
        MVM_gc_root_temp_pop_n(tc, 2);
    } else {
        res = obj;
    }
    return res;
}

void MVM_repr_compose(MVMThreadContext *tc, MVMObject *type, MVMObject *obj) {
    REPR(type)->compose(tc, STABLE(type), obj);
}

MVM_PUBLIC void MVM_repr_pos_set_elems(MVMThreadContext *tc, MVMObject *obj, MVMint64 elems) {
    REPR(obj)->pos_funcs.set_elems(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), elems);
}

void MVM_repr_populate_indices_array(MVMThreadContext *tc, MVMObject *arr, MVMint64 *elems) {
    MVMint64 i;
    *elems = MVM_repr_elems(tc, arr);
    if (*elems > tc->num_multi_dim_indices)
        tc->multi_dim_indices = MVM_realloc(tc->multi_dim_indices,
            *elems * sizeof(MVMint64));
    for (i = 0; i < *elems; i++)
        tc->multi_dim_indices[i] = MVM_repr_at_pos_i(tc, arr, i);
}

void MVM_repr_set_dimensions(MVMThreadContext *tc, MVMObject *obj, MVMObject *dims) {
    if (IS_CONCRETE(obj)) {
        MVMint64 num_dims;
        MVM_repr_populate_indices_array(tc, dims, &num_dims);
        REPR(obj)->pos_funcs.set_dimensions(tc, STABLE(obj), obj,
            OBJECT_BODY(obj), num_dims, tc->multi_dim_indices);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot set dimensions on a type object");
    }
}

MVM_PUBLIC void MVM_repr_pos_splice(MVMThreadContext *tc, MVMObject *obj, MVMObject *replacement, MVMint64 offset, MVMint64 count) {
    REPR(obj)->pos_funcs.splice(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), replacement,
        offset, count);
}

MVM_PUBLIC MVMint64 MVM_repr_exists_pos(MVMThreadContext *tc, MVMObject *obj, MVMint64 index) {
    MVMint64 elems = REPR(obj)->elems(tc, STABLE(obj), obj, OBJECT_BODY(obj));
    if (index < 0)
        index += elems;
    return index >= 0 && index < elems && !MVM_is_null(tc, MVM_repr_at_pos_o(tc, obj, index));
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
    if (IS_CONCRETE(obj)) {
        MVMRegister value;
        REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                                        idx, &value, MVM_reg_obj);
        return value.o;
    }
    return tc->instance->VMNull;
}

static void at_pos_multidim(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMRegister *value, MVMuint16 kind) {
    MVMint64 num_indices;
    MVM_repr_populate_indices_array(tc, indices, &num_indices);
    REPR(obj)->pos_funcs.at_pos_multidim(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), num_indices, tc->multi_dim_indices, value, kind);
}

MVMint64 MVM_repr_at_pos_multidim_i(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices) {
    MVMRegister r;
    at_pos_multidim(tc, obj, indices, &r, MVM_reg_int64);
    return r.i64;
}

MVMnum64 MVM_repr_at_pos_multidim_n(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices) {
    MVMRegister r;
    at_pos_multidim(tc, obj, indices, &r, MVM_reg_num64);
    return r.n64;
}

MVMString * MVM_repr_at_pos_multidim_s(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices) {
    MVMRegister r;
    at_pos_multidim(tc, obj, indices, &r, MVM_reg_str);
    return r.s;
}

MVMObject * MVM_repr_at_pos_multidim_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices) {
    MVMRegister r;
    at_pos_multidim(tc, obj, indices, &r, MVM_reg_obj);
    return r.o;
}

static void at_pos_2d(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMRegister *value, MVMuint16 kind) {
    MVMint64 c_indices[2] = { idx1, idx2 };
    REPR(obj)->pos_funcs.at_pos_multidim(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), 2, c_indices, value, kind);
}

MVMint64 MVM_repr_at_pos_2d_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2) {
    MVMRegister r;
    at_pos_2d(tc, obj, idx1, idx2, &r, MVM_reg_int64);
    return r.i64;
}

MVMnum64 MVM_repr_at_pos_2d_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2) {
    MVMRegister r;
    at_pos_2d(tc, obj, idx1, idx2, &r, MVM_reg_num64);
    return r.n64;
}

MVMString * MVM_repr_at_pos_2d_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2) {
    MVMRegister r;
    at_pos_2d(tc, obj, idx1, idx2, &r, MVM_reg_str);
    return r.s;
}

MVMObject * MVM_repr_at_pos_2d_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2) {
    MVMRegister r;
    at_pos_2d(tc, obj, idx1, idx2, &r, MVM_reg_obj);
    return r.o;
}

static void at_pos_3d(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMRegister *value, MVMuint16 kind) {
    MVMint64 c_indices[3] = { idx1, idx2, idx3 };
    REPR(obj)->pos_funcs.at_pos_multidim(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), 3, c_indices, value, kind);
}

MVMint64 MVM_repr_at_pos_3d_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3) {
    MVMRegister r;
    at_pos_3d(tc, obj, idx1, idx2, idx3, &r, MVM_reg_int64);
    return r.i64;
}

MVMnum64 MVM_repr_at_pos_3d_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3) {
    MVMRegister r;
    at_pos_3d(tc, obj, idx1, idx2, idx3, &r, MVM_reg_num64);
    return r.n64;
}

MVMString * MVM_repr_at_pos_3d_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3) {
    MVMRegister r;
    at_pos_3d(tc, obj, idx1, idx2, idx3, &r, MVM_reg_str);
    return r.s;
}

MVMObject * MVM_repr_at_pos_3d_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3) {
    MVMRegister r;
    at_pos_3d(tc, obj, idx1, idx2, idx3, &r, MVM_reg_obj);
    return r.o;
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

static void bind_pos_multidim(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMRegister value, MVMuint16 kind) {
    MVMint64 num_indices;
    MVM_repr_populate_indices_array(tc, indices, &num_indices);
    REPR(obj)->pos_funcs.bind_pos_multidim(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), num_indices, tc->multi_dim_indices, value, kind);
}

void MVM_repr_bind_pos_multidim_i(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMint64 value) {
    MVMRegister r;
    r.i64 = value;
    bind_pos_multidim(tc, obj, indices, r, MVM_reg_int64);
}
void MVM_repr_bind_pos_multidim_n(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMnum64 value) {
    MVMRegister r;
    r.n64 = value;
    bind_pos_multidim(tc, obj, indices, r, MVM_reg_num64);
}
void MVM_repr_bind_pos_multidim_s(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMString *value) {
    MVMRegister r;
    r.s = value;
    bind_pos_multidim(tc, obj, indices, r, MVM_reg_str);
}
void MVM_repr_bind_pos_multidim_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMObject *value) {
    MVMRegister r;
    r.o = value;
    bind_pos_multidim(tc, obj, indices, r, MVM_reg_obj);
}

static void bind_pos_2d(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMRegister value, MVMuint16 kind) {
    MVMint64 c_indices[2] = { idx1, idx2 };
    REPR(obj)->pos_funcs.bind_pos_multidim(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), 2, c_indices, value, kind);
}

void MVM_repr_bind_pos_2d_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 value) {
    MVMRegister r;
    r.i64 = value;
    bind_pos_2d(tc, obj, idx1, idx2, r, MVM_reg_int64);
}
void MVM_repr_bind_pos_2d_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMnum64 value) {
    MVMRegister r;
    r.n64 = value;
    bind_pos_2d(tc, obj, idx1, idx2, r, MVM_reg_num64);
}
void MVM_repr_bind_pos_2d_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMString *value) {
    MVMRegister r;
    r.s = value;
    bind_pos_2d(tc, obj, idx1, idx2, r, MVM_reg_str);
}
void MVM_repr_bind_pos_2d_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMObject *value) {
    MVMRegister r;
    r.o = value;
    bind_pos_2d(tc, obj, idx1, idx2, r, MVM_reg_obj);
}

static void bind_pos_3d(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMRegister value, MVMuint16 kind) {
    MVMint64 c_indices[3] = { idx1, idx2, idx3 };
    REPR(obj)->pos_funcs.bind_pos_multidim(tc, STABLE(obj), obj,
        OBJECT_BODY(obj), 3, c_indices, value, kind);
}

void MVM_repr_bind_pos_3d_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMint64 value) {
    MVMRegister r;
    r.i64 = value;
    bind_pos_3d(tc, obj, idx1, idx2, idx3, r, MVM_reg_int64);
}
void MVM_repr_bind_pos_3d_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMnum64 value) {
    MVMRegister r;
    r.n64 = value;
    bind_pos_3d(tc, obj, idx1, idx2, idx3, r, MVM_reg_num64);
}
void MVM_repr_bind_pos_3d_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMString *value) {
    MVMRegister r;
    r.s = value;
    bind_pos_3d(tc, obj, idx1, idx2, idx3, r, MVM_reg_str);
}
void MVM_repr_bind_pos_3d_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMObject *value) {
    MVMRegister r;
    r.o = value;
    bind_pos_3d(tc, obj, idx1, idx2, idx3, r, MVM_reg_obj);
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

MVMint64 MVM_repr_pop_i(MVMThreadContext *tc, MVMObject *obj) {
    MVMRegister value;
    REPR(obj)->pos_funcs.pop(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        &value, MVM_reg_int64);
    return value.i64;
}

MVMnum64 MVM_repr_pop_n(MVMThreadContext *tc, MVMObject *obj) {
    MVMRegister value;
    REPR(obj)->pos_funcs.pop(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        &value, MVM_reg_num64);
    return value.n64;
}

MVMString * MVM_repr_pop_s(MVMThreadContext *tc, MVMObject *obj) {
    MVMRegister value;
    REPR(obj)->pos_funcs.pop(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        &value, MVM_reg_str);
    return value.s;
}

MVMObject * MVM_repr_pop_o(MVMThreadContext *tc, MVMObject *obj) {
    MVMRegister value;
    REPR(obj)->pos_funcs.pop(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        &value, MVM_reg_obj);
    return value.o;
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

MVMint64 MVM_repr_at_key_i(MVMThreadContext *tc, MVMObject *obj, MVMString *key) {
    MVMRegister value;
    REPR(obj)->ass_funcs.at_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                                (MVMObject *)key, &value, MVM_reg_int64);
    return value.i64;
}

MVMnum64 MVM_repr_at_key_n(MVMThreadContext *tc, MVMObject *obj, MVMString *key) {
    MVMRegister value;
    REPR(obj)->ass_funcs.at_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                                (MVMObject *)key, &value, MVM_reg_num64);
    return value.n64;
}

MVMString * MVM_repr_at_key_s(MVMThreadContext *tc, MVMObject *obj, MVMString *key) {
    MVMRegister value;
    REPR(obj)->ass_funcs.at_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                                (MVMObject *)key, &value, MVM_reg_str);
    return value.s;
}

MVMObject * MVM_repr_at_key_o(MVMThreadContext *tc, MVMObject *obj, MVMString *key) {
    if (IS_CONCRETE(obj)) {
        MVMRegister value;
        REPR(obj)->ass_funcs.at_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                                    (MVMObject *)key, &value, MVM_reg_obj);
        return value.o;
    }
    return tc->instance->VMNull;
}

void MVM_repr_bind_key_i(MVMThreadContext *tc, MVMObject *obj, MVMString *key, MVMint64 val) {
    MVMRegister value;
    value.i64 = val;
    REPR(obj)->ass_funcs.bind_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        (MVMObject *)key, value, MVM_reg_int64);
}

void MVM_repr_bind_key_n(MVMThreadContext *tc, MVMObject *obj, MVMString *key, MVMnum64 val) {
    MVMRegister value;
    value.n64 = val;
    REPR(obj)->ass_funcs.bind_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        (MVMObject *)key, value, MVM_reg_num64);
}

void MVM_repr_bind_key_s(MVMThreadContext *tc, MVMObject *obj, MVMString *key, MVMString *val) {
    MVMRegister value;
    value.s = val;
    REPR(obj)->ass_funcs.bind_key(tc, STABLE(obj), obj, OBJECT_BODY(obj),
        (MVMObject *)key, value, MVM_reg_str);
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

MVMObject * MVM_repr_dimensions(MVMThreadContext *tc, MVMObject *obj) {
    if (IS_CONCRETE(obj)) {
        MVMint64 num_dims, i;
        MVMint64 *dims;
        MVMObject *result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIntArray);
        REPR(obj)->pos_funcs.dimensions(tc, STABLE(obj), obj, OBJECT_BODY(obj),
            &num_dims, &dims);
        for (i = 0; i < num_dims; i++)
            MVM_repr_bind_pos_i(tc, result, i, dims[i]);
        return result;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot get dimensions of a type object");
    }
}

MVMint64 MVM_repr_num_dimensions(MVMThreadContext *tc, MVMObject *obj) {
    if (IS_CONCRETE(obj)) {
        MVMint64 num_dims;
        MVMint64 *_;
        REPR(obj)->pos_funcs.dimensions(tc, STABLE(obj), obj, OBJECT_BODY(obj),
            &num_dims, &_);
        return num_dims;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot get number of dimensions of a type object");
    }
}

MVMint64 MVM_repr_get_int(MVMThreadContext *tc, MVMObject *obj) {
    if (!IS_CONCRETE(obj))
        MVM_exception_throw_adhoc(tc, "Cannot unbox a type object (%s) to int.", STABLE(obj)->debug_name);
    return REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMnum64 MVM_repr_get_num(MVMThreadContext *tc, MVMObject *obj) {
    if (!IS_CONCRETE(obj))
        MVM_exception_throw_adhoc(tc, "Cannot unbox a type object (%s) to a num.", STABLE(obj)->debug_name);
    return REPR(obj)->box_funcs.get_num(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMString * MVM_repr_get_str(MVMThreadContext *tc, MVMObject *obj) {
    if (!IS_CONCRETE(obj))
        MVM_exception_throw_adhoc(tc, "Cannot unbox a type object (%s) to a str.", STABLE(obj)->debug_name);
    return REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj));
}

MVMuint64 MVM_repr_get_uint(MVMThreadContext *tc, MVMObject *obj) {
    if (!IS_CONCRETE(obj))
        MVM_exception_throw_adhoc(tc, "Cannot unbox a type object (%s) to an unsigned int.", STABLE(obj)->debug_name);
    return REPR(obj)->box_funcs.get_uint(tc, STABLE(obj), obj, OBJECT_BODY(obj));
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

void MVM_repr_set_uint(MVMThreadContext *tc, MVMObject *obj, MVMuint64 val) {
    REPR(obj)->box_funcs.set_uint(tc, STABLE(obj), obj, OBJECT_BODY(obj), val);
}

MVMObject * MVM_repr_box_int(MVMThreadContext *tc, MVMObject *type, MVMint64 val) {
    MVMObject *res;
    res = MVM_intcache_get(tc, type, val);
    if (res == 0) {
        res = MVM_repr_alloc_init(tc, type);
        MVM_repr_set_int(tc, res, val);
    }
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

MVMObject * MVM_repr_box_uint(MVMThreadContext *tc, MVMObject *type, MVMuint64 val) {
    MVMObject *res = MVM_repr_alloc_init(tc, type);
    MVM_repr_set_uint(tc, res, val);
    return res;
}

MVM_PUBLIC MVMint64 MVM_repr_get_attr_i(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                           MVMString *name, MVMint16 hint) {
    MVMRegister result_reg;
    if (!IS_CONCRETE(object))
        MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a %s type object", STABLE(object)->debug_name);
    REPR(object)->attr_funcs.get_attribute(tc,
            STABLE(object), object, OBJECT_BODY(object),
            type, name,
            hint, &result_reg, MVM_reg_int64);
    return result_reg.i64;
}

MVM_PUBLIC MVMnum64 MVM_repr_get_attr_n(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                           MVMString *name, MVMint16 hint) {
    MVMRegister result_reg;
    if (!IS_CONCRETE(object))
        MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a %s type object", STABLE(object)->debug_name);
    REPR(object)->attr_funcs.get_attribute(tc,
            STABLE(object), object, OBJECT_BODY(object),
            type, name,
            hint, &result_reg, MVM_reg_num64);
    return result_reg.n64;
}

MVM_PUBLIC MVMString * MVM_repr_get_attr_s(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                           MVMString *name, MVMint16 hint) {
    MVMRegister result_reg;
    if (!IS_CONCRETE(object))
        MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a %s type object", STABLE(object)->debug_name);
    REPR(object)->attr_funcs.get_attribute(tc,
            STABLE(object), object, OBJECT_BODY(object),
            type, name,
            hint, &result_reg, MVM_reg_str);
    return result_reg.s;
}

MVM_PUBLIC MVMObject * MVM_repr_get_attr_o(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                           MVMString *name, MVMint16 hint) {
    MVMRegister result_reg;
    if (!IS_CONCRETE(object))
        MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a %s type object", STABLE(object)->debug_name);
    REPR(object)->attr_funcs.get_attribute(tc,
            STABLE(object), object, OBJECT_BODY(object),
            type, name,
            hint, &result_reg, MVM_reg_obj);
    return result_reg.o;
}


MVM_PUBLIC void MVM_repr_bind_attr_inso(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                           MVMString *name, MVMint16 hint, MVMRegister value_reg, MVMuint16 kind) {
    if (!IS_CONCRETE(object))
        MVM_exception_throw_adhoc(tc, "Cannot bind attributes in a %s type object", STABLE(object)->debug_name);
    REPR(object)->attr_funcs.bind_attribute(tc,
            STABLE(object), object, OBJECT_BODY(object),
            type, name,
            hint, value_reg, kind);
    MVM_SC_WB_OBJ(tc, object);
}

MVM_PUBLIC MVMint64 MVM_repr_attribute_inited(MVMThreadContext *tc, MVMObject *obj, MVMObject *type,
                                              MVMString *name) {
    if (!IS_CONCRETE(obj))
        MVM_exception_throw_adhoc(tc, "Cannot look up attributes in a %s type object", STABLE(obj)->debug_name);
    return REPR(obj)->attr_funcs.is_attribute_initialized(tc,
        STABLE(obj), OBJECT_BODY(obj),
        type, name, MVM_NO_HINT);
}

MVM_PUBLIC MVMint64    MVM_repr_compare_repr_id(MVMThreadContext *tc, MVMObject *object, MVMuint32 REPRId) {
    return object && REPR(object)->ID == REPRId ? 1 : 0;
}

MVM_PUBLIC MVMint64    MVM_repr_hint_for(MVMThreadContext *tc, MVMObject *object, MVMString *attrname) {
    return REPR(object)->attr_funcs.hint_for(tc, STABLE(object), object, attrname);
}
