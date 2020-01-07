MVMObject * MVM_spesh_try_get_how(MVMThreadContext *tc, MVMObject *obj);
MVMObject * MVM_spesh_try_find_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name);
MVMint64 MVM_spesh_try_can_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name);
MVMint8 MVM_spesh_get_reg_type(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMuint16 reg);
MVMint8 MVM_spesh_get_lex_type(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMuint16 outers, MVMuint16 idx);
MVMuint8 MVM_spesh_get_opr_type(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMSpeshIns *ins, MVMint32 i);
