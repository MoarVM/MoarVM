#include "moar.h"
#include "platform/time.h"
#include "tinymt64.h"

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
#include <stdlib.h>
#endif

#ifdef _WIN32
static wchar_t * ANSIToUnicode(MVMuint16 acp, const char *str)
{
     const int          len = MultiByteToWideChar(acp, 0, str, -1, NULL, 0);
     wchar_t * const result = (wchar_t *)MVM_malloc(len * sizeof(wchar_t));

     MultiByteToWideChar(acp, 0, str, -1, (LPWSTR)result, len);

     return result;
}

static char * UnicodeToUTF8(const wchar_t *str)
{
     const int       len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
     char * const result = (char *)MVM_malloc(len + 1);

     WideCharToMultiByte(CP_UTF8, 0, str, -1, result, len, NULL, NULL);

     return result;
}

static char * ANSIToUTF8(MVMuint16 acp, const char * str)
{
    wchar_t * const wstr = ANSIToUnicode(acp, str);
    char  * const result = UnicodeToUTF8(wstr);

    MVM_free(wstr);
    return result;
}

MVM_PUBLIC char **
MVM_UnicodeToUTF8_argv(const int argc, wchar_t **wargv)
{
    int i;
    char **argv = MVM_malloc((argc + 1) * sizeof(*argv));
    for (i = 0; i < argc; ++i)
    {
        argv[i] = UnicodeToUTF8(wargv[i]);
    }
    argv[i] = NULL;
    return argv;
}

#endif

MVMObject * MVM_proc_getenvhash(MVMThreadContext *tc) {
    MVMInstance * const instance = tc->instance;
    MVMObject   *       env_hash;

    MVMuint32     pos = 0;
    MVMString *needle = MVM_string_ascii_decode(tc, instance->VMString, STR_WITH_LEN("="));
#ifndef _WIN32
    char      *env;
#else
    wchar_t   *env;
    (void) _wgetenv(L"windows"); /* populate _wenviron */
#endif

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&needle);

    env_hash = MVM_repr_alloc_init(tc,  MVM_hll_current(tc)->slurpy_hash_type);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&env_hash);

#ifndef _WIN32
    while ((env = environ[pos++]) != NULL) {
        MVMString    *str = MVM_string_utf8_c8_decode(tc, instance->VMString, env, strlen(env));
#else
    while ((env = _wenviron[pos++]) != NULL) {
        char * const _env = UnicodeToUTF8(env);
        MVMString    *str = MVM_string_utf8_c8_decode(tc, instance->VMString, _env, strlen(_env));
#endif

        MVMuint32 index = MVM_string_index(tc, str, needle, 0);

        MVMString *key, *val;
        MVMObject *box;

#ifdef _WIN32
        MVM_free(_env);
#endif
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&str);

        key  = MVM_string_substring(tc, str, 0, index);
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&key);

        val  = MVM_string_substring(tc, str, index + 1, -1);
        box  = MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type, val);
        MVM_repr_bind_key_o(tc, env_hash, key, box);

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
                _env[i++] = MVM_string_utf8_c8_encode_C_string(tc, env_str); \
            } \
            _env[size] = NULL; \
        }); \
    }); \
} while (0)

#define FREE_ENV() do { \
    i = 0;  \
    while(_env[i]) \
        MVM_free(_env[i++]); \
    MVM_free(_env); \
} while (0)

static void spawn_on_exit(uv_process_t *req, MVMint64 exit_status, int term_signal) {
    if (req->data)
        *(MVMint64 *)req->data = (exit_status << 8) | term_signal;
    uv_unref((uv_handle_t *)req);
    uv_close((uv_handle_t *)req, NULL);
}

