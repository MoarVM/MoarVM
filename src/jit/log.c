#include "moar.h"

/* inline this? maybe */
void MVM_jit_log(MVMThreadContext *tc, const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (tc->instance->jit_log_fh) {
        vfprintf(tc->instance->jit_log_fh, fmt, args);
    }
    va_end(args);
}

void MVM_jit_log_bytecode(MVMThreadContext *tc, void * code, size_t codesize) {
    size_t dirname_length = strlen(tc->instance->jit_bytecode_dir);
    char * filename = malloc(dirname_length + strlen("/jit-code.bin") + 1);
    strcpy(filename, tc->instance->jit_bytecode_dir);
    strcpy(filename + dirname_length, "/jit-code.bin");
    FILE * f = fopen(filename, "w");
    fwrite(code, sizeof(char), codesize, f);
    fclose(f);
}
