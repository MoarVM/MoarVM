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

void MVM_jit_log_bytecode(MVMThreadContext *tc, MVMJitCode *code) {
    /* Filename format: moar-jit-%d.bin. number can consume at most 10
     * bytes, moar-jit-.bin is 13 bytes, one byte for the zero at the
     * end, one byte for the directory separator is 25 bytes, plus the
     * length of the bytecode directory itself */
    char * filename = MVM_malloc(strlen(tc->instance->jit_bytecode_dir) + 25);
    FILE * out;
    sprintf(filename, "%s/moar-jit-%04d.bin", tc->instance->jit_bytecode_dir, code->seq_nr);
    out = fopen(filename, "w");
    if (out) {
        fwrite(code->func_ptr, sizeof(char), code->size, out);
        fclose(out);
        if (tc->instance->jit_bytecode_map) {
            char *frame_name         = MVM_string_utf8_encode_C_string(tc, code->sf->body.name);
            char *frame_cuuid        = MVM_string_utf8_encode_C_string(tc, code->sf->body.cuuid);
            /* I'd like to add linenumber and filename information, but it's really a lot of work at this point */
            fprintf(tc->instance->jit_bytecode_map, "%s\t%s\t%s\n", filename, frame_name, frame_cuuid);
            MVM_free(frame_name);
            MVM_free(frame_cuuid);
        }
    } else {
        MVM_jit_log(tc, "ERROR: could dump bytecode in %s\n", filename);
    }
    MVM_free(filename);
}
