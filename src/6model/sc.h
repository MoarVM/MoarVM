/* SC manipulation functions. */
MVMObject * MVM_sc_create(MVMThreadContext *tc, MVMString *handle);
struct _MVMString * MVM_sc_get_handle(MVMThreadContext *tc, MVMSerializationContext *sc);
struct _MVMString * MVM_sc_get_description(MVMThreadContext *tc, MVMSerializationContext *sc);
MVMint64 MVM_sc_find_object_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj);
MVMint64 MVM_sc_find_stable_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMSTable *st);
MVMint64 MVM_sc_find_code_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj);
MVMObject * MVM_sc_get_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
void MVM_sc_set_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *obj);
MVMSTable * MVM_sc_get_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
void MVM_sc_set_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMSTable *st);
MVMObject * MVM_sc_get_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx);
void MVM_sc_set_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *code);
