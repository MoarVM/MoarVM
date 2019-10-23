#include <moar.h>
#include <stdio.h>
#include <stdarg.h>

MVMuint8 _DEBUG_GLOBAL = 0;

void MVM_string_printh(MVMThreadContext *tc, MVMOSHandle *handle, MVMString *a) {
    MVMuint64 encoded_size;
    char *encoded;
    MVM_string_check_arg(tc, a, "print");
    encoded = MVM_string_utf8_encode(tc, a, &encoded_size, MVM_TRANSLATE_NEWLINE_OUTPUT);
    MVM_io_write_bytes_c(tc, tc->instance->stdout_handle, encoded, encoded_size);
    MVM_free(encoded);
}

void MVM_string_note(MVMThreadContext *tc, MVMString *a) {
    MVM_string_printh(tc, (MVMOSHandle *)tc->instance->stderr_handle,
        MVM_string_concatenate(tc, a, tc->instance->str_consts.platform_newline)
    );
}

void MVM_ascii_note(MVMThreadContext *tc, const char *note) {
    MVM_string_note(tc, MVM_string_ascii_decode_nt(tc, tc->instance->VMString, note));
}

/*
 * Returns MVM_malloc'ated string which must be MVM_free'd.
 */
char * MVM_ascii_vsprintf(const char *fmt, va_list args) {
    MVMuint32 buf_len = strlen(fmt) * 2;
    char * buf = NULL;
    do {
        MVMuint32 printed;
        buf = MVM_malloc(buf_len);
        printed = vsnprintf(buf, buf_len, fmt, args);
        if (printed >= buf_len) {
            buf_len = printed + 1;
            MVM_free(buf);
            buf = NULL;
        }
    } while (!buf);
    return buf;
}

MVMString * MVM_string_vsprintf(MVMThreadContext *tc, const char *fmt, va_list args) {
    char *buf = MVM_ascii_vsprintf(fmt, args);
    MVMString *str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, buf);
    MVM_free(buf);
    return str;
}

char * MVM_ascii_sprintf(MVMThreadContext *tc, const char *fmt, ...) {
    va_list args;
    char *str;
    va_start(args, fmt);
    str = MVM_ascii_vsprintf(fmt, args);
    va_end(args);
    return str;
}

MVMString * MVM_string_sprintf(MVMThreadContext *tc, const char *fmt, ...) {
    va_list args;
    char *buf;
    MVMString *str;
    va_start(args, fmt);
    buf = MVM_ascii_vsprintf(fmt, args);
    va_end(args);
    str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, buf);
    MVM_free(buf);
    return str;
}

MVMString *make_prefix(MVMThreadContext *tc, const char *prepend, MVMString *prefix) {
    MVMString *prepend_str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, prepend ? prepend : "| ");
    MVMString *empty = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "");
    return MVM_string_concatenate(tc, prepend_str, prefix ? prefix : empty);
}

MVMString *max_depth_str(MVMThreadContext *tc, MVMString *prefix) {
    return MVM_string_concatenate(tc, prefix, MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "..."));
}

#define MVM_PUSH_TO_LINES(FMT, ...) MVM_repr_push_s(tc, lines, MVM_string_concatenate(tc, prefix, MVM_string_sprintf(FMT, __VA_ARGS__)))
#define MVM_MAX_DEPTH(block) if (depth >= max_depth) { \
        MVM_PUSH_TO_LINES("%s", "..."); \
    } \
    else block
#define MVM_APPEND_TO_LINES(STR_LIST) MVMROOT(tc, STR_LIST, { \
        MVMuint64 elems = MVM_repr_elems(tc, STR_LIST); \
        MVMuint64 i; \
        for (i = 0; i < elems; i++) { \
            MVM_repr_push(tc, lines, MVM_repr_at_pos_s(tc, STR_LIST, i)); \
        } \
    });

MVMArray * MVM_stringify_MVMObject(MVMThreadContext *tc, MVMString *prefix, MVMObject *obj, MVMuint32 max_depth, MVMuint32 depth) {
    MVMArray *lines = (MVMArray *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStrArray);
    MVMROOT(tc, lines, {
        MVM_MAX_DEPTH({
            MVM_PUSH_TO_LINES("st\t:%p", obj->st);
            MVMArray *stable_lines = MVM_stringify_MVMStable(tc, make_prefix(tc, NULL, prefix), obj->st, max_depth, depth+1);
            MVM_APPEND_TO_LINES(stable_lines);
        });
    });
    return lines;
}

MVMArray * MVM_stringify_MVMStable(MVMThreadContext *tc, MVMString *prefix, MVMSTable *st, MVMuint32 max_depth, MVMuint32 depth) {
    MVMArray *lines = (MVMArray *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStrArray);
    MVMROOT(tc, lines, {
        MVM_MAX_DEPTH({
            MVM_PUSH_TO_LINES("%-20s: %p", "REPR_data", st->REPR_data);
            MVM_PUSH_TO_LINES("%-20s: %lu", "size", st->size);
            /* ... more fields are expected here ... */
            MVMArray *method_cache = MVM_stringify_MVMObject(tc, make_prefix(tc, NULL, prefix), st->method_cache, max_depth, depth + 1);
            MVM_APPEND_TO_LINES()
        });
    });
    return lines;
}

#if MVM_DEBUG
void MVM_debug_global(MVMuint8 on) {
    _DEBUG_GLOBAL = on ? 1 : 0;
}

void MVM_debug_thread(MVMThreadContext *tc, MVMuint8 on) {
    tc->debug = on ? 1 : 0;
}

void MVM_debug_printf(MVMThreadContext *tc, const char *env, const char *fmt, ...) {
    MVMuint8 env_debug = 0;
    if (env) {
        env_debug = getenv(env) ? 1 : 0;
    }
    if (_DEBUG_GLOBAL || tc->debug || env_debug) {
        va_list args;
        va_start(args, fmt);
        MVM_string_printh(tc, (MVMOSHandle *)tc->instance->stderr_handle, MVM_string_vsprintf(tc, fmt, args));
        va_end(args);
    }
}
#endif /* MVM_DEBUG */
