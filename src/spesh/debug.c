#include "moar.h"
#include <stdarg.h>

MVM_THREAD_LOCAL char spesh_debug_buffer[2048];

#if MVM_USE_ZSTD
#include "zstd.h"

MVM_THREAD_LOCAL char spesh_debug_cmpbuffer[2048];
MVM_THREAD_LOCAL MVMuint64 uncompressed_tell = 0;
#endif

void MVM_spesh_debug_printf(MVMThreadContext *tc, const char *format, ...) {
    va_list list;
    va_start(list, format);
    vsnprintf(spesh_debug_buffer, 2048, format, list);
    va_end(list);

    MVM_spesh_debug_puts(tc, spesh_debug_buffer);
}

void MVM_spesh_debug_puts(MVMThreadContext *tc, char *text) {
    if (tc->instance->spesh_log_zstd &&
#if MVM_USE_ZSTD
        1
#else
        0
#endif
    ) {
#if MVM_USE_ZSTD
        size_t compressed_size = 0;

        size_t data_amount = strlen(text);

        size_t compress_bound = ZSTD_COMPRESSBOUND(data_amount);

        char *target = spesh_debug_cmpbuffer;

        if (compress_bound >= 2048) {
            target = MVM_malloc(compress_bound);
        }

        compressed_size = ZSTD_compress(
            target,
            compress_bound,
            text,
            data_amount,
            4);
        if (ZSTD_isError(compressed_size)) {
            MVM_oops(tc, "couldn't compress piece of spesh log for output: %s", ZSTD_getErrorName(compressed_size));
        }

        fwrite(target, sizeof(char), compressed_size, tc->instance->spesh_log_fh);

        if (compress_bound >= 2048) {
            MVM_free(target);
        }
#endif
    }
    else {
        fputs(text, tc->instance->spesh_log_fh);
    }
}

size_t MVM_spesh_debug_tell(MVMThreadContext *tc) {
    return ftell(tc->instance->spesh_log_fh);
}

void MVM_spesh_debug_flush(MVMThreadContext *tc) {
    fflush(tc->instance->spesh_log_fh);
}
