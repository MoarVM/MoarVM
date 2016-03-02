MVMCompUnit * MVM_cu_from_bytes(MVMThreadContext *tc, MVMuint8 *bytes, MVMuint32 size);
MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, const char *filename);
MVMuint16 MVM_cu_callsite_add(MVMThreadContext *tc, MVMCompUnit *cu, MVMCallsite *cs);
MVMuint32 MVM_cu_string_add(MVMThreadContext *tc, MVMCompUnit *cu, MVMString *str);

MVM_STATIC_INLINE MVMString * MVM_cu_string(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint32 idx) {
    return cu->body.strings[idx];
}
