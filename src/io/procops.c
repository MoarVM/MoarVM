#include "moar.h"
#include "platform/time.h"
#include "platform/fork.h"
#include "core/jfs64.h"
#include "bithacks.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>

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

char *resize_pty(int *fd_pty, int cols, int rows) {
    struct winsize winp;
    winp.ws_col = cols;
    winp.ws_row = rows;
    winp.ws_xpixel = 0;
    winp.ws_ypixel = 0;
    if (ioctl(*fd_pty, TIOCSWINSZ, &winp) < 0) {
        char *error_str = MVM_malloc(128);
        snprintf(error_str, 127, "Error in TIOCSWINSZ: %s (error code %i)",
                strerror(errno), errno);
        return error_str;
    }
    return NULL;
}

char *make_pty(int *fd_pty, int *fd_tty, int cols, int rows) {
    int ret;
    char *error_str;

    *fd_pty = posix_openpt(O_RDWR);
    if (*fd_pty < 0) {
        error_str = MVM_malloc(128);
        snprintf(error_str, 127, "Error in posix_openpt: %s (error code %i)",
                strerror(errno), errno);
        return error_str;
    }

    if (grantpt(*fd_pty) < 0) {
        close(*fd_pty);
        error_str = MVM_malloc(128);
        snprintf(error_str, 127, "Error in grantpt: %s (error code %i)",
                strerror(errno), errno);
        return error_str;
    }

    if (unlockpt(*fd_pty) < 0) {
        close(*fd_pty);
        error_str = MVM_malloc(128);
        snprintf(error_str, 127, "Error in unlockpt: %s (error code %i)",
                strerror(errno), errno);
        return error_str;
    }

    int path_tty_size = 40;
    char *path_tty = MVM_calloc(path_tty_size, sizeof(char *));
    // Apple and linux both have ptsname_r.
    // Use TIOCGPTPEER. (see man ioctl_tty) Where is that available?
    // There is no ptsname_r on OpenBSD.
    while ((ret = ptsname_r(*fd_pty, path_tty, path_tty_size)) == ERANGE) {
        path_tty_size *= 2;
        path_tty = MVM_realloc(path_tty, path_tty_size * sizeof(char *));
    }
    if (ret != 0) {
        MVM_free(path_tty);
        close(*fd_pty);
        error_str = MVM_malloc(128);
        snprintf(error_str, 127, "Error in ptsname_r: %s (error code %i)",
                strerror(errno), errno);
        return error_str;
    }

    *fd_tty = open(path_tty, O_RDWR | O_NOCTTY);
    if (*fd_tty < 0) {
        MVM_free(path_tty);
        close(*fd_pty);
        error_str = MVM_malloc(128);
        snprintf(error_str, 127, "Error in open of tty device: %s (error code %i)",
                strerror(errno), errno);
        return error_str;
    }

    MVM_free(path_tty);

    error_str = resize_pty(fd_pty, cols, rows);
    if (error_str) {
        close(*fd_pty);
        close(*fd_tty);
        return error_str;
    }

    return NULL;
}



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

    if (instance->env_hash) {
        return instance->env_hash;
    }
    else {
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

        instance->env_hash = env_hash;

        return env_hash;
    }
}

#define INIT_ENV() do { \
    MVMROOT(tc, iter) { \
        MVMString * const equal = MVM_string_ascii_decode(tc, tc->instance->VMString, STR_WITH_LEN("=")); \
        MVMROOT(tc, equal) { \
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
        } \
    } \
} while (0)

#define FREE_ENV() do { \
    i = 0;  \
    while(_env[i]) \
        MVM_free(_env[i++]); \
    MVM_free(_env); \
} while (0)

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
    uv_stream_t       *stdin_handle;
    MVMuint32          had_stdin_handle;
    int                stdin_to_close;
    MVMuint32          seq_stdout;
    MVMuint32          seq_stderr;
    MVMuint32          seq_merge;
    MVMint64           permit_stdout;
    MVMint64           permit_stderr;
    MVMint64           permit_merge;
    uv_pipe_t         *pipe_stdout;
    uv_pipe_t         *pipe_stderr;
    int                reading_stdout;
    int                reading_stderr;
    ProcessState       state;
    int                using;
    int                merge;
    size_t             last_read;
} SpawnInfo;

/* Info we convey about a write task. */
typedef struct {
    MVMOSHandle      *handle;
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
        MVMROOT2(tc, arr, t) {
            MVMObject *bytes_box = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt,
                wi->buf.len);
            MVM_repr_push_o(tc, arr, bytes_box);
        }
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    }
    else {
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        MVMROOT2(tc, arr, t) {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(status));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        }
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
    MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
    MVM_free_null(wi->req);
}

