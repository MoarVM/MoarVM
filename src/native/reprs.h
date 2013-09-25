#define MVM_REPR_ID_CVoid       (MVM_REPR_CORE_COUNT + 0)
#define MVM_REPR_ID_CChar       (MVM_REPR_CORE_COUNT + 1)
#define MVM_REPR_ID_CUChar      (MVM_REPR_CORE_COUNT + 2)
#define MVM_REPR_ID_CShort      (MVM_REPR_CORE_COUNT + 3)
#define MVM_REPR_ID_CUShort     (MVM_REPR_CORE_COUNT + 4)
#define MVM_REPR_ID_CInt        (MVM_REPR_CORE_COUNT + 5)
#define MVM_REPR_ID_CUInt       (MVM_REPR_CORE_COUNT + 6)
#define MVM_REPR_ID_CLong       (MVM_REPR_CORE_COUNT + 7)
#define MVM_REPR_ID_CULong      (MVM_REPR_CORE_COUNT + 8)
#define MVM_REPR_ID_CLLong      (MVM_REPR_CORE_COUNT + 9)
#define MVM_REPR_ID_CULLong     (MVM_REPR_CORE_COUNT + 10)
#define MVM_REPR_ID_CInt8       (MVM_REPR_CORE_COUNT + 11)
#define MVM_REPR_ID_CUInt8      (MVM_REPR_CORE_COUNT + 12)
#define MVM_REPR_ID_CInt16      (MVM_REPR_CORE_COUNT + 13)
#define MVM_REPR_ID_CUInt16     (MVM_REPR_CORE_COUNT + 14)
#define MVM_REPR_ID_CInt32      (MVM_REPR_CORE_COUNT + 15)
#define MVM_REPR_ID_CUInt32     (MVM_REPR_CORE_COUNT + 16)
#define MVM_REPR_ID_CInt64      (MVM_REPR_CORE_COUNT + 17)
#define MVM_REPR_ID_CUInt64     (MVM_REPR_CORE_COUNT + 18)
#define MVM_REPR_ID_CIntPtr     (MVM_REPR_CORE_COUNT + 19)
#define MVM_REPR_ID_CUIntPtr    (MVM_REPR_CORE_COUNT + 20)
#define MVM_REPR_ID_CIntMax     (MVM_REPR_CORE_COUNT + 21)
#define MVM_REPR_ID_CUIntMax    (MVM_REPR_CORE_COUNT + 22)
#define MVM_REPR_ID_CFloat      (MVM_REPR_CORE_COUNT + 23)
#define MVM_REPR_ID_CDouble     (MVM_REPR_CORE_COUNT + 24)
#define MVM_REPR_ID_CLDouble    (MVM_REPR_CORE_COUNT + 25)
#define MVM_REPR_ID_CPtr        (MVM_REPR_CORE_COUNT + 26)
#define MVM_REPR_ID_CFPtr       (MVM_REPR_CORE_COUNT + 27)
#define MVM_REPR_ID_CArray      (MVM_REPR_CORE_COUNT + 28)
#define MVM_REPR_ID_CStruct     (MVM_REPR_CORE_COUNT + 29)
#define MVM_REPR_ID_CUnion      (MVM_REPR_CORE_COUNT + 30)
#define MVM_REPR_ID_CFlexStruct (MVM_REPR_CORE_COUNT + 31)

#define MVM_REPR_NATIVE_COUNT   32

#define MVMCVoid_CAN_BOX        0
#define MVMCChar_CAN_BOX        MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCUChar_CAN_BOX       MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCShort_CAN_BOX       MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCUShort_CAN_BOX      MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCInt_CAN_BOX         MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCUInt_CAN_BOX        MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCLong_CAN_BOX        MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCULong_CAN_BOX       MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCLLong_CAN_BOX       MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCULLong_CAN_BOX      MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCInt8_CAN_BOX        MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCUInt8_CAN_BOX       MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCInt16_CAN_BOX       MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCUInt16_CAN_BOX      MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCInt32_CAN_BOX       MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCUInt32_CAN_BOX      MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCInt64_CAN_BOX       MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCUInt64_CAN_BOX      MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCIntPtr_CAN_BOX      MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCUIntPtr_CAN_BOX     MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCIntMax_CAN_BOX      MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCUIntMax_CAN_BOX     MVM_STORAGE_SPEC_CAN_BOX_INT
#define MVMCFloat_CAN_BOX       MVM_STORAGE_SPEC_CAN_BOX_NUM
#define MVMCDouble_CAN_BOX      MVM_STORAGE_SPEC_CAN_BOX_NUM
#define MVMCLDouble_CAN_BOX     MVM_STORAGE_SPEC_CAN_BOX_NUM
#define MVMCPtr_CAN_BOX         0
#define MVMCFPtr_CAN_BOX        0
#define MVMCArray_CAN_BOX       0
#define MVMCStruct_CAN_BOX      0
#define MVMCUnion_CAN_BOX       0
#define MVMCFlexStruct_CAN_BOX  0

const MVMREPROps * MVMCVoid_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCChar_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUChar_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCShort_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUShort_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCInt_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUInt_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCLong_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCULong_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCLLong_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCULLong_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCInt8_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUInt8_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCInt16_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUInt16_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCInt32_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUInt32_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCInt64_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUInt64_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCIntPtr_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUIntPtr_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCIntMax_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUIntMax_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCFloat_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCDouble_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCLDouble_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCPtr_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCFPtr_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCArray_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCStruct_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCUnion_initialize(MVMThreadContext *tc);
const MVMREPROps * MVMCFlexStruct_initialize(MVMThreadContext *tc);
