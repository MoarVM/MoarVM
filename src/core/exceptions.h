MVM_NO_RETURN void MVM_panic(MVMint32 exitCode, const char *messageFormat, ...) MVM_NO_RETURN_GCC;
MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_GCC;