/* Does setup work for an asynchronous write. */
static void write_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncProcessData *handle_data;
    MVMAsyncTask          *spawn_task;
    MVMArray              *buffer;
    SpawnInfo             *si;
    char                  *output;
    int                    output_size, r = 0;

    /* Add to work in progress. */
    SpawnWriteInfo *wi = (SpawnWriteInfo *)data;
    wi->tc             = tc;
    wi->work_idx       = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Extract buf data. */
    buffer = (MVMArray *)wi->buf_data;
    output = (char *)(buffer->body.slots.i8 + buffer->body.start);
    output_size = (int)buffer->body.elems;

    /* Create and initialize write request. */
    wi->req           = MVM_malloc(sizeof(uv_write_t));
    wi->buf           = uv_buf_init(output, output_size);
    wi->req->data     = data;
    handle_data       = (MVMIOAsyncProcessData *)wi->handle->body.data;
    spawn_task        = (MVMAsyncTask *)handle_data->async_task;
    si                = spawn_task ? (SpawnInfo *)spawn_task->body.data : NULL;
    if (!si || !si->stdin_handle || (r = uv_write(wi->req, si->stdin_handle, &(wi->buf), 1, on_write)) < 0) {
        /* Error; need to notify. */
        MVMROOT(tc, async_task) {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVM_repr_push_o(tc, arr, ((MVMAsyncTask *)async_task)->body.schedulee);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVMROOT(tc, arr) {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, (si && si->stdin_handle
                        ? uv_strerror(r)
                        : si && si->had_stdin_handle
                            ? (si->state == STATE_DONE
                                ? "Cannot write to process that has already terminated"
                                : "Cannot write to process after close-stdin")
                            : "This process is not opened for write"));
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            }
            MVM_repr_push_o(tc, ((MVMAsyncTask *)async_task)->body.queue, arr);
        }

        /* Cleanup handle. */
        MVM_free_null(wi->req);
    }
}

/* Marks objects for a write task. */
static void write_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    SpawnWriteInfo *wi = (SpawnWriteInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &wi->handle);
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
    NULL,
    write_gc_mark,
    write_gc_free
};

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
    MVMROOT4(tc, queue, schedulee, h, buffer) {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    }
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &write_op_table;
    wi              = MVM_calloc(1, sizeof(SpawnWriteInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->buf_data, buffer);
    task->body.data = wi;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task) {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    }

    return task;
}

/* Marks an async handle. */
static void proc_async_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    MVMIOAsyncProcessData *apd = (MVMIOAsyncProcessData *)data;
    if (data)
        MVM_gc_worklist_add(tc, worklist, &(apd->async_task));
}

static void proc_async_gc_free(MVMThreadContext *tc, MVMObject *root, void *data) {
    if (data)
        MVM_free(data);
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
    NULL,
    NULL
};

static void deferred_close_perform(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data);

static const MVMAsyncTaskOps deferred_close_op_table = {
    deferred_close_perform,
    NULL,
    NULL,
    NULL,
    NULL
};

static MVMint64 close_stdin(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncProcessData *handle_data = (MVMIOAsyncProcessData *)h->body.data;
    MVMAsyncTask          *spawn_task  = (MVMAsyncTask *)handle_data->async_task;
    SpawnInfo             *si          = spawn_task ? (SpawnInfo *)spawn_task->body.data : NULL;
    if (si && MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.pty)) {
        // Stdin and stdout use the same FD. So we must not close as there might still be data on stdout.
        return 0;
    }
    if (si && si->state == STATE_UNSTARTED) {
        MVMAsyncTask *task;
        MVMROOT(tc, h) {
            task = (MVMAsyncTask *)MVM_repr_alloc_init(tc,
                tc->instance->boot_types.BOOTAsync);
        }
        task->body.ops  = &deferred_close_op_table;
        task->body.data = si;
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
        return 0;
    }
    if (si && si->stdin_handle) {
        MVMAsyncTask *task;
        MVMROOT(tc, h) {
            task = (MVMAsyncTask *)MVM_repr_alloc_init(tc,
                tc->instance->boot_types.BOOTAsync);
        }
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
        MVMROOT(tc, h) {
            task = (MVMAsyncTask *)MVM_repr_alloc_init(tc,
                tc->instance->boot_types.BOOTAsync);
        }
        task->body.ops  = &deferred_close_op_table;
        task->body.data = si;
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
        return;
    }
    if (si->stdin_handle) {
        close_stdin(tc, h);
    }
}

static MVMObject * get_async_task_handle(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncProcessData *handle_data = (MVMIOAsyncProcessData *)h->body.data;
    return handle_data->async_task;
}

/* IO ops table, for async process, populated with functions. */
static const MVMIOAsyncWritable proc_async_writable = { write_bytes };
static const MVMIOClosable      closable            = { close_stdin };
static const MVMIOOps proc_op_table = {
    &closable,
    NULL,
    NULL,
    NULL,
    &proc_async_writable,
    NULL,
    NULL,
    NULL,
    get_async_task_handle,
    NULL,
    NULL,
    NULL,
    proc_async_gc_mark,
    proc_async_gc_free
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
    uv_mutex_t *mutex;
    if (!MVM_is_null(tc, done_cb)) {
        MVMROOT(tc, done_cb) {
            /* Get status. */
            MVMint64 status = (exit_status << 8) | term_signal;

            /* Get what we'll need to build and convey the result. */
            MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask     *t   = MVM_io_eventloop_get_active_work(tc, si->work_idx);

            /* Box and send along status. */
            MVM_repr_push_o(tc, arr, done_cb);
            MVMROOT2(tc, arr, t) {
                MVMObject *result_box = MVM_repr_box_int(tc,
                    tc->instance->boot_types.BOOTInt, status);
                MVM_repr_push_o(tc, arr, result_box);
            }
            MVM_repr_push_o(tc, t->body.queue, arr);
        }
    }

    /* when invoked via MVMIOOps, close_stdin is already wrapped in a mutex */
    os_handle = (MVMOSHandle *) si->handle;
    mutex = os_handle->body.mutex;
    uv_mutex_lock(mutex);
    si->state = STATE_DONE;
    close_stdin(tc, os_handle);
    uv_mutex_unlock(mutex);

    /* Close any stdin we were bound to. */
    if (si->stdin_to_close) {
        close(si->stdin_to_close);
        si->stdin_to_close = 0;
    }

    /* Close handle. */
    uv_close((uv_handle_t *)req, spawn_async_close);
    ((MVMIOAsyncProcessData *)((MVMOSHandle *)si->handle)->body.data)->handle = NULL;
    if (--si->using == 0)
        MVM_io_eventloop_remove_active_work(tc, &(si->work_idx));
}

