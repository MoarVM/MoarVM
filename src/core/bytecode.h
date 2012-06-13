void MVM_bytecode_unpack(MVMThreadContext *tc, MVMCompUnit *cu);

/* Steal endianness from APR */
#ifdef APR_IS_BIGENDIAN
#define MVM_BIGENDIAN           1
#endif
