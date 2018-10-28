void MVM_repr_init(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMObject * MVM_repr_alloc(MVMThreadContext *tc, MVMObject *type);
MVM_PUBLIC MVMObject * MVM_repr_alloc_init(MVMThreadContext *tc, MVMObject *type);
MVM_PUBLIC MVMObject * MVM_repr_clone(MVMThreadContext *tc, MVMObject *obj);
void MVM_repr_compose(MVMThreadContext *tc, MVMObject *type, MVMObject *obj);

MVM_PUBLIC void MVM_repr_pos_set_elems(MVMThreadContext *tc, MVMObject *obj, MVMint64 elems);
void MVM_repr_populate_indices_array(MVMThreadContext *tc, MVMObject *arr, MVMint64 *elems);
MVM_PUBLIC void MVM_repr_set_dimensions(MVMThreadContext *tc, MVMObject *obj, MVMObject *dims);
MVM_PUBLIC MVMObject * MVM_repr_pos_slice(MVMThreadContext *tc, MVMObject *src, MVMint64 start, MVMint64 end);
MVM_PUBLIC void MVM_repr_pos_splice(MVMThreadContext *tc, MVMObject *obj, MVMObject *replacement, MVMint64 offset, MVMint64 count);

MVM_PUBLIC MVMint64    MVM_repr_at_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVM_PUBLIC MVMnum64    MVM_repr_at_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVM_PUBLIC MVMString * MVM_repr_at_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVM_PUBLIC MVMObject * MVM_repr_at_pos_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);

MVM_PUBLIC MVMint64    MVM_repr_at_pos_multidim_i(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices);
MVM_PUBLIC MVMnum64    MVM_repr_at_pos_multidim_n(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices);
MVM_PUBLIC MVMString * MVM_repr_at_pos_multidim_s(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices);
MVM_PUBLIC MVMObject * MVM_repr_at_pos_multidim_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices);

MVM_PUBLIC MVMint64    MVM_repr_at_pos_2d_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2);
MVM_PUBLIC MVMnum64    MVM_repr_at_pos_2d_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2);
MVM_PUBLIC MVMString * MVM_repr_at_pos_2d_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2);
MVM_PUBLIC MVMObject * MVM_repr_at_pos_2d_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2);

MVM_PUBLIC MVMint64    MVM_repr_at_pos_3d_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3);
MVM_PUBLIC MVMnum64    MVM_repr_at_pos_3d_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3);
MVM_PUBLIC MVMString * MVM_repr_at_pos_3d_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3);
MVM_PUBLIC MVMObject * MVM_repr_at_pos_3d_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3);

MVM_PUBLIC MVMint64 MVM_repr_exists_pos(MVMThreadContext *tc, MVMObject *obj, MVMint64 index);

MVM_PUBLIC void MVM_repr_bind_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMint64 value);
MVM_PUBLIC void MVM_repr_bind_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMnum64 value);
MVM_PUBLIC void MVM_repr_bind_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMString *value);
MVM_PUBLIC void MVM_repr_bind_pos_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMObject *value);

MVM_PUBLIC void MVM_repr_write_buf(MVMThreadContext *tc, MVMObject *obj, char *value, MVMint64 offset, MVMint64 size);
MVM_PUBLIC MVMint64 MVM_repr_read_buf(MVMThreadContext *tc, MVMObject *obj, MVMint64 offset, MVMint64 size);

MVM_PUBLIC void MVM_repr_bind_pos_multidim_i(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMint64 value);
MVM_PUBLIC void MVM_repr_bind_pos_multidim_n(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMnum64 value);
MVM_PUBLIC void MVM_repr_bind_pos_multidim_s(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMString *value);
MVM_PUBLIC void MVM_repr_bind_pos_multidim_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *indices, MVMObject *value);

MVM_PUBLIC void MVM_repr_bind_pos_2d_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 value);
MVM_PUBLIC void MVM_repr_bind_pos_2d_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMnum64 value);
MVM_PUBLIC void MVM_repr_bind_pos_2d_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMString *value);
MVM_PUBLIC void MVM_repr_bind_pos_2d_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMObject *value);