#ifndef MIN
    #define MIN(x,y) ((x)<(y)?(x):(y))
#endif

MVM_STATIC_INLINE void adjust_nursery(MVMThreadContext *tc, size_t read_buffer_size) {
    int adjustment = MIN(read_buffer_size, 32768) & ~0x7;
    if (adjustment && (char *)tc->nursery_alloc_limit - adjustment > (char *)tc->nursery_alloc) {
        tc->nursery_alloc_limit = (char *)(tc->nursery_alloc_limit) - adjustment;
        /*fprintf(stderr, "made an adjustment of %x: %p - %p == %x; read buffer size %x\n", adjustment, tc->nursery_alloc_limit, tc->nursery_alloc, (char *)tc->nursery_alloc_limit - (char *)tc->nursery_alloc, read_buffer_size);*/
    }
    else {
        /*fprintf(stderr, "did not make an adjustment of %x: %p - %p == %x; read buffer size %x\n", adjustment, tc->nursery_alloc_limit, tc->nursery_alloc, (char *)tc->nursery_alloc_limit - (char *)tc->nursery_alloc, read_buffer_size);*/
    }
}


/* Allocates a buffer of the suggested size. */
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    SpawnInfo *si = (SpawnInfo *)handle->data;
    size_t size   = si->last_read ? si->last_read : 64;
    MVMThreadContext *tc = si->tc;

    if (size < 128) {
        size = 128;
    }
    else {
        size = MVM_bithacks_next_greater_pow2(size + 1);
    }

    adjust_nursery(tc, size);

    buf->base = MVM_malloc(size);
    buf->len  = size;
}

/* Read functions for stdout/stderr/merged. */
static void async_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf, SpawnInfo *si,
                       MVMObject *callback, MVMuint32 seq_number, MVMint64 *permit) {
    MVMThreadContext *tc  = si->tc;
    MVMObject *arr;
    MVMAsyncTask *t;
    MVMROOT(tc, callback) {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        t = MVM_io_eventloop_get_active_work(tc, si->work_idx);
    }
    MVM_repr_push_o(tc, arr, callback);
    if (nread >= 0) {
        MVMROOT2(tc, t, arr) {
            /* Push the sequence number. */
            MVMObject *seq_boxed = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, seq_number);
            MVM_repr_push_o(tc, arr, seq_boxed);

            /* Push buffer of data. */
            {
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

            /* Update handle with amount read. */
            si->last_read = nread;

            /* Update permit count, stop reading if we run out. */
            if (*permit > 0) {
                (*permit)--;
                if (*permit == 0) {
                    uv_read_stop(handle);
                    if (handle == (uv_stream_t *)si->pipe_stdout)
                        si->reading_stdout = 0;
                    else if (handle == (uv_stream_t *)si->pipe_stderr)
                        si->reading_stderr = 0;
                    else
                        MVM_panic(1, "Confused stopping reading async process handle");
                }
            }
        }
    }
    else if (nread == UV_EOF || (nread == UV_EIO && MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.pty))) {
        MVMROOT2(tc, t, arr) {
            MVMObject *final = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, seq_number);
            MVM_repr_push_o(tc, arr, final);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        }
        if (buf->base)
            MVM_free(buf->base);
        uv_close((uv_handle_t *)handle, NULL);
        if (--si->using == 0)
            MVM_io_eventloop_remove_active_work(tc, &(si->work_idx));
    }
    else {
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        MVMROOT2(tc, t, arr) {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(nread));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        }
        if (buf->base)
            MVM_free(buf->base);
        uv_close((uv_handle_t *)handle, NULL);
        if (--si->using == 0)
            MVM_io_eventloop_remove_active_work(tc, &(si->work_idx));
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
}
static void async_spawn_stdout_bytes_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    SpawnInfo *si = (SpawnInfo *)handle->data;
    MVMObject *cb = MVM_repr_at_key_o(si->tc, si->callbacks,
        si->tc->instance->str_consts.stdout_bytes);
    async_read(handle, nread, buf, si, cb, si->seq_stdout++, &(si->permit_stdout));
}
static void async_spawn_stderr_bytes_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    SpawnInfo *si = (SpawnInfo *)handle->data;
    MVMObject *cb = MVM_repr_at_key_o(si->tc, si->callbacks,
        si->tc->instance->str_consts.stderr_bytes);
    async_read(handle, nread, buf, si, cb, si->seq_stderr++, &(si->permit_stderr));
}
static void async_spawn_merge_bytes_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    SpawnInfo *si = (SpawnInfo *)handle->data;
    MVMObject *cb = MVM_repr_at_key_o(si->tc, si->callbacks,
        si->tc->instance->str_consts.merge_bytes);
    async_read(handle, nread, buf, si, cb, si->seq_merge++, &(si->permit_merge));
}

