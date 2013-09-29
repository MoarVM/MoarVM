typedef struct {
    MVMContainerSpec basic;
    MVMint64 (*fetch_int)(MVMThreadContext *tc, MVMObject *cont);
    void (*store_int)(MVMThreadContext *tc, MVMObject *cont, MVMint64 value);
    MVMnum64 (*fetch_num)(MVMThreadContext *tc, MVMObject *cont);
    void (*store_num)(MVMThreadContext *tc, MVMObject *cont, MVMnum64 value);
    MVMString * (*fetch_str)(MVMThreadContext *tc, MVMObject *cont);
    void (*store_str)(MVMThreadContext *tc, MVMObject *cont, MVMString *value);
} MVMContainerSpecEx;

extern const MVMContainerConfigurer
    MVM_CONTAINER_CONF_CChar,
    MVM_CONTAINER_CONF_CDouble,
    MVM_CONTAINER_CONF_CFloat,
    MVM_CONTAINER_CONF_CFPtr,
    MVM_CONTAINER_CONF_CInt,
    MVM_CONTAINER_CONF_CInt16,
    MVM_CONTAINER_CONF_CInt32,
    MVM_CONTAINER_CONF_CInt64,
    MVM_CONTAINER_CONF_CInt8,
    MVM_CONTAINER_CONF_CIntMax,
    MVM_CONTAINER_CONF_CIntPtr,
    MVM_CONTAINER_CONF_CLDouble,
    MVM_CONTAINER_CONF_CLLong,
    MVM_CONTAINER_CONF_CLong,
    MVM_CONTAINER_CONF_CPtr,
    MVM_CONTAINER_CONF_CShort,
    MVM_CONTAINER_CONF_CUChar,
    MVM_CONTAINER_CONF_CUInt,
    MVM_CONTAINER_CONF_CUInt16,
    MVM_CONTAINER_CONF_CUInt32,
    MVM_CONTAINER_CONF_CUInt64,
    MVM_CONTAINER_CONF_CUInt8,
    MVM_CONTAINER_CONF_CUIntMax,
    MVM_CONTAINER_CONF_CUIntPtr,
    MVM_CONTAINER_CONF_CULLong,
    MVM_CONTAINER_CONF_CULong,
    MVM_CONTAINER_CONF_CUShort,
    MVM_CONTAINER_CONF_CVoid;
