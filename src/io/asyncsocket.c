#include "moar.h"

/* Number of bytes we accept per read. */
#define CHUNK_SIZE 65536

/* Data that we keep for an asynchronous socket handle. */
typedef struct {
    /* The libuv handle to the socket. */
    uv_stream_t *handle;

    /* Decode stream, for turning bytes into strings. */
    MVMDecodeStream *ds;
} MVMIOAsyncSocketData;

static MVMAsyncTask * read_chars(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                 MVMObject *schedulee, MVMObject *async_type) {
    MVM_exception_throw_adhoc(tc, "NYI");
}

static MVMAsyncTask * read_bytes(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                 MVMObject *schedulee, MVMObject *buf_type, MVMObject *async_type) {
    MVM_exception_throw_adhoc(tc, "NYI");
}

static MVMAsyncTask * write_str(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                MVMObject *schedulee, MVMString *s, MVMObject *async_type) {
    MVM_exception_throw_adhoc(tc, "NYI");
}

static MVMAsyncTask * write_bytes(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                  MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type) {
    MVM_exception_throw_adhoc(tc, "NYI");
}

static void do_close(MVMThreadContext *tc, MVMIOAsyncSocketData *data) {
    if (data->handle) {
         uv_close((uv_handle_t *)data->handle, NULL);
         data->handle = NULL;
    }
    if (data->ds) {
        MVM_string_decodestream_destory(tc, data->ds);
        data->ds = NULL;
    }
}
static void close_socket(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncSocketData *data = (MVMIOAsyncSocketData *)h->body.data;
    do_close(tc, data);
}

static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIOAsyncSocketData *data = (MVMIOAsyncSocketData *)d;
    do_close(tc, data);
}

/* IO ops table, populated with functions. */
static const MVMIOClosable      closable       = { close_socket };
static const MVMIOAsyncReadable async_readable = { read_chars, read_bytes };
static const MVMIOAsyncWritable async_writable = { write_str, write_bytes };
static const MVMIOOps op_table = {
    &closable,
    NULL,
    NULL,
    NULL,
    &async_readable,
    &async_writable,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    gc_free
};

/* Info we convey about a connection attempt task. */
typedef struct {
    struct sockaddr  *dest;
    uv_tcp_t         *socket;
    uv_connect_t     *connect;
    MVMThreadContext *tc;
    int               work_idx;
} ConnectInfo;

/* When a connection takes place, need to keep the appropriate promise. */
static void on_connect(uv_connect_t* req, int status) {
    ConnectInfo      *ci  = (ConnectInfo *)req->data;
    MVMThreadContext *tc  = ci->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = (MVMAsyncTask *)MVM_repr_at_pos_o(tc,
        tc->instance->event_loop_active, ci->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (status >= 0) {
        /* Allocate and set up handle. */
        MVMOSHandle          *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
        MVMIOAsyncSocketData *data   = calloc(1, sizeof(MVMIOAsyncSocketData));
        data->handle                 = (uv_stream_t *)ci->socket;
        result->body.ops             = &op_table;
        result->body.data            = data;
        MVM_repr_push_o(tc, arr, (MVMObject *)result);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    }
    else {
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
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
    free(req);
}

/* Initilalize the connection on the event loop. */
static void connect_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    int r;

    /* Add to work in progress. */
    ConnectInfo *ci = (ConnectInfo *)data;
    ci->tc        = tc;
    ci->work_idx  = MVM_repr_elems(tc, tc->instance->event_loop_active);
    MVM_repr_push_o(tc, tc->instance->event_loop_active, async_task);

    /* Create and initialize socket and connection. */
    ci->socket        = malloc(sizeof(uv_tcp_t));
    ci->connect       = malloc(sizeof(uv_connect_t));
    ci->connect->data = data;
    if ((r = uv_tcp_init(loop, ci->socket)) < 0 ||
        (r = uv_tcp_connect(ci->connect, ci->socket, ci->dest, on_connect)) < 0) {
        /* Error; need to notify. */
        MVMROOT(tc, async_task, {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask *t   = (MVMAsyncTask *)async_task;
            MVM_repr_push_o(tc, arr, t->body.schedulee);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
            MVMROOT(tc, arr, {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, uv_strerror(r));
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            });
            MVM_repr_push_o(tc, t->body.queue, arr);
        });

        /* Cleanup handles. */
        free(ci->socket);
        ci->socket = NULL;
        free(ci->connect);
        ci->connect = NULL;
    }
}

/* Frees info for a connection task. */
static void connect_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        ConnectInfo *ci = (ConnectInfo *)data;
        if (ci->dest)
            free(ci->dest);
        free(ci);
    }
}

/* Operations table for async connect task. */
static const MVMAsyncTaskOps connect_op_table = {
    connect_setup,
    NULL,
    connect_gc_free
};

/* Sets off an asynchronous socket connection. */
MVMObject * MVM_io_socket_connect_async(MVMThreadContext *tc, MVMObject *queue,
                                        MVMObject *schedulee, MVMString *host,
                                        MVMint64 port, MVMObject *async_type) {
    MVMAsyncTask *task;
    ConnectInfo  *ci;

    /* Resolve hostname. (Could be done asynchronously too.) */
    struct sockaddr *dest = MVM_io_resolve_host_name(tc, host, port);

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncconnect target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncconnect result type must have REPR AsyncTask");

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &connect_op_table;
    ci              = calloc(1, sizeof(ConnectInfo));
    ci->dest        = dest;
    task->body.data = ci;

    /* Hand the task off to the event loop, which will set up the timer on the
     * event loop. */
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return (MVMObject *)task;
}

MVMObject * MVM_io_socket_listen_async(MVMThreadContext *tc, MVMObject *queue,
                                       MVMObject *schedulee, MVMString *host,
                                       MVMint64 port, MVMObject *async_type) {
    MVM_exception_throw_adhoc(tc, "async listen NYI");
}