/* Actually spawns an async task. This runs in the event loop thread. */
static MVMint64 get_pipe_fd(MVMThreadContext *tc, uv_pipe_t *pipe) {
    uv_os_fd_t fd;
    if (uv_fileno((uv_handle_t *)pipe, &fd) == 0)
        return (MVMint64)fd;
    else
        return 0;
}
static void spawn_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMint64 spawn_result;
    char *error_str = NULL;

    /* Process info setup. */
    uv_process_t *process = MVM_calloc(1, sizeof(uv_process_t));
    uv_process_options_t process_options = {0};
    uv_stdio_container_t process_stdio[3];

    int fd_pty, fd_tty;

    /* Add to work in progress. */
    SpawnInfo *si = (SpawnInfo *)data;
    si->tc        = tc;
    si->work_idx  = MVM_io_eventloop_add_active_work(tc, async_task);
    si->using     = 1;

    /* Create input/output handles as needed. */
    if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.pty)) {
        MVMint64 cols = 80;
        MVMint64 rows = 24;
        if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.pty_cols))
            cols = MVM_repr_get_int(tc,
                MVM_repr_at_key_o(tc, si->callbacks, tc->instance->str_consts.pty_cols));
        if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.pty_rows))
            rows = MVM_repr_get_int(tc,
                MVM_repr_at_key_o(tc, si->callbacks, tc->instance->str_consts.pty_rows));

        error_str = make_pty(&fd_pty, &fd_tty, cols, rows);
        if (error_str)
            goto spawn_setup_error;

        process_stdio[0].flags   = UV_INHERIT_FD;
        process_stdio[0].data.fd = fd_tty;
        process_stdio[1].flags   = UV_INHERIT_FD;
        process_stdio[1].data.fd = fd_tty;
        process_stdio[2].flags   = UV_INHERIT_FD;
        process_stdio[2].data.fd = fd_tty;

        int res;
        size_t exec_path_size = 4096;
        char *exec_path = (char*)MVM_calloc(exec_path_size, sizeof(char));
        res = uv_exepath(exec_path, &exec_path_size);
        while (res < 0 && exec_path_size < 4096*8) {
            exec_path_size *= 2;
            exec_path = (char*)MVM_realloc(exec_path, exec_path_size * sizeof(char));
            res = uv_exepath(exec_path, &exec_path_size);
        }
        if (res < 0) {
            close(fd_pty);
            close(fd_tty);
            error_str = MVM_malloc(128);
            snprintf(error_str, 127, "Error retrieving our own executable path: %s (error code %i)",
                    uv_strerror(res), res);
            goto spawn_setup_error;
        }

        int argc = 0;
        while (si->args[argc] != 0)
            argc++;

        char **args = (char**)MVM_calloc(argc+2, sizeof(char*));

        args[0] = exec_path;

        // 26 = strlen("--pty-spawn-helper=") + 5 fd digits + separator + trailing 0
        int args1len = 26 + strlen(si->prog);
        args[1] = (char *)MVM_calloc(args1len, sizeof(char));
        snprintf(args[1], args1len, "--pty-spawn-helper=%05i|%s", fd_pty, si->prog);

        for(int c = 1; si->args[c] != 0; c++)
            args[c+1] = si->args[c];

        args[argc+1] = 0;

        MVM_free(si->args[0]);
        MVM_free(si->args);
        si->args = args;

        MVM_free(si->prog);
        si->prog = exec_path;

        uv_pipe_t *pipe = MVM_malloc(sizeof(uv_pipe_t));
        uv_pipe_init(loop, pipe, 0);
        uv_pipe_open(pipe, fd_pty);
        pipe->data = si;
        si->stdin_handle             = (uv_stream_t *)pipe;
        si->had_stdin_handle         = 1;

        si->pipe_stdout = MVM_malloc(sizeof(uv_pipe_t));
        uv_pipe_init(loop, si->pipe_stdout, 0);
        uv_pipe_open(si->pipe_stdout, fd_pty);
        si->pipe_stdout->data = si;
        si->using++;
    }
    else {
        if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.write)) {
            uv_pipe_t *pipe = MVM_malloc(sizeof(uv_pipe_t));
            uv_pipe_init(loop, pipe, 0);
            pipe->data = si;
            process_stdio[0].flags       = UV_CREATE_PIPE | UV_READABLE_PIPE;
            process_stdio[0].data.stream = (uv_stream_t *)pipe;
            si->stdin_handle             = (uv_stream_t *)pipe;
            si->had_stdin_handle         = 1;
        }
        else if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stdin_fd)) {
            process_stdio[0].flags   = UV_INHERIT_FD;
            process_stdio[0].data.fd = (int)MVM_repr_get_int(tc,
                MVM_repr_at_key_o(tc, si->callbacks, tc->instance->str_consts.stdin_fd));
            if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stdin_fd_close))
                si->stdin_to_close = process_stdio[0].data.fd;
        }
        else {
            process_stdio[0].flags   = UV_INHERIT_FD;
            process_stdio[0].data.fd = 0;
        }
        if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.merge_bytes)) {
            si->pipe_stdout = MVM_malloc(sizeof(uv_pipe_t));
            uv_pipe_init(loop, si->pipe_stdout, 0);
            si->pipe_stdout->data = si;
            process_stdio[1].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
            process_stdio[1].data.stream = (uv_stream_t *)si->pipe_stdout;
            si->pipe_stderr = MVM_malloc(sizeof(uv_pipe_t));
            uv_pipe_init(loop, si->pipe_stderr, 0);
            si->pipe_stderr->data = si;
            process_stdio[2].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
            process_stdio[2].data.stream = (uv_stream_t *)si->pipe_stderr;
            si->merge = 1;
            si->using += 2;
        }
        else {
            if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stdout_bytes)) {
                si->pipe_stdout = MVM_malloc(sizeof(uv_pipe_t));
                uv_pipe_init(loop, si->pipe_stdout, 0);
                si->pipe_stdout->data = si;
                process_stdio[1].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
                process_stdio[1].data.stream = (uv_stream_t *)si->pipe_stdout;
                si->using++;
            }
            else if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stdout_fd)) {
                process_stdio[1].flags   = UV_INHERIT_FD;
                process_stdio[1].data.fd = (int)MVM_repr_get_int(tc,
                    MVM_repr_at_key_o(tc, si->callbacks, tc->instance->str_consts.stdout_fd));
            }
            else {
                process_stdio[1].flags   = UV_INHERIT_FD;
                process_stdio[1].data.fd = 1;
            }
            if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stderr_bytes)) {
                si->pipe_stderr = MVM_malloc(sizeof(uv_pipe_t));
                uv_pipe_init(loop, si->pipe_stderr, 0);
                si->pipe_stderr->data = si;
                process_stdio[2].flags       = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
                process_stdio[2].data.stream = (uv_stream_t *)si->pipe_stderr;
                si->using++;
            }
            else if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.stderr_fd)) {
                process_stdio[2].flags   = UV_INHERIT_FD;
                process_stdio[2].data.fd = (int)MVM_repr_get_int(tc,
                    MVM_repr_at_key_o(tc, si->callbacks, tc->instance->str_consts.stderr_fd));
            }
            else {
                process_stdio[2].flags   = UV_INHERIT_FD;
                process_stdio[2].data.fd = 2;
            }
        }
    }

    /* Set up process start info. */
    process_options.stdio       = process_stdio;
    process_options.file        = si->prog;
    process_options.args        = si->args;
    process_options.cwd         = si->cwd;
    process_options.flags       = UV_PROCESS_WINDOWS_HIDE | UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
    process_options.env         = si->env;
    process_options.stdio_count = 3;
    process_options.exit_cb     = async_spawn_on_exit;

    /* Attach data, spawn, report any error. */
    process->data = si;
    spawn_result  = uv_spawn(loop, process, &process_options);

    if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.pty))
        close(fd_tty);

    if (spawn_result) {
        if (MVM_repr_exists_key(tc, si->callbacks, tc->instance->str_consts.pty))
            close(fd_pty);
        error_str = MVM_malloc(128);
        snprintf(error_str, 127, "Failed to spawn process %s: %s (error code %"PRId64")",
                si->prog, uv_strerror(spawn_result), spawn_result);
        goto spawn_setup_error;
    }

    if (error_str) {
spawn_setup_error:
        MVMObject *msg_box = NULL;
        si->state = STATE_DONE;
        MVMROOT2(tc, async_task, msg_box) {
            MVMObject *error_cb;
            MVMString *msg_str;

            msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, error_str);

            MVM_free(error_str);

            msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);

            error_cb = MVM_repr_at_key_o(tc, si->callbacks,
                tc->instance->str_consts.error);
            if (!MVM_is_null(tc, error_cb)) {
                MVMROOT(tc, error_cb) {
                    MVMObject *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
                    MVM_repr_push_o(tc, arr, error_cb);
                    MVM_repr_push_o(tc, arr, msg_box);
                    MVMROOT(tc, arr) {
                        MVM_repr_push_o(tc, arr, MVM_repr_box_int(tc, tc->instance->boot_types.BOOTInt, spawn_result));
                    }
                    MVM_repr_push_o(tc, ((MVMAsyncTask *)async_task)->body.queue, arr);
                }
            }

            if (si->pipe_stdout) {
                MVMObject *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
                MVMObject *cb = MVM_repr_at_key_o(tc, si->callbacks,
                    tc->instance->str_consts.stdout_bytes);
                MVM_repr_push_o(tc, arr, cb);
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
                MVM_repr_push_o(tc, arr, msg_box);
                MVM_repr_push_o(tc, ((MVMAsyncTask *)async_task)->body.queue, arr);
            }

            if (si->pipe_stderr) {
                MVMObject *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
                MVMObject *cb = MVM_repr_at_key_o(tc, si->callbacks,
                    tc->instance->str_consts.stderr_bytes);
                MVM_repr_push_o(tc, arr, cb);
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
                MVM_repr_push_o(tc, arr, msg_box);
                MVM_repr_push_o(tc, ((MVMAsyncTask *)async_task)->body.queue, arr);
            }

            if (si->stdin_to_close) {
                close(si->stdin_to_close);
                si->stdin_to_close = 0;
            }
        }

        MVM_io_eventloop_remove_active_work(tc, &(si->work_idx));
    }
    else {
        MVMOSHandle *handle = (MVMOSHandle *)si->handle;
        MVMIOAsyncProcessData *apd = (MVMIOAsyncProcessData *)handle->body.data;
        MVMObject *ready_cb = MVM_repr_at_key_o(tc, si->callbacks,
            tc->instance->str_consts.ready);
        apd->handle = process;
        si->state = STATE_STARTED;

        if (!MVM_is_null(tc, ready_cb)) {
            MVMROOT2(tc, ready_cb, async_task) {
                MVMObject *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
                MVMROOT(tc, arr) {
                    MVMObject *pid;
                    MVMObject *handle_arr = MVM_repr_alloc_init(tc,
                        tc->instance->boot_types.BOOTIntArray);
                    MVM_repr_push_i(tc, handle_arr, si->pipe_stdout
                        ? get_pipe_fd(tc, si->pipe_stdout)
                        : -1);
                    MVM_repr_push_i(tc, handle_arr, si->pipe_stderr
                        ? get_pipe_fd(tc, si->pipe_stderr)
                        : -1);
                    MVM_repr_push_o(tc, arr, ready_cb);
                    MVM_repr_push_o(tc, arr, handle_arr);
                    pid = MVM_repr_box_int(tc, tc->instance->boot_types.BOOTInt, process->pid);
                    MVM_repr_push_o(tc, arr, pid);
                    MVM_repr_push_o(tc, ((MVMAsyncTask *)async_task)->body.queue, arr);
                }
            }
        }
    }
}

