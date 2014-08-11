#include "moar.h"
#include "platform/time.h"
#include "tinymt64.h"

#include <math.h>

/* concatenating with "" ensures that only literal strings are accepted as argument. */
#define STR_WITH_LEN(str)  ("" str ""), (sizeof(str) - 1)

/* MSVC compilers know about environ,
 * see http://msdn.microsoft.com/en-us//library/vstudio/stxk41x1.aspx */
#ifndef _WIN32
#include <unistd.h>
#  ifdef __APPLE_CC__
#    include <crt_externs.h>
#    define environ (*_NSGetEnviron())
#  else
extern char **environ;
#  endif
#else
#  include <process.h>
#endif

#ifdef _WIN32
static wchar_t * ANSIToUnicode(MVMuint16 acp, const char *str)
{
     const int          len = MultiByteToWideChar(acp, 0, str, -1, NULL, 0);
     wchar_t * const result = (wchar_t *)malloc(len * sizeof(wchar_t));

     MultiByteToWideChar(acp, 0, str, -1, (LPWSTR)result, len);

     return result;
}

static char * UnicodeToUTF8(const wchar_t *str)
{
     const int       len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
     char * const result = (char *)malloc(len * sizeof(char));

     WideCharToMultiByte(CP_UTF8, 0, str, -1, result, len, NULL, NULL);

     return result;
}

static char * ANSIToUTF8(MVMuint16 acp, const char * str)
{
    wchar_t * const wstr = ANSIToUnicode(acp, str);
    char  * const result = UnicodeToUTF8(wstr);

    free(wstr);
    return result;
}

#endif

MVMObject * MVM_proc_getenvhash(MVMThreadContext *tc) {
    MVMInstance * const instance = tc->instance;
    MVMObject   *       env_hash;

#ifdef _WIN32
    const MVMuint16 acp = GetACP(); /* We should get ACP at runtime. */
#endif
    MVMuint32     pos = 0;
    MVMString *needle = MVM_string_ascii_decode(tc, instance->VMString, STR_WITH_LEN("="));
    char      *env;

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&needle);

    env_hash = MVM_repr_alloc_init(tc,  MVM_hll_current(tc)->slurpy_hash_type);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&env_hash);

    while ((env = environ[pos++]) != NULL) {
#ifndef _WIN32
        MVMString    *str = MVM_string_utf8_decode(tc, instance->VMString, env, strlen(env));
#else
        char * const _env = ANSIToUTF8(acp, env);
        MVMString    *str = MVM_string_utf8_decode(tc, instance->VMString, _env, strlen(_env));
#endif

        MVMuint32 index = MVM_string_index(tc, str, needle, 0);

        MVMString *key, *val;

#ifdef _WIN32
        free(_env);
#endif
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&str);

        key  = MVM_string_substring(tc, str, 0, index);
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&key);

        val  = MVM_string_substring(tc, str, index + 1, -1);
        MVM_repr_bind_key_o(tc, env_hash, key,
            MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type, val));

        MVM_gc_root_temp_pop_n(tc, 2);
    }

    MVM_gc_root_temp_pop_n(tc, 2);

    return env_hash;
}

#define INIT_ENV() do { \
    MVMROOT(tc, iter, { \
        MVMString * const equal = MVM_string_ascii_decode(tc, tc->instance->VMString, STR_WITH_LEN("=")); \
        MVMROOT(tc, equal, { \
            MVMString *env_str = NULL; \
            MVMObject *iterval = NULL; \
            i = 0; \
            while(MVM_iter_istrue(tc, iter)) { \
                MVM_repr_shift_o(tc, (MVMObject *)iter); \
                env_str = MVM_string_concatenate(tc, MVM_iterkey_s(tc, iter), equal); \
                iterval = MVM_iterval(tc, iter); \
                env_str = MVM_string_concatenate(tc, env_str, MVM_repr_get_str(tc, iterval)); \
                _env[i++] = MVM_string_utf8_encode_C_string(tc, env_str); \
            } \
            _env[size] = NULL; \
        }); \
    }); \
} while (0)

#define FREE_ENV() do { \
    i = 0;  \
    while(_env[i]) \
        free(_env[i++]); \
    free(_env); \
} while (0)