static void setup_process_stdio(MVMThreadContext *tc, MVMObject *handle, uv_process_t *process,
        uv_stdio_container_t *stdio, int fd, MVMint64 flags, const char *op) {
    if (flags & MVM_PIPE_CAPTURE) {
        MVMIOSyncPipeData *pipedata;

        if (REPR(handle)->ID != MVM_REPR_ID_MVMOSHandle)
            MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", op);

        pipedata           = (MVMIOSyncPipeData *)((MVMOSHandle *)handle)->body.data;
        pipedata->process  = process;

        stdio->flags       = UV_CREATE_PIPE | (fd == 0 ? UV_READABLE_PIPE : UV_WRITABLE_PIPE);
        stdio->data.stream = pipedata->ss.handle;
    }
    else if (flags & MVM_PIPE_INHERIT) {
        if (handle == tc->instance->VMNull) {
            stdio->flags   = UV_INHERIT_FD;
            stdio->data.fd = fd;
        }
        else {
            MVMOSHandleBody body = ((MVMOSHandle *)handle)->body;

            if (REPR(handle)->ID != MVM_REPR_ID_MVMOSHandle)
                MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", op);

            body.ops->pipeable->bind_stdio_handle(tc, ((MVMOSHandle *)handle), stdio, process);
        }
    }
    else
        stdio->flags = UV_IGNORE;
}

MVMint64 MVM_proc_shell(MVMThreadContext *tc, MVMString *cmd, MVMString *cwd, MVMObject *env,
        MVMObject *in, MVMObject *out, MVMObject *err, MVMint64 flags) {
    MVMint64 result = 0, spawn_result;
    uv_process_t *process = MVM_calloc(1, sizeof(uv_process_t));
    uv_process_options_t process_options = {0};
    uv_stdio_container_t process_stdio[3];
    int i, process_still_running;

    char * const cmdin = MVM_string_utf8_c8_encode_C_string(tc, cmd);
    char * const _cwd = MVM_string_utf8_c8_encode_C_string(tc, cwd);
    const MVMuint64 size = MVM_repr_elems(tc, env);
    char **_env = MVM_malloc((size + 1) * sizeof(char *));
    MVMIter *iter;

#ifdef _WIN32
    const MVMuint16 acp = GetACP(); /* We should get ACP at runtime. */
    char * const _cmd = ANSIToUTF8(acp, getenv("ComSpec"));
    char *args[3];
    args[0] = "/c";
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

    MVMROOT(tc, in, {
    MVMROOT(tc, out, {
    MVMROOT(tc, err, {
        iter = (MVMIter *)MVM_iter(tc, env);
        INIT_ENV();
    });
    });
    });

    setup_process_stdio(tc, in,  process, &process_stdio[0], 0, flags,      "shell");
    setup_process_stdio(tc, out, process, &process_stdio[1], 1, flags >> 3, "shell");
    if (!(flags & MVM_PIPE_MERGED_OUT_ERR))
        setup_process_stdio(tc, err, process, &process_stdio[2], 2, flags >> 6, "shell");

    process_options.stdio       = process_stdio;
    process_options.file        = _cmd;
    process_options.args        = args;
    process_options.cwd         = _cwd;
    process_options.flags       = UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS | UV_PROCESS_WINDOWS_HIDE;
    process_options.env         = _env;
    if (flags & MVM_PIPE_MERGED_OUT_ERR) {
        process_options.stdio_count = 2;
    }
    else
        process_options.stdio_count = 3;
    process_options.exit_cb     = spawn_on_exit;
    if (flags & (MVM_PIPE_CAPTURE_IN | MVM_PIPE_CAPTURE_OUT | MVM_PIPE_CAPTURE_ERR)) {
        process_still_running = 1;
        process->data = MVM_calloc(1, sizeof(MVMint64));
        uv_ref((uv_handle_t *)process);
        spawn_result = uv_spawn(tc->loop, process, &process_options);
        if (spawn_result)
            result = spawn_result;
    }
    else {
        process_still_running = 0;
        process->data = &result;
        uv_ref((uv_handle_t *)process);
        spawn_result = uv_spawn(tc->loop, process, &process_options);
        if (spawn_result)
            result = spawn_result;
        else
            uv_run(tc->loop, UV_RUN_DEFAULT);
    }

    FREE_ENV();
    MVM_free(_cwd);
#ifdef _WIN32
    MVM_free(_cmd);
#endif
    MVM_free(cmdin);
    uv_unref((uv_handle_t *)process);

    if (!process_still_running)
        MVM_free(process);

    return result;
}

