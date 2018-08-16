struct MVMIntConstCache {
    MVMObject *types[4];
    MVMObject *cache[4][16];
};

void MVM_intcache_for(MVMThreadContext *tc, MVMObject *type);
MVMObject *MVM_intcache_get(MVMThreadContext *tc, MVMObject *type, MVMint64 value);
MVMint32 MVM_intcache_type_index(MVMThreadContext *tc, MVMObject *type);