#define SPAWN(shell) do { \
    process->data               = &result; \
    process_stdio[0].flags      = UV_INHERIT_FD; \
    process_stdio[0].data.fd    = 0; \
    process_stdio[1].flags      = UV_INHERIT_FD; \
    process_stdio[1].data.fd    = 1; \
    process_stdio[2].flags      = UV_INHERIT_FD; \
    process_stdio[2].data.fd    = 2; \
    process_options.stdio       = process_stdio; \
    process_options.file        = shell; \
    process_options.args        = args; \
    process_options.cwd         = _cwd; \
    process_options.flags       = UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS | UV_PROCESS_WINDOWS_HIDE; \
    process_options.env         = _env; \
    process_options.stdio_count = 3; \
    process_options.exit_cb     = spawn_on_exit; \
    uv_ref((uv_handle_t *)process); \
    spawn_result = uv_spawn(tc->loop, process, &process_options); \
    if (spawn_result) \
        result = spawn_result; \
    else \
        uv_run(tc->loop, UV_RUN_DEFAULT); \
} while (0)

static void spawn_on_exit(uv_process_t *req, MVMint64 exit_status, int term_signal) {
    if (req->data)
        *(MVMint64 *)req->data = (exit_status << 8) | term_signal;
    uv_unref((uv_handle_t *)req);
    uv_close((uv_handle_t *)req, NULL);
}

MVMObject * MVM_file_openpipe(MVMThreadContext *tc, MVMString *cmd, MVMString *cwd, MVMObject *env, MVMString *err_path) {
    MVMint64 spawn_result;
    uv_process_t *process = calloc(1, sizeof(uv_process_t));
    uv_process_options_t process_options = {0};
    uv_stdio_container_t process_stdio[3];
    int i;
    int status;
    int readable = 1;
    uv_pipe_t *out, *in;

    char * const cmdin = MVM_string_utf8_encode_C_string(tc, cmd);
    char * const _cwd = MVM_string_utf8_encode_C_string(tc, cwd);
    const MVMuint64 size = MVM_repr_elems(tc, env);
    MVMIter * const iter = (MVMIter *)MVM_iter(tc, env);
    char **_env = malloc((size + 1) * sizeof(char *));

#ifdef _WIN32
    const MVMuint16 acp = GetACP(); /* We should get ACP at runtime. */
    char * const _cmd = ANSIToUTF8(acp, getenv("ComSpec"));
    char *args[3];
    args[0] = "/c";
    {
        MVMint64 len = strlen(cmdin);
        MVMint64 i;
        for (i = 0; i < len; i++)
            if (cmdin[i] == '/')
                cmdin[i] = '\\';
    }
    args[1] = cmdin;
    args[2] = NULL;
#else
    char * const _cmd = "/bin/sh";
    char *args[4];
    args[0] = "/bin/sh";
    args[1] = "-c";
    args[2] = cmdin;
    args[3] = NULL;
#endif

    INIT_ENV();
    /* Making openpipe distinguish between :rp and :wp and all other options
     * is left as an excercise for the reader.
    readable = strncmp(cmdin, "/usr/bin/wc", 11) != 0; */

    if (readable) {
        /* We want to read from the child's stdout. */
        out = malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, out, 0);
        uv_pipe_open(out, 0);
        process_stdio[0].flags       = UV_INHERIT_FD; // child's stdin
        process_stdio[0].data.fd     = 0;
        process_stdio[1].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE; // child's stdout
        process_stdio[1].data.stream = (uv_stream_t*)out;
    }
    else {
        /* We want to print to the child's stdin. */
        in  = malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, in, 0);
        uv_pipe_open(in, 1);
        process_stdio[0].flags       = UV_CREATE_PIPE | UV_READABLE_PIPE; // child's stdin
        process_stdio[0].data.stream = (uv_stream_t*)in;
        process_stdio[1].flags       = UV_INHERIT_FD; // child's stdout
        process_stdio[1].data.fd     = 1;
    }
    process_stdio[2].flags      = UV_INHERIT_FD; // child's stderr
    process_stdio[2].data.fd    = 2;
    process_options.stdio       = process_stdio;
    process_options.file        = _cmd;
    process_options.args        = args;
    process_options.cwd         = _cwd;
    process_options.flags       = UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS | UV_PROCESS_WINDOWS_HIDE;
    process_options.env         = _env;
    process_options.stdio_count = 3;
    process_options.exit_cb     = spawn_on_exit;
    uv_ref((uv_handle_t *)process);
    spawn_result = uv_spawn(tc->loop, process, &process_options);
    if (spawn_result) {
        FREE_ENV();
        free(_cwd);
        free(cmdin);
        uv_unref((uv_handle_t *)process);
        MVM_exception_throw_adhoc(tc, "Failed to open pipe: %d", errno);
    }

    FREE_ENV();
    free(_cwd);
    free(cmdin);
    uv_unref((uv_handle_t *)process);

    return MVM_io_syncpipe(tc, (uv_stream_t *)(readable ? out : in), process);
}