/* Permits provide the back-pressure mechanism for the readers. */
static void spawn_permit(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data,
                         MVMint64 channel, MVMint64 permits) {
    SpawnInfo *si = (SpawnInfo *)data;
    if (si->work_idx < 0)
        return;
    if (channel == 0 && si->pipe_stdout && si->pipe_stderr && si->merge) {
        if (permits < 0)
            si->permit_merge = -1;
        else if (si->permit_merge < 0)
            si->permit_merge = permits;
        else
            si->permit_merge += permits;
        if (!si->reading_stdout && si->permit_merge) {
            uv_read_start((uv_stream_t *)si->pipe_stdout, on_alloc,
                async_spawn_merge_bytes_read);
            uv_read_start((uv_stream_t *)si->pipe_stderr, on_alloc,
                async_spawn_merge_bytes_read);
            si->reading_stdout = 1;
            si->reading_stderr = 1;
        }
        else if (si->reading_stdout && !si->permit_merge) {
            uv_read_stop((uv_stream_t *)si->pipe_stdout);
            uv_read_stop((uv_stream_t *)si->pipe_stderr);
            si->reading_stdout = 0;
            si->reading_stderr = 0;
        }
    }
    else if (channel == 1 && si->pipe_stdout && !si->merge) {
        if (permits < 0)
            si->permit_stdout = -1;
        else if (si->permit_stdout < 0)
            si->permit_stdout = permits;
        else
            si->permit_stdout += permits;
        if (!si->reading_stdout && si->permit_stdout) {
            uv_read_start((uv_stream_t *)si->pipe_stdout, on_alloc,
                async_spawn_stdout_bytes_read);
            si->reading_stdout = 1;
        }
        else if (si->reading_stdout && !si->permit_stdout) {
            uv_read_stop((uv_stream_t *)si->pipe_stdout);
            si->reading_stdout = 0;
        }
    }
    else if (channel == 2 && si->pipe_stderr && !si->merge) {
        if (permits < 0)
            si->permit_stderr = -1;
        else if (si->permit_stderr < 0)
            si->permit_stderr = permits;
        else
            si->permit_stderr += permits;
        if (!si->reading_stderr && si->permit_stderr) {
            uv_read_start((uv_stream_t *)si->pipe_stderr, on_alloc,
                async_spawn_stderr_bytes_read);
            si->reading_stderr = 1;
        }
        else if (si->reading_stderr && !si->permit_stderr) {
            uv_read_stop((uv_stream_t *)si->pipe_stderr);
            si->reading_stderr = 0;
        }
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
        if (si->prog) {
            MVM_free_null(si->prog);
        }
        if (si->cwd) {
            MVM_free_null(si->cwd);
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
            MVM_free_null(si->args);
        }
        if (si->pipe_stdout) {
            MVM_free_null(si->pipe_stdout);
        }
        if (si->pipe_stderr) {
            MVM_free_null(si->pipe_stderr);
        }
        MVM_free(si);
    }
}

