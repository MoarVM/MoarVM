struct MVMIntConstCache {
    MVMObject *types[4];
    MVMObject *cache[4][16];

    MVMSTable *stables[4];
    MVMuint16  offsets[4];
};

void MVM_intcache_for(MVMThreadContext *tc, MVMObject *type);
