void MVM_jit_log(MVMThreadContext *tc, const char *fmt, ...) MVM_FORMAT(printf, 2, 3);
void MVM_jit_log_bytecode(MVMThreadContext *tc, MVMJitCode *code);