/* Operations table for async connect task. */
static const MVMAsyncTaskOps spawn_op_table = {
    spawn_setup,
    spawn_permit,
    spawn_cancel,
    spawn_gc_mark,
    spawn_gc_free
};

/* Spawn a process asynchronously. */
MVMObject * MVM_proc_spawn_async(MVMThreadContext *tc, MVMObject *queue, MVMString *prog,
                                 MVMObject *argv, MVMString *cwd, MVMObject *env,
                                 MVMObject *callbacks) {
    MVMAsyncTask  *task;
    MVMOSHandle   *handle;
    SpawnInfo     *si;
    char          *_prog, *_cwd, **_env, **args;
    MVMuint64      size, arg_size, i;
    MVMIter       *iter;
    MVMRegister    reg;

    /* Validate queue REPR. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "spawnprocasync target queue must have ConcBlockingQueue REPR");

    /* Encode program name and arguments. */
    arg_size = MVM_repr_elems(tc, argv);
    if (arg_size < 1)
        MVM_exception_throw_adhoc(tc, "spawnprocasync must have first arg for program");

    _prog = MVM_string_utf8_c8_encode_C_string(tc, prog);

    /* We need a trailing NULL byte, thus the +1. */
    args = MVM_malloc((arg_size + 1) * sizeof(char *));
    for (i = 0; i < arg_size; i++) {
        REPR(argv)->pos_funcs.at_pos(tc, STABLE(argv), argv, OBJECT_BODY(argv), i, &reg, MVM_reg_obj);
        args[i] = MVM_string_utf8_c8_encode_C_string(tc, MVM_repr_get_str(tc, reg.o));
    }
    args[arg_size] = NULL;

    /* Encode CWD. */
    _cwd = MVM_string_utf8_c8_encode_C_string(tc, cwd);

    MVMROOT3(tc, queue, env, callbacks) {
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
        MVMROOT(tc, handle) {
            task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTAsync);
        }
        MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
        task->body.ops  = &spawn_op_table;
        si              = MVM_calloc(1, sizeof(SpawnInfo));
        si->prog        = _prog;
        si->cwd         = _cwd;
        si->env         = _env;
        si->args        = args;
        si->state       = STATE_UNSTARTED;
        MVM_ASSIGN_REF(tc, &(task->common.header), si->handle, handle);
        MVM_ASSIGN_REF(tc, &(task->common.header), si->callbacks, callbacks);
        task->body.data = si;
        MVM_ASSIGN_REF(tc, &(handle->common.header), data->async_task, task);
    }

    /* Hand the task off to the event loop. */
    MVMROOT(tc, handle) {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    }

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