MVMint64 MVM_proc_spawn(MVMThreadContext *tc, MVMObject *argv, MVMString *cwd, MVMObject *env,
        MVMObject *in, MVMObject *out, MVMObject *err, MVMint64 flags) {
    MVMint64 result = 0, spawn_result;
    uv_process_t *process = MVM_calloc(1, sizeof(uv_process_t));
    uv_process_options_t process_options = {0};
    uv_stdio_container_t process_stdio[3];
    int i;

    char   * const      _cwd = MVM_string_utf8_c8_encode_C_string(tc, cwd);
    const MVMuint64     size = MVM_repr_elems(tc, env);
    char              **_env = MVM_malloc((size + 1) * sizeof(char *));
    const MVMuint64  arg_size = MVM_repr_elems(tc, argv);
    char             **args = MVM_malloc((arg_size + 1) * sizeof(char *));
    MVMRegister        reg;
    MVMIter           *iter;

    i = 0;
    while(i < arg_size) {
        REPR(argv)->pos_funcs.at_pos(tc, STABLE(argv), argv, OBJECT_BODY(argv), i, &reg, MVM_reg_obj);
        args[i++] = MVM_string_utf8_c8_encode_C_string(tc, MVM_repr_get_str(tc, reg.o));
    }
    args[arg_size] = NULL;

    MVMROOT(tc, in, {
    MVMROOT(tc, out, {
    MVMROOT(tc, err, {
        iter = (MVMIter *)MVM_iter(tc, env);
        INIT_ENV();
    });
    });
    });

    setup_process_stdio(tc, in,  process, &process_stdio[0], 0, flags,      "spawn");
    setup_process_stdio(tc, out, process, &process_stdio[1], 1, flags >> 3, "spawn");
    if (!(flags & MVM_PIPE_MERGED_OUT_ERR))
        setup_process_stdio(tc, err, process, &process_stdio[2], 2, flags >> 6, "spawn");

    process_options.stdio       = process_stdio;
    process_options.file        = arg_size ? args[0] : NULL;
    process_options.args        = args;
    process_options.cwd         = _cwd;
    process_options.flags       = UV_PROCESS_WINDOWS_HIDE;
    process_options.env         = _env;
    if (flags & MVM_PIPE_MERGED_OUT_ERR) {
        process_options.stdio_count = 2;
    }
    else
        process_options.stdio_count = 3;
    process_options.exit_cb     = spawn_on_exit;
    if (flags & (MVM_PIPE_CAPTURE_IN | MVM_PIPE_CAPTURE_OUT | MVM_PIPE_CAPTURE_ERR)) {
        process->data = MVM_calloc(1, sizeof(MVMint64));
        uv_ref((uv_handle_t *)process);
        spawn_result = uv_spawn(tc->loop, process, &process_options);
        if (spawn_result)
            result = spawn_result;
    }
    else {
        process->data = &result;
        uv_ref((uv_handle_t *)process);
        spawn_result = uv_spawn(tc->loop, process, &process_options);
        if (spawn_result)
            result = spawn_result;
        else
            uv_run(tc->loop, UV_RUN_DEFAULT);
    }

    FREE_ENV();
    MVM_free(_cwd);
    uv_unref((uv_handle_t *)process);

    i = 0;
    while(args[i])
        MVM_free(args[i++]);

    MVM_free(args);

    return result;
}

/* Data that we keep for an asynchronous process handle. */
typedef struct {
    /* The libuv handle to the process. */
    uv_process_t *handle;

    /* The async task handle, provided we're running. */
    MVMObject *async_task;

    /* The exit signal to send, if any. */
    MVMint64 signal;
} MVMIOAsyncProcessData;

typedef enum {
    STATE_UNSTARTED,
    STATE_STARTED,
    STATE_DONE
} ProcessState;

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
    uv_stream_t       *stdin_handle;
    ProcessState       state;
    int                using;
} SpawnInfo;

/* Info we convey about a write task. */
typedef struct {
    MVMOSHandle      *handle;
    MVMString        *str_data;
    MVMObject        *buf_data;
    uv_write_t       *req;
    uv_buf_t          buf;
    MVMThreadContext *tc;
    int               work_idx;
} SpawnWriteInfo;

/* Completion handler for an asynchronous write. */
static void on_write(uv_write_t *req, int status) {
    SpawnWriteInfo   *wi  = (SpawnWriteInfo *)req->data;
    MVMThreadContext *tc  = wi->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = MVM_io_eventloop_get_active_work(tc, wi->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (status >= 0) {
        MVMROOT(tc, arr, {
        MVMROOT(tc, t, {
            MVMObject *bytes_box = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt,
                wi->buf.len);
            MVM_repr_push_o(tc, arr, bytes_box);
        });
        });
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    }
    else {
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        MVMROOT(tc, arr, {
        MVMROOT(tc, t, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(status));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        });
        });
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
    if (wi->str_data)
        MVM_free(wi->buf.base);
    MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
    MVM_free(wi->req);
}

