MVM_NO_RETURN void MVM_panic(MVMint32 exitCode, const char *messageFormat, ...) MVM_NO_RETURN_GCC;
MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_GCC;

#define MVM_exitcode_NYI            12
#define MVM_exitcode_compunit       13
#define MVM_exitcode_invalidopcode  14
#define MVM_exitcode_gcalloc        15
#define MVM_exitcode_gcroots        16
#define MVM_exitcode_gcnursery      17