MVMint64 MVM_proc_shell(MVMThreadContext *tc, MVMString *cmd, MVMString *cwd, MVMObject *env) {
    MVMint64 result = 0, spawn_result;
    uv_process_t *process = calloc(1, sizeof(uv_process_t));
    uv_process_options_t process_options = {0};
    uv_stdio_container_t process_stdio[3];
    int i;

    char * const cmdin = MVM_string_utf8_encode_C_string(tc, cmd);
    char * const _cwd = MVM_string_utf8_encode_C_string(tc, cwd);
    const MVMuint64 size = MVM_repr_elems(tc, env);
    MVMIter * const iter = (MVMIter *)MVM_iter(tc, env);
    char **_env = malloc((size + 1) * sizeof(char *));

#ifdef _WIN32
    const MVMuint16 acp = GetACP(); /* We should get ACP at runtime. */
    char * const _cmd = ANSIToUTF8(acp, getenv("ComSpec"));
    char *args[3];
    args[0] = "/c";
    {
        MVMint64 len = strlen(cmdin);
        MVMint64 i;
        for (i = 0; i < len; i++) {
            if (cmdin[i] == ' ')
                break;
            if (cmdin[i] == '/')
                cmdin[i] = '\\';
        }
    }
    args[1] = cmdin;
    args[2] = NULL;
#else
    char * const _cmd = "/bin/sh";
    char *args[4];
    args[0] = "/bin/sh";
    args[1] = "-c";
    args[2] = cmdin;
    args[3] = NULL;
#endif

    INIT_ENV();
    SPAWN(_cmd);
    FREE_ENV();

    free(_cwd);

#ifdef _WIN32
    free(_cmd);
#endif

    free(cmdin);
    return result;
}

MVMint64 MVM_proc_spawn(MVMThreadContext *tc, MVMObject *argv, MVMString *cwd, MVMObject *env) {
    MVMint64 result = 0, spawn_result;
    uv_process_t *process = calloc(1, sizeof(uv_process_t));
    uv_process_options_t process_options = {0};
    uv_stdio_container_t process_stdio[3];
    int i;

    char   * const      _cwd = MVM_string_utf8_encode_C_string(tc, cwd);
    const MVMuint64     size = MVM_repr_elems(tc, env);
    MVMIter * const     iter = (MVMIter *)MVM_iter(tc, env);
    char              **_env = malloc((size + 1) * sizeof(char *));
    const MVMuint64  arg_size = MVM_repr_elems(tc, argv);
    char             **args = malloc((arg_size + 1) * sizeof(char *));
    MVMRegister        reg;

    i = 0;
    while(i < arg_size) {
        REPR(argv)->pos_funcs.at_pos(tc, STABLE(argv), argv, OBJECT_BODY(argv), i, &reg, MVM_reg_obj);
        args[i++] = MVM_string_utf8_encode_C_string(tc, MVM_repr_get_str(tc, reg.o));
    }
    args[arg_size] = NULL;

    INIT_ENV();
    SPAWN(arg_size ? args[0] : NULL);
    FREE_ENV();

    free(_cwd);

    i = 0;
    while(args[i])
        free(args[i++]);

    free(args);

    return result;
}

/* Data that we keep for an asynchronous process handle. */
typedef struct {
    /* The libuv handle to the process. */
    uv_process_t *handle;
} MVMIOAsyncProcessData;

/* IO ops table, for async process, populated with functions. */
static const MVMIOAsyncWritable proc_async_writable = { /*write_str*/NULL, /*write_bytes*/NULL };
static const MVMIOOps proc_op_table = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &proc_async_writable,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