/* Does setup work for an asynchronous write. */
static void write_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncProcessData *handle_data;
    MVMAsyncTask          *spawn_task;
    SpawnInfo             *si;
    char                  *output;
    int                    output_size, r;

    /* Add to work in progress. */
    SpawnWriteInfo *wi = (SpawnWriteInfo *)data;
    wi->tc             = tc;
    wi->work_idx       = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Encode the string, or extract buf data. */
    if (wi->str_data) {
        MVMuint64 output_size_64;
        output = MVM_string_utf8_encode(tc, wi->str_data, &output_size_64, 1);
        output_size = (int)output_size_64;
    }
    else {
        MVMArray *buffer = (MVMArray *)wi->buf_data;
        output = (char *)(buffer->body.slots.i8 + buffer->body.start);
        output_size = (int)buffer->body.elems;
    }

    /* Create and initialize write request. */
    wi->req           = MVM_malloc(sizeof(uv_write_t));
    wi->buf           = uv_buf_init(output, output_size);
    wi->req->data     = data;
    handle_data       = (MVMIOAsyncProcessData *)wi->handle->body.data;
    spawn_task        = (MVMAsyncTask *)handle_data->async_task;
    si                = spawn_task ? (SpawnInfo *)spawn_task->body.data : NULL;
    if (!si || !si->stdin_handle || (r = uv_write(wi->req, si->stdin_handle, &(wi->buf), 1, on_write)) < 0) {
        /* Error; need to notify. */
        MVMROOT(tc, async_task, {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask *t   = (MVMAsyncTask *)async_task;
            MVM_repr_push_o(tc, arr, t->body.schedulee);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVMROOT(tc, arr, {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, (si && si->stdin_handle
                        ? uv_strerror(r)
                        : "This process is not opened for write"));
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            });
            MVM_repr_push_o(tc, t->body.queue, arr);
        });

        /* Cleanup handle. */
        MVM_free(wi->req);
        wi->req = NULL;
    }
}

/* Marks objects for a write task. */
static void write_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    SpawnWriteInfo *wi = (SpawnWriteInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &wi->handle);
    MVM_gc_worklist_add(tc, worklist, &wi->str_data);
    MVM_gc_worklist_add(tc, worklist, &wi->buf_data);
}

/* Frees info for a write task. */
static void write_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data)
        MVM_free(data);
}

/* Operations table for async write task. */
static const MVMAsyncTaskOps write_op_table = {
    write_setup,
    NULL,
    write_gc_mark,
    write_gc_free
};

