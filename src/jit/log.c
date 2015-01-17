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

static char * jitcode_name(MVMThreadContext *tc, MVMJitCode *code) {
    MVMuint64 cuuid_len;
    MVMuint64 name_len;
    char *cuuid = MVM_string_ascii_encode(tc, code->sf->body.cuuid,
                                              &cuuid_len);
    char *name  = MVM_string_ascii_encode(tc, code->sf->body.name,
                                              &name_len);
    MVMuint64 dirname_len = strlen(tc->instance->jit_bytecode_dir);
    // 4 chars for prefix, 3 chars for the separators, 4 for the postfix, 1 for the 0
    char *filename = MVM_malloc(dirname_len + name_len + cuuid_len + 12);
    char *dst = filename;
    memcpy(dst, tc->instance->jit_bytecode_dir, dirname_len);
    dst[dirname_len] = '/';
    dst += dirname_len + 1;
    memcpy(dst, "jit-", 4);
    dst += 4;
    memcpy(dst, cuuid, cuuid_len);
    dst[cuuid_len] = '.';
    dst += cuuid_len + 1;
    memcpy(dst, name, name_len);
    dst += name_len;
    memcpy(dst, ".bin", 5);
    MVM_free(name);
    MVM_free(cuuid);
    return filename;
}

void MVM_jit_log_bytecode(MVMThreadContext *tc, MVMJitCode *code) {
    char * filename = jitcode_name(tc, code);
    FILE * f = fopen(filename, "w");
    if (f) {
        fwrite(code->func_ptr, sizeof(char), code->size, f);
        fclose(f);
        MVM_jit_log(tc, "Dump bytecode in %s\n", filename);

    } else {
        MVM_jit_log(tc, "Could not dump bytecode in %s\n", filename);
    }
    MVM_free(filename);
}