/* Info we convey about an async spawn task. */
typedef struct {
    MVMThreadContext  *tc;
    int                work_idx;
    MVMObject         *handle;
    MVMObject         *callbacks;
    char              *prog;
    char              *cwd;
    char             **env;
    char             **args;
    MVMDecodeStream   *ds_stdout;
    MVMDecodeStream   *ds_stderr;
    MVMuint32          seq_stdout;
    MVMuint32          seq_stderr;
} SpawnInfo;

static void async_spawn_on_exit(uv_process_t *req, MVMint64 exit_status, int term_signal) {
    /* Check we've got a callback to fire. */
    SpawnInfo        *si  = (SpawnInfo *)req->data;
    MVMThreadContext *tc  = si->tc;
    MVMObject *done_cb = MVM_repr_at_key_o(tc, si->callbacks,
        tc->instance->str_consts.done);
    if (!MVM_is_null(tc, done_cb)) {
        MVMROOT(tc, done_cb, {
            /* Get status. */
            MVMint64 status = (exit_status << 8) | term_signal;

            /* Get what we'll need to build and convey the result. */
            MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask     *t   = (MVMAsyncTask *)MVM_repr_at_pos_o(tc,
                tc->instance->event_loop_active, si->work_idx);

            /* Box and send along status. */
            MVM_repr_push_o(tc, arr, done_cb);
            MVMROOT(tc, arr, {
            MVMROOT(tc, t, {
                MVMObject *result_box = MVM_repr_box_int(tc,
                    tc->instance->boot_types.BOOTInt, status);
                MVM_repr_push_o(tc, arr, result_box);
            });
            });
            MVM_repr_push_o(tc, t->body.queue, arr);
        });
    }

    /* Close handle. */
    uv_close((uv_handle_t *)req, NULL);
}

/* Allocates a buffer of the suggested size. */
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    size_t size = suggested_size > 0 ? suggested_size : 4;
    buf->base   = malloc(size);
    buf->len    = size;
}

/* Read functions for stdout/stderr. */
static void async_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf, SpawnInfo *si,
                       MVMObject *callback, MVMDecodeStream *ds, MVMuint32 seq_number) {
    MVMThreadContext *tc  = si->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = (MVMAsyncTask *)MVM_repr_at_pos_o(tc,
        tc->instance->event_loop_active, si->work_idx);
    MVM_repr_push_o(tc, arr, callback);
    if (nread > 0) {
        MVMROOT(tc, t, {
        MVMROOT(tc, arr, {
            /* Push the sequence number. */
            MVMObject *seq_boxed = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, seq_number);
            MVM_repr_push_o(tc, arr, seq_boxed);

            /* Either need to produce a buffer or decode characters. */
            if (ds) {
                MVMString *str;
                MVMObject *boxed_str;
                MVM_string_decodestream_add_bytes(tc, ds, buf->base, nread);
                str = MVM_string_decodestream_get_all(tc, ds);
                boxed_str = MVM_repr_box_str(tc, tc->instance->boot_types.BOOTStr, str);
                MVM_repr_push_o(tc, arr, boxed_str);
            }
            else {
                MVMObject *buf_type    = MVM_repr_at_key_o(tc, si->callbacks,
                                            tc->instance->str_consts.buf_type);
                MVMArray  *res_buf     = (MVMArray *)MVM_repr_alloc_init(tc, buf_type);
                res_buf->body.slots.i8 = buf->base;
                res_buf->body.start    = 0;
                res_buf->body.ssize    = nread;
                res_buf->body.elems    = nread;
                MVM_repr_push_o(tc, arr, (MVMObject *)res_buf);
            }

            /* Finally, no error. */
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        });
        });
    }
    else if (nread == UV_EOF) {
        MVMROOT(tc, t, {
        MVMROOT(tc, arr, {
            MVMObject *minus_one = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, -1);
            MVM_repr_push_o(tc, arr, minus_one);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        });
        });
        if (buf->base)
            free(buf->base);
        uv_read_stop(handle);
    }
    else {
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        MVMROOT(tc, t, {
        MVMROOT(tc, arr, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(nread));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        });
        });
        if (buf->base)
            free(buf->base);
        uv_read_stop(handle);
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
}
static void async_spawn_stdout_chars_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    SpawnInfo *si = (SpawnInfo *)handle->data;
    MVMObject *cb = MVM_repr_at_key_o(si->tc, si->callbacks,
        si->tc->instance->str_consts.stdout_chars);
    async_read(handle, nread, buf, si, cb, si->ds_stdout, si->seq_stdout++);
}
static void async_spawn_stdout_bytes_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    SpawnInfo *si = (SpawnInfo *)handle->data;
    MVMObject *cb = MVM_repr_at_key_o(si->tc, si->callbacks,
        si->tc->instance->str_consts.stdout_bytes);
    async_read(handle, nread, buf, si, cb, NULL, si->seq_stdout++);
}
static void async_spawn_stderr_chars_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    SpawnInfo *si = (SpawnInfo *)handle->data;
    MVMObject *cb = MVM_repr_at_key_o(si->tc, si->callbacks,
        si->tc->instance->str_consts.stderr_chars);
    async_read(handle, nread, buf, si, cb, si->ds_stderr, si->seq_stderr++);
}
static void async_spawn_stderr_bytes_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    SpawnInfo *si = (SpawnInfo *)handle->data;
    MVMObject *cb = MVM_repr_at_key_o(si->tc, si->callbacks,
        si->tc->instance->str_consts.stderr_bytes);
    async_read(handle, nread, buf, si, cb, NULL, si->seq_stderr++);
}