static MVMAsyncTask * write_str(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                MVMObject *schedulee, MVMString *s, MVMObject *async_type) {
    MVMAsyncTask *task;
    SpawnWriteInfo    *wi;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncwritestr target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncwritestr result type must have REPR AsyncTask");

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
    MVMROOT(tc, h, {
    MVMROOT(tc, s, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &write_op_table;
    wi              = MVM_calloc(1, sizeof(SpawnWriteInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->str_data, s);
    task->body.data = wi;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return task;
}

static MVMAsyncTask * write_bytes(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                  MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type) {
    MVMAsyncTask *task;
    SpawnWriteInfo    *wi;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncwritebytes target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncwritebytes result type must have REPR AsyncTask");
    if (!IS_CONCRETE(buffer) || REPR(buffer)->ID != MVM_REPR_ID_VMArray)
        MVM_exception_throw_adhoc(tc, "asyncwritebytes requires a native array to read from");
    if (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_U8
        && ((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "asyncwritebytes requires a native array of uint8 or int8");

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
    MVMROOT(tc, h, {
    MVMROOT(tc, buffer, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &write_op_table;
    wi              = MVM_calloc(1, sizeof(SpawnWriteInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->buf_data, buffer);
    task->body.data = wi;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return task;
}

/* Marks an async handle. */
static void proc_async_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    MVMIOAsyncProcessData *apd = (MVMIOAsyncProcessData *)data;
    if (data)
        MVM_gc_worklist_add(tc, worklist, &(apd->async_task));
}

/* Does an asynchronous close (since it must run on the event loop). */
static void close_cb(uv_handle_t *handle) {
    MVM_free(handle);
}
static void close_perform(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    uv_close((uv_handle_t *)data, close_cb);
}

/* Operations table for async close task. */
static const MVMAsyncTaskOps close_op_table = {
    close_perform,
    NULL,
    NULL,
    NULL
};

static void deferred_close_perform(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data);

static const MVMAsyncTaskOps deferred_close_op_table = {
    deferred_close_perform,
    NULL,
    NULL,
    NULL
};

static MVMint64 close_stdin(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncProcessData *handle_data = (MVMIOAsyncProcessData *)h->body.data;
    MVMAsyncTask          *spawn_task  = (MVMAsyncTask *)handle_data->async_task;
    SpawnInfo             *si          = spawn_task ? (SpawnInfo *)spawn_task->body.data : NULL;
    if (si && si->state == STATE_UNSTARTED) {
        MVMAsyncTask *task;
        MVMROOT(tc, h, {
            task = (MVMAsyncTask *)MVM_repr_alloc_init(tc,
                tc->instance->boot_types.BOOTAsync);
        });
        task->body.ops  = &deferred_close_op_table;
        task->body.data = si;
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
        return 0;
    }
    if (si && si->stdin_handle) {
        MVMAsyncTask *task;
        MVMROOT(tc, h, {
            task = (MVMAsyncTask *)MVM_repr_alloc_init(tc,
                tc->instance->boot_types.BOOTAsync);
        });
        task->body.ops  = &close_op_table;
        task->body.data = si->stdin_handle;
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
        si->stdin_handle = NULL;
    }
    return 0;
}

static void deferred_close_perform(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    SpawnInfo *si = (SpawnInfo *) data;
    MVMOSHandle *h = (MVMOSHandle *) si->handle;

    if (si->state == STATE_UNSTARTED) {
        MVMAsyncTask *task;
        MVMROOT(tc, h, {
            task = (MVMAsyncTask *)MVM_repr_alloc_init(tc,
                tc->instance->boot_types.BOOTAsync);
        });
        task->body.ops  = &deferred_close_op_table;
        task->body.data = si;
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
        return;
    }
    if (si->stdin_handle) {
        close_stdin(tc, h);
    }
}

/* IO ops table, for async process, populated with functions. */
static const MVMIOAsyncWritable proc_async_writable = { write_str, write_bytes };
static const MVMIOClosable      closable            = { close_stdin };
static const MVMIOOps proc_op_table = {
    &closable,
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
    NULL,
    proc_async_gc_mark,
    NULL
};

static void spawn_async_close(uv_handle_t *handle) {
    MVM_free(handle);
}

static void async_spawn_on_exit(uv_process_t *req, MVMint64 exit_status, int term_signal) {
    /* Check we've got a callback to fire. */
    SpawnInfo        *si  = (SpawnInfo *)req->data;
    MVMThreadContext *tc  = si->tc;
    MVMObject *done_cb = MVM_repr_at_key_o(tc, si->callbacks,
        tc->instance->str_consts.done);
    MVMOSHandle *os_handle;
    if (!MVM_is_null(tc, done_cb)) {
        MVMROOT(tc, done_cb, {
            /* Get status. */
            MVMint64 status = (exit_status << 8) | term_signal;

            /* Get what we'll need to build and convey the result. */
            MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask     *t   = MVM_io_eventloop_get_active_work(tc, si->work_idx);

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

    /* when invoked via MVMIOOps, close_stdin is already wrapped in a mutex */
    os_handle = (MVMOSHandle *) si->handle;
    uv_mutex_lock(os_handle->body.mutex);
    si->state = STATE_DONE;
    close_stdin(tc, os_handle);
    uv_mutex_unlock(os_handle->body.mutex);

    /* Close handle. */
    uv_close((uv_handle_t *)req, spawn_async_close);
    ((MVMIOAsyncProcessData *)((MVMOSHandle *)si->handle)->body.data)->handle = NULL;
    if (--si->using == 0)
        MVM_io_eventloop_remove_active_work(tc, &(si->work_idx));
}

/* Allocates a buffer of the suggested size. */
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    size_t size = suggested_size > 0 ? suggested_size : 4;
    buf->base   = MVM_malloc(size);
    buf->len    = size;
}

/* Read functions for stdout/stderr. */
static void async_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf, SpawnInfo *si,
                       MVMObject *callback, MVMDecodeStream *ds, MVMuint32 seq_number) {
    MVMThreadContext *tc  = si->tc;
    MVMObject *arr;
    MVMAsyncTask *t;
    MVMROOT(tc, callback, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        t = MVM_io_eventloop_get_active_work(tc, si->work_idx);
    });
    MVM_repr_push_o(tc, arr, callback);
    if (nread >= 0) {
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
                res_buf->body.slots.i8 = (MVMint8 *)buf->base;
                res_buf->body.start    = 0;
                res_buf->body.ssize    = buf->len;
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
            MVMObject *final = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, seq_number);
            MVM_repr_push_o(tc, arr, final);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        });
        });
        if (buf->base)
            MVM_free(buf->base);
        uv_close((uv_handle_t *) handle, NULL);
        if (--si->using == 0)
            MVM_io_eventloop_remove_active_work(tc, &(si->work_idx));
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
            MVM_free(buf->base);
        uv_close((uv_handle_t *) handle, NULL);
        if (--si->using == 0)
            MVM_io_eventloop_remove_active_work(tc, &(si->work_idx));
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
    uv_process_t *process = MVM_calloc(1, sizeof(uv_process_t));
    uv_process_options_t process_options = {0};
    uv_stdio_container_t process_stdio[3];
    uv_pipe_t *stdout_pipe = NULL;
    uv_pipe_t *stderr_pipe = NULL;
    uv_read_cb stdout_cb, stderr_cb;

    /* Add to work in progress. */
    SpawnInfo *si = (SpawnInfo *)data;
    si->tc        = tc;
    si->work_idx  = MVM_io_eventloop_add_active_work(tc, async_task);
    si->using     = 1;

    /* Create input/output handles as needed. */
    if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.write)) {
        uv_pipe_t *pipe = MVM_malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, pipe, 0);
        pipe->data = si;
        process_stdio[0].flags       = UV_CREATE_PIPE | UV_READABLE_PIPE;
        process_stdio[0].data.stream = (uv_stream_t *)pipe;
        si->stdin_handle             = (uv_stream_t *)pipe;
    }
    else {
        process_stdio[0].flags   = UV_INHERIT_FD;
        process_stdio[0].data.fd = 0;
    }
    if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stdout_chars)) {
        uv_pipe_t *pipe = MVM_malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, pipe, 0);
        pipe->data = si;
        process_stdio[1].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
        process_stdio[1].data.stream = (uv_stream_t *)pipe;
        si->ds_stdout                = MVM_string_decodestream_create(tc, MVM_encoding_type_utf8, 0, 1);
        stdout_pipe                  = pipe;
        stdout_cb                    = async_spawn_stdout_chars_read;
        si->using++;
    }
    else if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stdout_bytes)) {
        uv_pipe_t *pipe = MVM_malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, pipe, 0);
        pipe->data = si;
        process_stdio[1].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
        process_stdio[1].data.stream = (uv_stream_t *)pipe;
        stdout_pipe                  = pipe;
        stdout_cb                    = async_spawn_stdout_bytes_read;
        si->using++;
    }
    else {
        process_stdio[1].flags   = UV_INHERIT_FD;
        process_stdio[1].data.fd = 1;
    }
    if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stderr_chars)) {
        uv_pipe_t *pipe = MVM_malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, pipe, 0);
        pipe->data = si;
        process_stdio[2].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
        process_stdio[2].data.stream = (uv_stream_t *)pipe;
        si->ds_stderr                = MVM_string_decodestream_create(tc, MVM_encoding_type_utf8, 0, 1);
        stderr_pipe                  = pipe;
        stderr_cb                    = async_spawn_stderr_chars_read;
        si->using++;
    }
    else if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stderr_bytes)) {
        uv_pipe_t *pipe = MVM_malloc(sizeof(uv_pipe_t));
        uv_pipe_init(tc->loop, pipe, 0);
        pipe->data = si;
        process_stdio[2].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
        process_stdio[2].data.stream = (uv_stream_t *)pipe;
        stderr_pipe                  = pipe;
        stderr_cb                    = async_spawn_stderr_bytes_read;
        si->using++;
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
        si->state = STATE_DONE;
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
            MVM_io_eventloop_remove_active_work(tc, &(si->work_idx));
        }
    }
    else {
        MVMOSHandle           *handle  = (MVMOSHandle *)si->handle;
        MVMIOAsyncProcessData *apd     = (MVMIOAsyncProcessData *)handle->body.data;
        MVMObject *ready_cb;
        apd->handle                    = process;

        ready_cb = MVM_repr_at_key_o(tc, si->callbacks,
            tc->instance->str_consts.ready);
        si->state = STATE_STARTED;

        if (!MVM_is_null(tc, ready_cb)) {
            MVMROOT(tc, ready_cb, {
            MVMROOT(tc, async_task, {
                MVMObject *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
                MVM_repr_push_o(tc, arr, ready_cb);
                MVM_repr_push_o(tc, ((MVMAsyncTask *)async_task)->body.queue, arr);
            });
            });
        }

        /* Start any output readers. */
        if (stdout_pipe)
            uv_read_start((uv_stream_t *)stdout_pipe, on_alloc, stdout_cb);
        if (stderr_pipe)
            uv_read_start((uv_stream_t *)stderr_pipe, on_alloc, stderr_cb);
    }
}

