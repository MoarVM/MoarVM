#include "moar.h"

/* Info we convey about a file watcher. */
typedef struct {
    char             *path;
    uv_fs_event_t     handle;
    MVMThreadContext *tc;
    int               work_idx;
} WatchInfo;

static void on_changed(uv_fs_event_t *handle, const char *filename, int events, int status) {
    WatchInfo        *wi  = (WatchInfo *)handle->data;
    MVMThreadContext *tc  = wi->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = (MVMAsyncTask *)MVM_repr_at_pos_o(tc,
        tc->instance->event_loop_active, wi->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVMROOT(tc, t, {
    MVMROOT(tc, arr, {
        MVMObject *filename_boxed;
        MVMObject *rename_boxed;
        if (filename) {
            MVMString *filename_str = MVM_string_utf8_decode(tc,
                tc->instance->VMString, filename, strlen(filename));
            filename_boxed = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr,
                filename_str);
        }
        else {
            filename_boxed = tc->instance->boot_types.BOOTStr;
        }
        MVM_repr_push_o(tc, arr, filename_boxed);
        rename_boxed = MVM_repr_box_int(tc,
            tc->instance->boot_types.BOOTInt,
            events == UV_RENAME ? 1 : 0);
        MVM_repr_push_o(tc, arr, rename_boxed);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    });
    });
    MVM_repr_push_o(tc, t->body.queue, arr);
}

/* Sets the signal handler up on the event loop. */
static void setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    WatchInfo *wi = (WatchInfo *)data;
    int        r;

    /* Add task to active list. */
    wi->work_idx    = MVM_repr_elems(tc, tc->instance->event_loop_active);
    wi->tc          = tc;
    wi->handle.data = wi;
    MVM_repr_push_o(tc, tc->instance->event_loop_active, async_task);

    /* Start watching. */
    uv_fs_event_init(loop, &wi->handle);
    if ((r = uv_fs_event_start(&wi->handle, on_changed, wi->path, 0)) != 0) {
        /* Error; need to notify. */
        MVMROOT(tc, async_task, {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask *t   = (MVMAsyncTask *)async_task;
            MVM_repr_push_o(tc, arr, t->body.schedulee);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVMROOT(tc, arr, {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, uv_strerror(r));
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            });
            MVM_repr_push_o(tc, t->body.queue, arr);
        });
    }
}

/* Frees data associated with a file watcher task. */
static void gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data)
        MVM_free(data);
}

/* Operations table for a file watcher task. */
static const MVMAsyncTaskOps op_table = {
    setup,
    NULL,
    NULL,
    gc_free
};

MVMObject * MVM_io_file_watch(MVMThreadContext *tc, MVMObject *queue,
                              MVMObject *schedulee, MVMString *path,
                              MVMObject *async_type) {
    MVMAsyncTask *task;
    WatchInfo    *watch_info;

    /* Encode path. */
    char *c_path = MVM_string_utf8_encode_C_string(tc, path);

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "file watch target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "file watch result type must have REPR AsyncTask");

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops   = &op_table;
    watch_info       = MVM_malloc(sizeof(WatchInfo));
    watch_info->path = c_path;
    task->body.data  = watch_info;

    /* Hand the task off to the event loop. */
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return (MVMObject *)task;
}
