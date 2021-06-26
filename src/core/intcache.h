/* We play both kinds of music, Country *and* Western... */
MVM_STATIC_INLINE int MVM_intcache_possible_type_index(MVMObject *type) {
    MVMuint32 id = REPR(type)->ID;
    if (id == MVM_REPR_ID_P6int)
        return MVM_INTCACHE_P6INT_INDEX;
    if (id == MVM_REPR_ID_P6opaque)
        return MVM_INTCACHE_P6BIGINT_INDEX;
    return -1;
}

#define MVM_INTCACHE_RANGE_CHECK(value) ((value) >= -1 && (value) < 15)
#define MVM_INTCACHE_ZERO_OFFSET 1

void MVM_intcache_for(MVMThreadContext *tc, MVMObject *type);
MVMObject *MVM_intcache_get(MVMThreadContext *tc, MVMObject *type, MVMint64 value);
int MVM_intcache_type_index(MVMThreadContext *tc, MVMObject *type);