/* On cancel, kill the process. */
static void spawn_cancel(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    /* Locate handle. */
    SpawnInfo             *si      = (SpawnInfo *)data;
    MVMOSHandle           *handle  = (MVMOSHandle *)si->handle;
    MVMIOAsyncProcessData *apd     = (MVMIOAsyncProcessData *)handle->body.data;
    uv_process_t          *phandle = apd->handle;

    /* If it didn't already end, try to kill it. exit_cb will clean up phandle
     * should the signal lead to process exit. */
    if (phandle) {
#ifdef _WIN32
        /* On Windows, make sure we use a signal that will actually work. */
        if (apd->signal != SIGTERM && apd->signal != SIGKILL && apd->signal != SIGINT)
            apd->signal = SIGKILL;
#endif
        uv_process_kill(phandle, (int)apd->signal);
    }
}

/* Marks objects for a spawn task. */
static void spawn_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    SpawnInfo *si = (SpawnInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &si->handle);
    MVM_gc_worklist_add(tc, worklist, &si->callbacks);
}

/* Frees info for a spawn task. */
static void spawn_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        SpawnInfo *si = (SpawnInfo *)data;
        if (si->cwd) {
            MVM_free(si->cwd);
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
                MVM_free(si->args[i++]);
            MVM_free(si->args);
            si->args = NULL;
        }
        if (si->ds_stdout) {
            MVM_string_decodestream_destroy(tc, si->ds_stdout);
            si->ds_stdout = NULL;
        }
        if (si->ds_stderr) {
            MVM_string_decodestream_destroy(tc, si->ds_stderr);
            si->ds_stderr = NULL;
        }
        MVM_free(si);
    }
}