/* Actually spawns an async task. This runs in the event loop thread. */
static void spawn_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMint64 spawn_result;

    /* Process info setup. */
    uv_process_t *process = calloc(1, sizeof(uv_process_t));
    uv_process_options_t process_options = {0};
    uv_stdio_container_t process_stdio[3];
    uv_pipe_t *stdout_pipe = NULL;
    uv_pipe_t *stderr_pipe = NULL;
    uv_read_cb stdout_cb, stderr_cb;

    /* Add to work in progress. */
    SpawnInfo *si = (SpawnInfo *)data;
    si->tc        = tc;
    si->work_idx  = MVM_repr_elems(tc, tc->instance->event_loop_active);
    MVM_repr_push_o(tc, tc->instance->event_loop_active, async_task);

    /* TODO: support write */
    process_stdio[0].flags   = UV_INHERIT_FD;
    process_stdio[0].data.fd = 0;
    if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stdout_chars)) {
        uv_pipe_t *pipe = malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, pipe, 0);
        uv_pipe_open(pipe, 0);
        pipe->data = si;
        process_stdio[1].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
        process_stdio[1].data.stream = (uv_stream_t *)pipe;
        si->ds_stdout                = MVM_string_decodestream_create(tc, MVM_encoding_type_utf8, 0);
        stdout_pipe                  = pipe;
        stdout_cb                    = &async_spawn_stdout_chars_read;
    }
    else if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stdout_bytes)) {
        uv_pipe_t *pipe = malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, pipe, 0);
        uv_pipe_open(pipe, 0);
        pipe->data = si;
        process_stdio[1].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
        process_stdio[1].data.stream = (uv_stream_t *)pipe;
        stdout_pipe                  = pipe;
        stdout_cb                    = &async_spawn_stdout_bytes_read;
    }
    else {
        process_stdio[1].flags   = UV_INHERIT_FD;
        process_stdio[1].data.fd = 1;
    }
    if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stderr_chars)) {
        uv_pipe_t *pipe = malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, pipe, 0);
        uv_pipe_open(pipe, 0);
        pipe->data = si;
        process_stdio[2].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
        process_stdio[2].data.stream = (uv_stream_t *)pipe;
        si->ds_stderr                = MVM_string_decodestream_create(tc, MVM_encoding_type_utf8, 0);
        stderr_pipe                  = pipe;
        stderr_cb                    = &async_spawn_stderr_chars_read;
    }
    else if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stderr_bytes)) {
        uv_pipe_t *pipe = malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, pipe, 0);
        uv_pipe_open(pipe, 0);
        pipe->data = si;
        process_stdio[2].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
        process_stdio[2].data.stream = (uv_stream_t *)pipe;
        stderr_pipe                  = pipe;
        stderr_cb                    = &async_spawn_stderr_bytes_read;
    }
    else {
        process_stdio[2].flags   = UV_INHERIT_FD;
        process_stdio[2].data.fd = 2;
    }

    /* Set up process start info. */
    process_options.stdio       = process_stdio;
    process_options.file        = si->prog;
    process_options.args        = si->args;
    process_options.cwd         = si->cwd;
    process_options.flags       = UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS | UV_PROCESS_WINDOWS_HIDE;
    process_options.env         = si->env;
    process_options.stdio_count = 3;
    process_options.exit_cb     = async_spawn_on_exit;

    /* Attach data, spawn, report any error. */
    process->data = si;
    spawn_result  = uv_spawn(tc->loop, process, &process_options);
    if (spawn_result) {
        MVMObject *error_cb = MVM_repr_at_key_o(tc, si->callbacks,
            tc->instance->str_consts.error);
        if (!MVM_is_null(tc, error_cb)) {
            MVMROOT(tc, error_cb, {
            MVMROOT(tc, async_task, {
                MVMObject *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
                MVM_repr_push_o(tc, arr, error_cb);
                MVMROOT(tc, arr, {
                    MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                        tc->instance->VMString, uv_strerror(spawn_result));
                    MVMObject *msg_box = MVM_repr_box_str(tc,
                        tc->instance->boot_types.BOOTStr, msg_str);
                    MVM_repr_push_o(tc, arr, msg_box);
                });
                MVM_repr_push_o(tc, ((MVMAsyncTask *)async_task)->body.queue, arr);
            });
            });
        }
    }

    /* Start any output readers. */
    if (stdout_pipe)
        uv_read_start((uv_stream_t *)stdout_pipe, on_alloc, stdout_cb);
    if (stderr_pipe)
        uv_read_start((uv_stream_t *)stderr_pipe, on_alloc, stderr_cb);
}

