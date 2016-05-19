MVMCompUnit * MVM_cu_from_bytes(MVMThreadContext *tc, MVMuint8 *bytes, MVMuint32 size);
MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, const char *filename);
MVMCompUnit * MVM_cu_map_from_file_handle(MVMThreadContext *tc, uv_file fd, MVMuint64 pos);
MVMuint16 MVM_cu_callsite_add(MVMThreadContext *tc, MVMCompUnit *cu, MVMCallsite *cs);
MVMuint32 MVM_cu_string_add(MVMThreadContext *tc, MVMCompUnit *cu, MVMString *str);
MVMString * MVM_cu_obtain_string(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint32 idx);

MVM_STATIC_INLINE MVMString * MVM_cu_string(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint32 idx) {
    MVMString *s = cu->body.strings[idx];
    return s ? s : MVM_cu_obtain_string(tc, cu, idx);
}

MVM_STATIC_INLINE void MVM_cu_ensure_string_decoded(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint32 idx) {
    if (!cu->body.strings[idx])
        MVM_cu_obtain_string(tc, cu, idx);
}