/* Operations table for async connect task. */
static const MVMAsyncTaskOps spawn_op_table = {
    spawn_setup,
    spawn_cancel,
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
    args = MVM_malloc((arg_size + 1) * sizeof(char *));
    for (i = 0; i < arg_size; i++) {
        REPR(argv)->pos_funcs.at_pos(tc, STABLE(argv), argv, OBJECT_BODY(argv), i, &reg, MVM_reg_obj);
        args[i] = MVM_string_utf8_c8_encode_C_string(tc, MVM_repr_get_str(tc, reg.o));
    }
    args[arg_size] = NULL;
    prog = args[0];

    /* Encode CWD. */
    _cwd = MVM_string_utf8_c8_encode_C_string(tc, cwd);

    MVMROOT(tc, queue, {
    MVMROOT(tc, env, {
    MVMROOT(tc, callbacks, {
        MVMIOAsyncProcessData *data;

        /* Encode environment. */
        size = MVM_repr_elems(tc, env);
        iter = (MVMIter *)MVM_iter(tc, env);
        _env = MVM_malloc((size + 1) * sizeof(char *));
        INIT_ENV();

        /* Create handle. */
        data              = MVM_calloc(1, sizeof(MVMIOAsyncProcessData));
        handle            = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
        handle->body.ops  = &proc_op_table;
        handle->body.data = data;

        /* Create async task handle. */
        MVMROOT(tc, handle, {
            task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTAsync);
        });
        MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
        task->body.ops  = &spawn_op_table;
        si              = MVM_calloc(1, sizeof(SpawnInfo));
        si->prog        = prog;
        si->cwd         = _cwd;
        si->env         = _env;
        si->args        = args;
        si->state       = STATE_UNSTARTED;
        MVM_ASSIGN_REF(tc, &(task->common.header), si->handle, handle);
        MVM_ASSIGN_REF(tc, &(task->common.header), si->callbacks, callbacks);
        task->body.data = si;
        MVM_ASSIGN_REF(tc, &(handle->common.header), data->async_task, task);
    });
    });
    });

    /* Hand the task off to the event loop. */
    MVMROOT(tc, handle, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return (MVMObject *)handle;
}

