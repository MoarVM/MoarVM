void MVM_repr_init(MVMThreadContext *tc, MVMObject *obj);
MVMObject * MVM_repr_alloc_init(MVMThreadContext *tc, MVMObject *type);
MVMObject * MVM_repr_clone(MVMThreadContext *tc, MVMObject *obj);
void MVM_repr_compose(MVMThreadContext *tc, MVMObject *type, MVMObject *obj);

MVMint64 MVM_repr_at_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVMnum64 MVM_repr_at_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVMString * MVM_repr_at_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVMObject * MVM_repr_at_pos_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);

void MVM_repr_bind_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMint64 value);
void MVM_repr_bind_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMnum64 value);
void MVM_repr_bind_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMString *value);
void MVM_repr_bind_pos_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx, MVMObject *value);

void MVM_repr_push_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 pushee);
void MVM_repr_push_n(MVMThreadContext *tc, MVMObject *obj, MVMnum64 pushee);
void MVM_repr_push_s(MVMThreadContext *tc, MVMObject *obj, MVMString *pushee);
void MVM_repr_push_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *pushee);

MVMint64 MVM_repr_shift_i(MVMThreadContext *tc, MVMObject *obj);
MVMnum64 MVM_repr_shift_n(MVMThreadContext *tc, MVMObject *obj);
MVMString * MVM_repr_shift_s(MVMThreadContext *tc, MVMObject *obj);
MVMObject * MVM_repr_shift_o(MVMThreadContext *tc, MVMObject *obj);

MVMObject * MVM_repr_at_key_boxed(MVMThreadContext *tc, MVMObject *obj, MVMString *key);
void MVM_repr_bind_key_boxed(MVMThreadContext *tc, MVMObject *obj, MVMString *key, MVMObject *val);
MVMint64 MVM_repr_exists_key(MVMThreadContext *tc, MVMObject *obj, MVMString *key);
void MVM_repr_delete_key(MVMThreadContext *tc, MVMObject *obj, MVMString *key);

MVMuint64 MVM_repr_elems(MVMThreadContext *tc, MVMObject *obj);

MVMint64 MVM_repr_get_int(MVMThreadContext *tc, MVMObject *obj);
MVMnum64 MVM_repr_get_num(MVMThreadContext *tc, MVMObject *obj);
MVMString * MVM_repr_get_str(MVMThreadContext *tc, MVMObject *obj);
void MVM_repr_set_int(MVMThreadContext *tc, MVMObject *obj, MVMint64 val);
void MVM_repr_set_num(MVMThreadContext *tc, MVMObject *obj, MVMnum64 val);
void MVM_repr_set_str(MVMThreadContext *tc, MVMObject *obj, MVMString *val);

MVMObject * MVM_repr_box_int(MVMThreadContext *tc, MVMObject *type, MVMint64 val);
MVMObject * MVM_repr_box_num(MVMThreadContext *tc, MVMObject *type, MVMnum64 val);
MVMObject * MVM_repr_box_str(MVMThreadContext *tc, MVMObject *type, MVMString *val);

#define MVM_repr_at_key_int(tc, obj, key) \
    MVM_repr_get_int((tc), MVM_repr_at_key_boxed((tc), (obj), (key)))
#define MVM_repr_at_key_num(tc, obj, key) \
    MVM_repr_get_num((tc), MVM_repr_at_key_boxed((tc), (obj), (key)))
#define MVM_repr_at_key_str(tc, obj, key) \
    MVM_repr_get_str((tc), MVM_repr_at_key_boxed((tc), (obj), (key)))

#define MVM_repr_bind_key_int(tc, obj, key, val) \
    MVM_repr_bind_key_boxed((tc), (obj), (key), MVM_repr_box_int((tc), (*((tc)->interp_cu))->body.hll_config->int_box_type, (val)))
#define MVM_repr_bind_key_num(tc, obj, key, val) \
    MVM_repr_bind_key_boxed((tc), (obj), (key), MVM_repr_box_num((tc), (*((tc)->interp_cu))->body.hll_config->num_box_type, (val)))
#define MVM_repr_bind_key_str(tc, obj, key, val) \
    MVM_repr_bind_key_boxed((tc), (obj), (key), MVM_repr_box_str((tc), (*((tc)->interp_cu))->body.hll_config->str_box_type, (val)))