/* Get the process ID of the parent process */
MVMint64 MVM_proc_getppid(MVMThreadContext *tc) {
    return uv_os_getppid();
}

/* generates a random int64 */
MVMint64 MVM_proc_rand_i(MVMThreadContext *tc) {
    return (MVMint64)jfs64_generate_uint64(tc->rand_state);
}

/* generates a number between 0 and 1 */
MVMnum64 MVM_proc_rand_n(MVMThreadContext *tc) {
    return ((jfs64_generate_uint64(tc->rand_state) >> 11) * (1.0 / 9007199254740992.0));
}

MVMnum64 MVM_proc_randscale_n(MVMThreadContext *tc, MVMnum64 scale) {
    return ((jfs64_generate_uint64(tc->rand_state) >> 11) * (1.0 / 9007199254740992.0)) * scale;
}

/* seed random number generator */
void MVM_proc_seed(MVMThreadContext *tc, MVMint64 seed) {
    jfs64_init(tc->rand_state, (MVMuint64)seed);
}

/* gets the system time since the epoch in nanoseconds */
MVMuint64 MVM_proc_time(MVMThreadContext *tc) {
    return MVM_platform_now();
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
        MVMROOT(tc, clargs) {
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
        }

        instance->clargs = clargs;
    }
    return clargs;
}

/* Gets resource usage statistics, so far as they are portably available (see
 * libuv docs) and puts them into an integer array. */
void MVM_proc_getrusage(MVMThreadContext *tc, MVMObject *result) {
    uv_rusage_t usage;
    int r;
    if ((r = uv_getrusage(&usage)) > 0)
        MVM_exception_throw_adhoc(tc, "Unable to getrusage: %s", uv_strerror(r));
    if (REPR(result)->ID != MVM_REPR_ID_VMArray || !IS_CONCRETE(result) ||
            ((MVMArrayREPRData *)STABLE(result)->REPR_data)->slot_type != MVM_ARRAY_I64) {
        MVM_exception_throw_adhoc(tc, "getrusage needs a concrete 64bit int array.");
    }
    MVM_repr_bind_pos_i(tc, result, 0, usage.ru_utime.tv_sec);
    MVM_repr_bind_pos_i(tc, result, 1, usage.ru_utime.tv_usec);
    MVM_repr_bind_pos_i(tc, result, 2, usage.ru_stime.tv_sec);
    MVM_repr_bind_pos_i(tc, result, 3, usage.ru_stime.tv_usec);
    MVM_repr_bind_pos_i(tc, result, 4, usage.ru_maxrss);
    MVM_repr_bind_pos_i(tc, result, 5, usage.ru_ixrss);
    MVM_repr_bind_pos_i(tc, result, 6, usage.ru_idrss);
    MVM_repr_bind_pos_i(tc, result, 7, usage.ru_isrss);
    MVM_repr_bind_pos_i(tc, result, 8, usage.ru_minflt);
    MVM_repr_bind_pos_i(tc, result, 9, usage.ru_majflt);
    MVM_repr_bind_pos_i(tc, result, 10, usage.ru_nswap);
    MVM_repr_bind_pos_i(tc, result, 11, usage.ru_inblock);
    MVM_repr_bind_pos_i(tc, result, 12, usage.ru_oublock);
    MVM_repr_bind_pos_i(tc, result, 13, usage.ru_msgsnd);
    MVM_repr_bind_pos_i(tc, result, 14, usage.ru_msgrcv);
    MVM_repr_bind_pos_i(tc, result, 15, usage.ru_nsignals);
    MVM_repr_bind_pos_i(tc, result, 16, usage.ru_nvcsw);
    MVM_repr_bind_pos_i(tc, result, 17, usage.ru_nivcsw);
}

