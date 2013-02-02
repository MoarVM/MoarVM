MVMint64 MVM_repr_at_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVMnum64 MVM_repr_at_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVMString * MVM_repr_at_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);
MVMObject * MVM_repr_at_pos_o(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx);

void MVM_repr_push_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 pushee);
void MVM_repr_push_n(MVMThreadContext *tc, MVMObject *obj, MVMnum64 pushee);
void MVM_repr_push_s(MVMThreadContext *tc, MVMObject *obj, MVMString *pushee);
void MVM_repr_push_o(MVMThreadContext *tc, MVMObject *obj, MVMObject *pushee);

MVMint64 MVM_repr_get_int(MVMThreadContext *tc, MVMObject *obj);
MVMnum64 MVM_repr_get_num(MVMThreadContext *tc, MVMObject *obj);
MVMString * MVM_repr_get_str(MVMThreadContext *tc, MVMObject *obj);
