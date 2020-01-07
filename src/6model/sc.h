/* SC manipulation functions. */
MVMObject * MVM_sc_create(MVMThreadContext *tc, MVMString *handle);
void MVM_sc_add_all_scs_entry(MVMThreadContext *tc, MVMSerializationContextBody *scb);
MVMString * MVM_sc_get_handle(MVMThreadContext *tc, MVMSerializationContext *sc);
MVMString * MVM_sc_get_description(MVMThreadContext *tc, MVMSerializationContext *sc);
void MVM_sc_set_description(MVMThreadContext *tc, MVMSerializationContext *sc, MVMString *desc);
MVMint64 MVM_sc_find_object_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj);
MVMint64 MVM_sc_find_object_idx_jit(MVMThreadContext *tc, MVMObject *sc, MVMObject *obj);
MVMint64 MVM_sc_find_stable_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMSTable *st);
MVMint64 MVM_sc_find_code_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj);
MVMuint8 MVM_sc_is_object_immediately_available(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
MVMObject * MVM_sc_get_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
MVMObject * MVM_sc_try_get_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
void MVM_sc_set_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *obj);
void MVM_sc_set_object_no_update(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *obj);
MVMSTable * MVM_sc_get_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
MVMSTable * MVM_sc_try_get_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
void MVM_sc_set_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMSTable *st);
void MVM_sc_push_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMSTable *st);
MVMObject * MVM_sc_get_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
MVMSerializationContext * MVM_sc_find_by_handle(MVMThreadContext *tc, MVMString *handle);
MVMSerializationContext * MVM_sc_get_sc_slow(MVMThreadContext *tc, MVMCompUnit *cu, MVMint16 dep);
MVM_STATIC_INLINE MVMSerializationContext * MVM_sc_get_sc(MVMThreadContext *tc,
                                                          MVMCompUnit *cu, MVMint16 dep) {
    MVMSerializationContext *sc = cu->body.scs[dep];
    return sc ? sc : MVM_sc_get_sc_slow(tc, cu, dep);
}
MVM_STATIC_INLINE MVMObject * MVM_sc_get_sc_object(MVMThreadContext *tc, MVMCompUnit *cu,
                                 MVMuint16 dep, MVMuint64 idx) {
    MVMSerializationContext *sc = MVM_sc_get_sc(tc, cu, dep);
    if (MVM_UNLIKELY(sc == NULL))
        MVM_exception_throw_adhoc(tc, "SC not yet resolved; lookup failed");
    return MVM_sc_get_object(tc, sc, idx);
}
void MVM_sc_disclaim(MVMThreadContext *tc, MVMSerializationContext *sc);

MVM_STATIC_INLINE MVMuint32 MVM_sc_get_idx_of_sc(MVMCollectable *col) {
    assert(!(col->flags & MVM_CF_FORWARDER_VALID));
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
    if (col->flags & MVM_CF_SERIALZATION_INDEX_ALLOCATED)
        return col->sc_forward_u.sci->sc_idx;
#endif
    return col->sc_forward_u.sc.sc_idx;
}

MVM_STATIC_INLINE MVMuint32 MVM_sc_get_idx_in_sc(MVMCollectable *col) {
    assert(!(col->flags & MVM_CF_FORWARDER_VALID));
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
    if (col->flags & MVM_CF_SERIALZATION_INDEX_ALLOCATED)
        return col->sc_forward_u.sci->idx;
    if (col->sc_forward_u.sc.idx == MVM_DIRECT_SC_IDX_SENTINEL)
        return ~0;
#endif
    return col->sc_forward_u.sc.idx;
}

MVM_STATIC_INLINE void MVM_sc_set_idx_in_sc(MVMCollectable *col, MVMuint32 i) {
    assert(!(col->flags & MVM_CF_FORWARDER_VALID));
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
    if (col->flags & MVM_CF_SERIALZATION_INDEX_ALLOCATED) {
        col->sc_forward_u.sci->idx = i;
    } else if (i >= MVM_DIRECT_SC_IDX_SENTINEL) {
        struct MVMSerializationIndex *const sci
            = MVM_malloc(sizeof(struct MVMSerializationIndex));
        sci->sc_idx = col->sc_forward_u.sc.sc_idx;
        sci->idx = i;
        col->sc_forward_u.sci = sci;
        col->flags |= MVM_CF_SERIALZATION_INDEX_ALLOCATED;
    } else
#endif
    {
        col->sc_forward_u.sc.idx = i;
    }
}

/* Gets a collectable's SC. */
MVM_STATIC_INLINE MVMSerializationContext * MVM_sc_get_collectable_sc(MVMThreadContext *tc, MVMCollectable *col) {
    MVMuint32 sc_idx;
    assert(!(col->flags & MVM_CF_FORWARDER_VALID));
    sc_idx = MVM_sc_get_idx_of_sc(col);
    assert(sc_idx != ~(MVMuint32)0);
    return sc_idx > 0 ? tc->instance->all_scs[sc_idx]->sc : NULL;
}

