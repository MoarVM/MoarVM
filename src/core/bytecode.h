/* Bytecode annotation, post-resolution. */
struct MVMBytecodeAnnotation {
    MVMuint32 bytecode_offset;
    MVMuint32 filename_string_heap_index;
    MVMuint32 line_number;
};

void MVM_bytecode_unpack(MVMThreadContext *tc, MVMCompUnit *cu);
MVMBytecodeAnnotation * MVM_bytecode_resolve_annotation(MVMThreadContext *tc, MVMStaticFrameBody *sfb, MVMuint32 offset);
void MVM_bytecode_finish_frame(MVMThreadContext *tc, MVMCompUnit *cu, MVMStaticFrame *sf, MVMint32 dump_only);
MVMuint8 MVM_bytecode_find_static_lexical_scref(MVMThreadContext *tc, MVMCompUnit *cu, MVMStaticFrame *sf, MVMuint16 index, MVMint32 *sc, MVMint32 *id);