/* Marks objects for a spawn task. */
void spawn_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    SpawnInfo *si = (SpawnInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &si->handle);
    MVM_gc_worklist_add(tc, worklist, &si->callbacks);
}

/* Frees info for a spawn task. */
static void spawn_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        SpawnInfo *si = (SpawnInfo *)data;
        if (si->cwd) {
            free(si->cwd);
            si->cwd = NULL;
        }
        if (si->env) {
            MVMuint32 i;
            char **_env = si->env;
            FREE_ENV();
            si->env = NULL;
        }
        if (si->args) {
            MVMuint32 i = 0;
            while (si->args[i])
                free(si->args[i++]);
            free(si->args);
            si->args = NULL;
        }
        if (si->ds_stdout) {
            MVM_string_decodestream_destory(tc, si->ds_stdout);
            si->ds_stdout = NULL;
        }
        if (si->ds_stderr) {
            MVM_string_decodestream_destory(tc, si->ds_stdout);
            si->ds_stderr = NULL;
        }
        free(si);
    }
}

/* Operations table for async connect task. */
static const MVMAsyncTaskOps spawn_op_table = {
    spawn_setup,
    NULL,
    spawn_gc_mark,
    spawn_gc_free
};

/* Spawn a process asynchronously. */
MVMObject * MVM_proc_spawn_async(MVMThreadContext *tc, MVMObject *queue, MVMObject *argv,
                                 MVMString *cwd, MVMObject *env, MVMObject *callbacks) {
    MVMAsyncTask  *task;
    MVMOSHandle   *handle;
    SpawnInfo     *si;
    char          *prog, *_cwd, **_env, **args;
    MVMuint64      size, arg_size, i;
    MVMIter       *iter;
    MVMRegister    reg;

    /* Validate queue REPR. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "spawnprocasync target queue must have ConcBlockingQueue REPR");

    /* Encode arguments, taking first as program name. */
    arg_size = MVM_repr_elems(tc, argv);
    if (arg_size < 1)
        MVM_exception_throw_adhoc(tc, "spawnprocasync must have first arg for program");
    args = malloc((arg_size + 1) * sizeof(char *));
    for (i = 0; i < arg_size; i++) {
        REPR(argv)->pos_funcs.at_pos(tc, STABLE(argv), argv, OBJECT_BODY(argv), i, &reg, MVM_reg_obj);
        args[i] = MVM_string_utf8_encode_C_string(tc, MVM_repr_get_str(tc, reg.o));
    }
    args[arg_size] = NULL;
    prog = args[0];

    /* Encode CWD. */
    _cwd = MVM_string_utf8_encode_C_string(tc, cwd);

    MVMROOT(tc, queue, {
    MVMROOT(tc, env, {
    MVMROOT(tc, callbacks, {
        MVMIOAsyncProcessData *data;

        /* Encode environment. */
        size = MVM_repr_elems(tc, env);
        iter = (MVMIter *)MVM_iter(tc, env);
        _env = malloc((size + 1) * sizeof(char *));
        INIT_ENV();

        /* Create handle. */
        data              = calloc(1, sizeof(MVMIOAsyncProcessData));
        handle            = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
        handle->body.ops  = &proc_op_table;
        handle->body.data = data;

        /* Create async task handle. */
        MVMROOT(tc, handle, {
            task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTAsync);
        });
        MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
        task->body.ops  = &spawn_op_table;
        si              = calloc(1, sizeof(SpawnInfo));
        si->prog        = prog;
        si->cwd         = _cwd;
        si->env         = _env;
        si->args        = args;
        MVM_ASSIGN_REF(tc, &(task->common.header), si->handle, handle);
        MVM_ASSIGN_REF(tc, &(task->common.header), si->callbacks, callbacks);
        task->body.data = si;
    });
    });
    });

    /* Hand the task off to the event loop. */
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return (MVMObject *)handle;
}

