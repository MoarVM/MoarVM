/* SC manipulation functions. */
MVMObject * MVM_sc_create(MVMThreadContext *tc, MVMString *handle);
MVMString * MVM_sc_get_handle(MVMThreadContext *tc, MVMSerializationContext *sc);
MVMString * MVM_sc_get_description(MVMThreadContext *tc, MVMSerializationContext *sc);
void MVM_sc_set_description(MVMThreadContext *tc, MVMSerializationContext *sc, MVMString *desc);
MVMint64 MVM_sc_find_object_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj);
MVMint64 MVM_sc_find_stable_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMSTable *st);
MVMint64 MVM_sc_find_code_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj);
MVMObject * MVM_sc_get_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
MVMObject * MVM_sc_try_get_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
void MVM_sc_set_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *obj);
void MVM_sc_push_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj);
MVMSTable * MVM_sc_get_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
MVMSTable * MVM_sc_try_get_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
void MVM_sc_set_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMSTable *st);
void MVM_sc_push_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMSTable *st);
MVMObject * MVM_sc_get_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
void MVM_sc_set_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *code);
void MVM_sc_set_code_list(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *code_list);
MVMuint64 MVM_sc_get_object_count(MVMThreadContext *tc, MVMSerializationContext *sc);
MVMSerializationContext * MVM_sc_find_by_handle(MVMThreadContext *tc, MVMString *handle);
MVMSerializationContext * MVM_sc_get_sc(MVMThreadContext *tc, MVMCompUnit *cu, MVMint16 dep);


/* Gets an object's SC. */
MVM_STATIC_INLINE MVMSerializationContext * MVM_sc_get_obj_sc(MVMThreadContext *tc, MVMObject *obj) {
    assert(!(obj->header.flags & MVM_CF_GEN2_LIVE));
    assert(!(obj->header.flags & MVM_CF_FORWARDER_VALID));
    return obj->header.sc_forward_u.sc;
}

/* Gets an STables's SC. */
MVM_STATIC_INLINE MVMSerializationContext * MVM_sc_get_stable_sc(MVMThreadContext *tc, MVMSTable *st) {
    assert(!(st->header.flags & MVM_CF_GEN2_LIVE));
    assert(!(st->header.flags & MVM_CF_FORWARDER_VALID));
    return st->header.sc_forward_u.sc;
}

/* Sets an object's SC. */
MVM_STATIC_INLINE void MVM_sc_set_obj_sc(MVMThreadContext *tc, MVMObject *obj, MVMSerializationContext *sc) {
    assert(!(obj->header.flags & MVM_CF_GEN2_LIVE));
    assert(!(obj->header.flags & MVM_CF_FORWARDER_VALID));
    MVM_ASSIGN_REF(tc, obj, obj->header.sc_forward_u.sc, sc);
}

/* Sets an STable's SC. */
MVM_STATIC_INLINE void MVM_sc_set_stable_sc(MVMThreadContext *tc, MVMSTable *st, MVMSerializationContext *sc) {
    assert(!(st->header.flags & MVM_CF_GEN2_LIVE));
    assert(!(st->header.flags & MVM_CF_FORWARDER_VALID));
    MVM_ASSIGN_REF(tc, st, st->header.sc_forward_u.sc, sc);
}


/* SC repossession write barriers. */
#define MVM_SC_WB_OBJ(tc, obj) \
    do { \
        MVMObject *check = (MVMObject *)obj; \
        assert(!(obj->header.flags & MVM_CF_GEN2_LIVE)); \
        assert(!(obj->header.flags & MVM_CF_FORWARDER_VALID)); \
        if (check->header.sc_forward_u.sc) \
            MVM_sc_wb_hit_obj(tc, check); \
    } while (0);
void MVM_sc_wb_hit_obj(MVMThreadContext *tc, MVMObject *obj);
#define MVM_SC_WB_ST(tc, st) \
    do { \
        MVMSTable *check = st; \
        assert(!(st->header.flags & MVM_CF_GEN2_LIVE)); \
        assert(!(st->header.flags & MVM_CF_FORWARDER_VALID)); \
        if (check->header.sc_forward_u.sc) \
            MVM_sc_wb_hit_st(tc, check); \
    } while (0);
void MVM_sc_wb_hit_st(MVMThreadContext *tc, MVMSTable *st);