/*

Per Linux Programmer's Manual fork(2):

       fork()  creates  a  new process by duplicating the calling process.  The new
       process is referred to  as  the  child  process.   The  calling  process  is
       referred to as the parent process.

And:

       *  The child process is created with a single  thread—the  one  that  called
          fork().   The entire virtual address space of the parent is replicated in
          the child, including the states  of  mutexes,  condition  variables,  and
          other  pthreads  objects; the use of pthread_atfork(3) may be helpful for
          dealing with problems that this can cause.

       *  After a fork() in a multithreaded program, the child can safely call only
          async-signal-safe  functions (see signal-safety(7)) until such time as it
          calls execve(2).

As it happens, MoarVM is inherently multithreaded - a spesh thread is started at
startup, and an asynchronous IO thread is started on demand. A debugserver
thread may also be started.

So before we can fork, we have to pretend we're temporarily non-multithreaded.
It is possible to stop the system threads because we have control over the locks
they use and we can signal them to stop. Because we have no such control over
user threads, we will not attempt to fork() in that case, and throw an exception
instead.

The simplest way of doing this that I can see is:

- prevent other threads from starting the event loop
- prevent other threads from modifying the threads list,
  since we'll need to inspect it.

*/

MVMint64 MVM_proc_fork(MVMThreadContext *tc) {
    MVMInstance *instance = tc->instance;
    const char *error = NULL;
    MVMint64 pid = -1;

    if (!MVM_platform_supports_fork(tc))
        MVM_exception_throw_adhoc(tc, "This platform does not support fork()");

    /* Acquire the necessary locks. The event loop mutex will protect
     * modification of the event loop. Nothing yet protects against the
     * modification of the spesh worker, but this is currently the only code
     * that it could conflict with. Maybe we need an explicit fork loop */
    MVM_gc_mark_thread_blocked(tc);
    uv_mutex_lock(&instance->mutex_event_loop);
    MVM_gc_mark_thread_unblocked(tc);

    /* Stop and join the system threads */
    MVM_spesh_worker_stop(tc);
    MVM_io_eventloop_stop(tc);
    MVM_spesh_worker_join(tc);
    MVM_io_eventloop_join(tc);
    /* Allow MVM_io_eventloop_start to restart the thread if necessary */
    instance->event_loop_thread = NULL;

    /* Do not mark thread blocked as the GC also tries to acquire
     * mutex_threads and it's held only briefly by all holders anyway */
    uv_mutex_lock(&instance->mutex_threads);

    /* Check if we are single threaded and if true, fork() */
    if (MVM_thread_cleanup_threads_list(tc, &instance->threads) == 1) {
        pid = MVM_platform_fork(tc);
    } else {
        error = "Program has more than one active thread";
    }

    if (pid == 0 && instance->event_loop) {
        /* Reinitialize uv_loop_t after fork in child */
        uv_loop_fork(instance->event_loop);
    }

    /* Release the thread lock, otherwise we can't start them */
    uv_mutex_unlock(&instance->mutex_threads);
    /* Without the mutex_event_loop being held, this might race */
    MVM_spesh_worker_start(tc);

    /* However, locks are nonrecursive, so unlocking is needed prior to
     * restarting the event loop */
    uv_mutex_unlock(&instance->mutex_event_loop);
    if (instance->event_loop)
        MVM_io_eventloop_start(tc);

    if (error != NULL)
        MVM_exception_throw_adhoc(tc, "fork() failed: %s\n", error);

    return pid;

}

void MVM_proc_pty_spawn(char *prog, char *argv[]) {
    // When this is called, we can assume that the STDIO handles have already all been
    // mapped to the pseudo TTY FD.

    // Close the PTY FD which we - as the child - have no business with.
    int fd_pty = atoi(prog);
    prog += 6;
    close(fd_pty);

    // Put ourself into a new session and process group, making us session
    // and process group leader.
    int sid = setsid();
    if (sid < 0) {
        fprintf(stderr, "Error in setsid: %s\n", strerror(errno));
        exit(1);
    }

    // Make our dear terminal the controlling terminal.
    int ret = ioctl(STDIN_FILENO, TIOCSCTTY);
    if (ret < 0) {
        fprintf(stderr, "Error in ioctl TIOCSCTTY: %s\n", strerror(errno));
        exit(1);
    }

    // Does not return.
    execvp(prog, argv);

    fprintf(stderr, "Spawn helper failed to spawn process %s: %s (error code %i)\n",
            prog, strerror(errno), errno);
}