MVM_PUBLIC void MVM_repr_bind_pos_3d_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMint64 value);
MVM_PUBLIC void MVM_repr_bind_pos_3d_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMnum64 value);
MVM_PUBLIC void MVM_repr_bind_pos_3d_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMString *value);
MVM_PUBLIC void MVM_repr_bind_pos_3d_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx1, MVMint64 idx2, MVMint64 idx3, MVMObject *value);

MVM_PUBLIC void MVM_repr_push_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 pushee);
MVM_PUBLIC void MVM_repr_push_n(MVMThreadContext *tc, MVMObject *obj, MVMnum64 pushee);
MVM_PUBLIC void MVM_repr_push_s(MVMThreadContext *tc, MVMObject *obj, MVMString *pushee);
MVM_PUBLIC void MVM_repr_push_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *pushee);

MVM_PUBLIC void MVM_repr_unshift_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 unshiftee);
MVM_PUBLIC void MVM_repr_unshift_n(MVMThreadContext *tc, MVMObject *obj, MVMnum64 unshiftee);
MVM_PUBLIC void MVM_repr_unshift_s(MVMThreadContext *tc, MVMObject *obj, MVMString *unshiftee);
MVM_PUBLIC void MVM_repr_unshift_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *unshiftee);

MVM_PUBLIC MVMint64    MVM_repr_pop_i(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMnum64    MVM_repr_pop_n(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMString * MVM_repr_pop_s(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMObject * MVM_repr_pop_o(MVMThreadContext *tc, MVMObject *obj);

MVM_PUBLIC MVMint64    MVM_repr_shift_i(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMnum64    MVM_repr_shift_n(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMString * MVM_repr_shift_s(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMObject * MVM_repr_shift_o(MVMThreadContext *tc, MVMObject *obj);

MVM_PUBLIC MVMint64    MVM_repr_at_key_i(MVMThreadContext *tc, MVMObject *obj, MVMString *key);
MVM_PUBLIC MVMnum64    MVM_repr_at_key_n(MVMThreadContext *tc, MVMObject *obj, MVMString *key);
MVM_PUBLIC MVMString * MVM_repr_at_key_s(MVMThreadContext *tc, MVMObject *obj, MVMString *key);
MVM_PUBLIC MVMObject * MVM_repr_at_key_o(MVMThreadContext *tc, MVMObject *obj, MVMString *key);

MVM_PUBLIC void MVM_repr_bind_key_i(MVMThreadContext *tc, MVMObject *obj, MVMString *key, MVMint64 val);
MVM_PUBLIC void MVM_repr_bind_key_n(MVMThreadContext *tc, MVMObject *obj, MVMString *key, MVMnum64 val);
MVM_PUBLIC void MVM_repr_bind_key_s(MVMThreadContext *tc, MVMObject *obj, MVMString *key, MVMString *val);
MVM_PUBLIC void MVM_repr_bind_key_o(MVMThreadContext *tc, MVMObject *obj, MVMString *key, MVMObject *val);

MVM_PUBLIC MVMint64 MVM_repr_exists_key(MVMThreadContext *tc, MVMObject *obj, MVMString *key);
MVM_PUBLIC void MVM_repr_delete_key(MVMThreadContext *tc, MVMObject *obj, MVMString *key);

MVM_PUBLIC MVMuint64 MVM_repr_elems(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMObject * MVM_repr_dimensions(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMint64 MVM_repr_num_dimensions(MVMThreadContext *tc, MVMObject *obj);

MVM_PUBLIC MVMint64 MVM_repr_get_int(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMnum64 MVM_repr_get_num(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMString * MVM_repr_get_str(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC MVMuint64 MVM_repr_get_uint(MVMThreadContext *tc, MVMObject *obj);
MVM_PUBLIC void MVM_repr_set_int(MVMThreadContext *tc, MVMObject *obj, MVMint64 val);
MVM_PUBLIC void MVM_repr_set_num(MVMThreadContext *tc, MVMObject *obj, MVMnum64 val);
MVM_PUBLIC void MVM_repr_set_str(MVMThreadContext *tc, MVMObject *obj, MVMString *val);
MVM_PUBLIC void MVM_repr_set_uint(MVMThreadContext *tc, MVMObject *obj, MVMuint64 val);

MVM_PUBLIC MVMObject * MVM_repr_box_int(MVMThreadContext *tc, MVMObject *type, MVMint64 val);
MVM_PUBLIC MVMObject * MVM_repr_box_num(MVMThreadContext *tc, MVMObject *type, MVMnum64 val);
MVM_PUBLIC MVMObject * MVM_repr_box_str(MVMThreadContext *tc, MVMObject *type, MVMString *val);
MVM_PUBLIC MVMObject * MVM_repr_box_uint(MVMThreadContext *tc, MVMObject *type, MVMuint64 val);

MVM_PUBLIC MVMint64    MVM_repr_get_attr_i(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                           MVMString *name, MVMint16 hint);
MVM_PUBLIC MVMnum64    MVM_repr_get_attr_n(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                           MVMString *name, MVMint16 hint);
MVM_PUBLIC MVMString * MVM_repr_get_attr_s(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                           MVMString *name, MVMint16 hint);
MVM_PUBLIC MVMObject * MVM_repr_get_attr_o(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                           MVMString *name, MVMint16 hint);

MVM_PUBLIC void        MVM_repr_bind_attr_inso(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                               MVMString *name, MVMint16 hint, MVMRegister value_reg, MVMuint16 kind);

MVM_PUBLIC MVMint64   MVM_repr_attribute_inited(MVMThreadContext *tc, MVMObject *object, MVMObject *type,
                                                MVMString *name);

MVM_PUBLIC MVMint64    MVM_repr_compare_repr_id(MVMThreadContext *tc, MVMObject *object, MVMuint32 REPRId);

MVM_PUBLIC MVMint64    MVM_repr_hint_for(MVMThreadContext *tc, MVMObject *object, MVMString *attrname);

MVM_PUBLIC void MVM_repr_atomic_bind_attr_o(MVMThreadContext *tc, MVMObject *object,
                                            MVMObject *type, MVMString *name,
                                            MVMObject *value);

MVM_PUBLIC MVMObject * MVM_repr_casattr_o(MVMThreadContext *tc, MVMObject *object,
                                          MVMObject *type, MVMString *name,
                                          MVMObject *expected, MVMObject *value);

#define MVM_repr_at_key_int(tc, obj, key) \
    MVM_repr_get_int((tc), MVM_repr_at_key_o((tc), (obj), (key)))
#define MVM_repr_at_key_num(tc, obj, key) \
    MVM_repr_get_num((tc), MVM_repr_at_key_o((tc), (obj), (key)))
#define MVM_repr_at_key_str(tc, obj, key) \
    MVM_repr_get_str((tc), MVM_repr_at_key_o((tc), (obj), (key)))

#define MVM_repr_bind_key_int(tc, obj, key, val) do { \
    MVMObject *boxed = MVM_repr_box_int((tc), (*((tc)->interp_cu))->body.hll_config->int_box_type, (val)); \
    MVM_repr_bind_key_o((tc), (obj), (key), boxed); \
} while (0)

#define MVM_repr_bind_key_num(tc, obj, key, val) do {\
    MVMObject *boxed = MVM_repr_box_int((tc), (*((tc)->interp_cu))->body.hll_config->num_box_type, (val)); \
    MVM_repr_bind_key_o((tc), (obj), (key), boxed); \
} while (0)

#define MVM_repr_bind_key_str(tc, obj, key, val) do {\
    MVMObject *boxed = MVM_repr_box_int((tc), (*((tc)->interp_cu))->body.hll_config->str_box_type, (val)); \
    MVM_repr_bind_key_o((tc), (obj), (key), boxed); \
} while (0)