/* Kills an asynchronously spawned process. */
void MVM_proc_kill_async(MVMThreadContext *tc, MVMObject *handle, MVMint64 signal) {
    MVM_exception_throw_adhoc(tc, "Async process killing NYI");
}

/* Get the current process ID. */
MVMint64 MVM_proc_getpid(MVMThreadContext *tc) {
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}

/* generates a random int64 */
MVMint64 MVM_proc_rand_i(MVMThreadContext *tc) {
    MVMuint64 result = tinymt64_generate_uint64(tc->rand_state);
    return *(MVMint64 *)&result;
}

/* generates a number between 0 and 1 */
MVMnum64 MVM_proc_rand_n(MVMThreadContext *tc) {
    return tinymt64_generate_double(tc->rand_state);
}

/* seed random number generator */
void MVM_proc_seed(MVMThreadContext *tc, MVMint64 seed) {
    /* Seed our one, plus the normal C srand for libtommath. */
    tinymt64_init(tc->rand_state, (MVMuint64)seed);
    srand((MVMuint32)seed);
}

/* gets the system time since the epoch truncated to integral seconds */
MVMint64 MVM_proc_time_i(MVMThreadContext *tc) {
    return (MVMint64)(MVM_platform_now() / 1000000000);
}

/* gets the system time since the epoch as floating point seconds */
MVMnum64 MVM_proc_time_n(MVMThreadContext *tc) {
    return (MVMnum64)MVM_platform_now() / 1000000000.0;
}

MVMString * MVM_executable_name(MVMThreadContext *tc) {
    MVMInstance * const instance = tc->instance;
    if (instance->exec_name)
        return MVM_string_utf8_decode(tc,
            instance->VMString,
            instance->exec_name, strlen(instance->exec_name));
    else
        return tc->instance->str_consts.empty;
}

MVMObject * MVM_proc_clargs(MVMThreadContext *tc) {
    MVMInstance * const instance = tc->instance;
    MVMObject            *clargs = instance->clargs;
    if (!clargs) {
        clargs = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
        MVMROOT(tc, clargs, {
            const MVMint64 num_clargs = instance->num_clargs;
            MVMint64 count;

            MVMString *prog_string = MVM_string_utf8_decode(tc,
                instance->VMString,
                instance->prog_name, strlen(instance->prog_name));

            MVMObject *boxed_str = MVM_repr_box_str(tc,
                instance->boot_types.BOOTStr, prog_string);

            MVM_repr_push_o(tc, clargs, boxed_str);

            for (count = 0; count < num_clargs; count++) {
                char *raw_clarg = instance->raw_clargs[count];
                MVMString *string = MVM_string_utf8_decode(tc,
                    instance->VMString, raw_clarg, strlen(raw_clarg));
                boxed_str = MVM_repr_box_str(tc,
                    instance->boot_types.BOOTStr, string);
                MVM_repr_push_o(tc, clargs, boxed_str);
            }
        });

        instance->clargs = clargs;
    }
    return clargs;
}