/* Gets an object's SC. */
MVM_STATIC_INLINE MVMSerializationContext * MVM_sc_get_obj_sc(MVMThreadContext *tc, MVMObject *obj) {
    return MVM_sc_get_collectable_sc(tc, &obj->header);
}

/* Gets a frame's SC. */
MVM_STATIC_INLINE MVMSerializationContext * MVM_sc_get_frame_sc(MVMThreadContext *tc, MVMFrame *f) {
    return MVM_sc_get_collectable_sc(tc, &f->header);
}

/* Gets an STables's SC. */
MVM_STATIC_INLINE MVMSerializationContext * MVM_sc_get_stable_sc(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_sc_get_collectable_sc(tc, &st->header);
}

/* Sets a collectable's SC. */
MVM_STATIC_INLINE void MVM_sc_set_collectable_sc(MVMThreadContext *tc, MVMCollectable *col, MVMSerializationContext *sc) {
    assert(!(col->flags & MVM_CF_FORWARDER_VALID));
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
    if (col->flags & MVM_CF_SERIALZATION_INDEX_ALLOCATED) {
        col->sc_forward_u.sci->sc_idx = sc->body->sc_idx;
        col->sc_forward_u.sci->idx    = ~0;
    } else
#endif
    {
        col->sc_forward_u.sc.sc_idx = sc->body->sc_idx;
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
        if (col->sc_forward_u.sc.sc_idx != sc->body->sc_idx) {
            struct MVMSerializationIndex *const sci
                = MVM_malloc(sizeof(struct MVMSerializationIndex));
            sci->sc_idx = sc->body->sc_idx;
            sci->idx = ~0;
            col->sc_forward_u.sci = sci;
            col->flags |= MVM_CF_SERIALZATION_INDEX_ALLOCATED;
        } else
#endif
        {
            col->sc_forward_u.sc.idx    = MVM_DIRECT_SC_IDX_SENTINEL;
        }
    }
}

/* Sets an object's SC. */
MVM_STATIC_INLINE void MVM_sc_set_obj_sc(MVMThreadContext *tc, MVMObject *obj, MVMSerializationContext *sc) {
    MVM_sc_set_collectable_sc(tc, &obj->header, sc);
}

/* Sets an frame's SC. */
MVM_STATIC_INLINE void MVM_sc_set_frame_sc(MVMThreadContext *tc, MVMFrame *f, MVMSerializationContext *sc) {
    MVM_sc_set_collectable_sc(tc, &f->header, sc);
}

/* Sets an STable's SC. */
MVM_STATIC_INLINE void MVM_sc_set_stable_sc(MVMThreadContext *tc, MVMSTable *st, MVMSerializationContext *sc) {
    MVM_sc_set_collectable_sc(tc, &st->header, sc);
}

/* Given an SC, an index and a code ref, store it and the index. */
MVM_STATIC_INLINE void MVM_sc_set_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *code) {
    MVMObject *roots = sc->body->root_codes;
    MVM_repr_bind_pos_o(tc, roots, idx, code);
    if (MVM_sc_get_idx_of_sc(&code->header) == sc->body->sc_idx)
        MVM_sc_set_idx_in_sc(&code->header, idx);
}

/* Sets the full list of code refs. */
MVM_STATIC_INLINE void MVM_sc_set_code_list(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *code_list) {
    MVM_ASSIGN_REF(tc, &(sc->common.header), sc->body->root_codes, code_list);
}

/* Gets the number of objects in the SC. */
MVM_STATIC_INLINE MVMuint64 MVM_sc_get_object_count(MVMThreadContext *tc, MVMSerializationContext *sc) {
    return sc->body->num_objects;
}

/* Given an SC and an object, push it onto the SC. */
MVM_STATIC_INLINE void MVM_sc_push_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj) {
    MVMuint32 idx = sc->body->num_objects;
    MVM_sc_set_object(tc, sc, idx, obj);
    if (MVM_sc_get_idx_of_sc(&obj->header) == sc->body->sc_idx)
        MVM_sc_set_idx_in_sc(&obj->header, idx);
}

/* SC repossession write barriers. */
void MVM_sc_wb_hit_obj(MVMThreadContext *tc, MVMObject *obj);
void MVM_sc_wb_hit_st(MVMThreadContext *tc, MVMSTable *st);

void MVM_SC_WB_OBJ(MVMThreadContext *tc, MVMObject *obj);

MVM_STATIC_INLINE void MVM_SC_WB_ST(MVMThreadContext *tc, MVMSTable *st) {
    assert(!(st->header.flags & MVM_CF_FORWARDER_VALID));
    assert(MVM_sc_get_idx_of_sc(&st->header) != ~(MVMuint32)0);
    if (MVM_sc_get_idx_of_sc(&st->header) > 0)
        MVM_sc_wb_hit_st(tc, st);
}