/* Kills an asynchronously spawned process. */
void MVM_proc_kill_async(MVMThreadContext *tc, MVMObject *handle_obj, MVMint64 signal) {
    /* Ensure it's a handle for a process. */
    if (REPR(handle_obj)->ID == MVM_REPR_ID_MVMOSHandle) {
        MVMOSHandle *handle = (MVMOSHandle *)handle_obj;
        if (handle->body.ops == &proc_op_table) {
            /* It's fine; send the kill by cancelling the task. */
            MVMIOAsyncProcessData *data = (MVMIOAsyncProcessData *)handle->body.data;
            data->signal = signal;
            MVM_io_eventloop_cancel_work(tc, data->async_task, NULL, NULL);
            return;
        }
    }
    MVM_exception_throw_adhoc(tc, "killprocasync requires a process handle");
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

MVMnum64 MVM_proc_randscale_n(MVMThreadContext *tc, MVMnum64 scale) {
    return tinymt64_generate_double(tc->rand_state) * scale;
}

/* seed random number generator */
void MVM_proc_seed(MVMThreadContext *tc, MVMint64 seed) {
    /* Seed our one, plus the normal C srand for libtommath. */
    tinymt64_init(tc->rand_state, (MVMuint64)seed);
/* do not call srand if we are not using rand */
#ifndef MP_USE_ALT_RAND
    srand((MVMuint32)seed);
#endif
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
        return MVM_string_utf8_c8_decode(tc,
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
#ifndef _WIN32
        MVMROOT(tc, clargs, {
            const MVMint64 num_clargs = instance->num_clargs;
            MVMint64 count;

            MVMString *prog_string = MVM_string_utf8_c8_decode(tc,
                instance->VMString,
                instance->prog_name, strlen(instance->prog_name));
            MVMObject *boxed_str = MVM_repr_box_str(tc,
                instance->boot_types.BOOTStr, prog_string);
            MVM_repr_push_o(tc, clargs, boxed_str);

            for (count = 0; count < num_clargs; count++) {
                char *raw_clarg = instance->raw_clargs[count];
                MVMString *string = MVM_string_utf8_c8_decode(tc,
                    instance->VMString, raw_clarg, strlen(raw_clarg));
                boxed_str = MVM_repr_box_str(tc,
                    instance->boot_types.BOOTStr, string);
                MVM_repr_push_o(tc, clargs, boxed_str);
            }
        });
#else
        MVMROOT(tc, clargs, {
            const MVMint64 num_clargs = instance->num_clargs;
            MVMint64 count;

            MVMString *prog_string = MVM_string_utf8_c8_decode(tc,
                instance->VMString,
                instance->prog_name, strlen(instance->prog_name));
            MVMObject *boxed_str = MVM_repr_box_str(tc,
                instance->boot_types.BOOTStr, prog_string);
            MVM_repr_push_o(tc, clargs, boxed_str);

            for (count = 0; count < num_clargs; count++) {
                char *raw_clarg = instance->raw_clargs[count];
                MVMString *string = MVM_string_utf8_c8_decode(tc,
                    instance->VMString, raw_clarg, strlen(raw_clarg));
                boxed_str = MVM_repr_box_str(tc,
                    instance->boot_types.BOOTStr, string);
                MVM_repr_push_o(tc, clargs, boxed_str);
            }
        });
#endif

        instance->clargs = clargs;
    }
    return clargs;
}
