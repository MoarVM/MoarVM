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
    MVMint32 dirname_len = strlen(tc->instance->jit_bytecode_dir);
    char seq_nr[20];
    MVMint32  seq_nr_len  = sprintf(seq_nr, "%d", code->seq_nr);
    char *filename = MVM_malloc(dirname_len + seq_nr_len + cuuid_len + name_len + 14);
    sprintf(filename, "%s/jit-%s-%s.%s.bin", tc->instance->jit_bytecode_dir,
            seq_nr, cuuid, name);
    MVM_free(cuuid);
    MVM_free(name);
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
