/* Bytecode annotation, post-resolution. */
struct MVMBytecodeAnnotation {
    MVMuint32 bytecode_offset;
    MVMuint32 filename_string_heap_index;
    MVMuint32 line_number;
    MVMuint32 ann_offset;
    MVMuint32 ann_index;
};

void MVM_bytecode_unpack(MVMThreadContext *tc, MVMCompUnit *cu);
MVMBytecodeAnnotation * MVM_bytecode_resolve_annotation(MVMThreadContext *tc, MVMStaticFrameBody *sfb, MVMuint32 offset);
void MVM_bytecode_advance_annotation(MVMThreadContext *tc, MVMStaticFrameBody *sfb, MVMBytecodeAnnotation *ba);
void MVM_bytecode_finish_frame(MVMThreadContext *tc, MVMCompUnit *cu, MVMStaticFrame *sf, MVMint32 dump_only);
MVMuint8 MVM_bytecode_find_static_lexical_scref(MVMThreadContext *tc, MVMCompUnit *cu, MVMStaticFrame *sf, MVMuint16 index, MVMuint32 *sc, MVMuint32 *id);
const MVMOpInfo * MVM_bytecode_get_validated_op_info(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint16 opcode);
