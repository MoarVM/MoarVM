struct MVMIntConstCache {
    MVMObject *types[4];
    MVMObject *cache[4][16];

    MVMSTable *stables[4];
    MVMuint16  offsets[4];
};

#define MVM_INTCACHE_RANGE_CHECK(value) ((value) >= -1 && (value) < 15)

void MVM_intcache_for(MVMThreadContext *tc, MVMObject *type);
MVMObject *MVM_intcache_get(MVMThreadContext *tc, MVMObject *type, MVMint64 value);
