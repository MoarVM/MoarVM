void MVM_bytecode_unpack(MVMThreadContext *tc, MVMCompUnit *cu);
MVMBytecodeAnnotation * MVM_bytecode_resolve_annotation(MVMThreadContext *tc, MVMStaticFrameBody *sfb, MVMuint32 offset);

/* Steal endianness from APR */
#ifdef APR_IS_BIGENDIAN
 #if APR_IS_BIGENDIAN
  #define MVM_BIGENDIAN           1
 #endif
#endif
