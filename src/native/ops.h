MVMObject * MVM_native_bloballoc(MVMThreadContext *tc, MVMuint64 size);

MVMObject * MVM_native_ptrcast(MVMThreadContext *tc, MVMObject *src,
        MVMObject *type, MVMint64 offset);
