MVMObject * MVM_native_bloballoc(MVMThreadContext *tc, MVMuint64 size);

MVMObject * MVM_native_ptrcast(MVMThreadContext *tc, MVMObject *src,
        MVMObject *type, MVMint64 offset);

MVMuint64 MVM_native_csizeof(MVMThreadContext *tc, MVMObject *obj);

MVMuint64 MVM_native_calignof(MVMThreadContext *tc, MVMObject *obj);

MVMuint64 MVM_native_coffsetof(MVMThreadContext *tc, MVMObject *obj,
        MVMString *member);
