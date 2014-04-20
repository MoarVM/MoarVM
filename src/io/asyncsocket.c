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

/* Info we convey about a read task. */
typedef struct {
    MVMOSHandle      *handle;
    MVMDecodeStream  *ds;
    int               seq_number;
    MVMThreadContext *tc;
    int               work_idx;
} ReadInfo;

/* Allocates a buffer of the suggested size. */
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    size_t size = suggested_size > 0 ? suggested_size : 4;
    buf->base   = malloc(size);
    buf->len    = size;
}

/* Read handler. */
static void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    ReadInfo         *ri  = (ReadInfo *)handle->data;
    MVMThreadContext *tc  = ri->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = (MVMAsyncTask *)MVM_repr_at_pos_o(tc,
        tc->instance->event_loop_active, ri->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (nread > 0) {
        MVMROOT(tc, t, {
        MVMROOT(tc, arr, {
            /* Push the sequence number. */
            MVMObject *seq_boxed = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, ri->seq_number++);
            MVM_repr_push_o(tc, arr, seq_boxed);

            /* Either need to produce a buffer or decode characters. */
            if (ri->ds) {
                MVMString *str;
                MVMObject *boxed_str;
                MVM_string_decodestream_add_bytes(tc, ri->ds, buf->base, nread);
                str = MVM_string_decodestream_get_all(tc, ri->ds);
                boxed_str = MVM_repr_box_str(tc, tc->instance->boot_types.BOOTStr, str);
                MVM_repr_push_o(tc, arr, boxed_str);
            }
            else {
                MVM_panic(1, "socket buf creation NYI\n");
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

/* Does setup work for setting up asynchronous reads. */
static void read_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncSocketData *handle_data;
    int                   r;

    /* Add to work in progress. */
    ReadInfo *ri  = (ReadInfo *)data;
    ri->tc        = tc;
    ri->work_idx  = MVM_repr_elems(tc, tc->instance->event_loop_active);
    MVM_repr_push_o(tc, tc->instance->event_loop_active, async_task);

    /* Start reading the stream. */
    handle_data = (MVMIOAsyncSocketData *)ri->handle->body.data;
    handle_data->handle->data = data;
    if ((r = uv_read_start(handle_data->handle, on_alloc, on_read)) < 0) {
        /* Error; need to notify. */
        MVMROOT(tc, async_task, {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask *t   = (MVMAsyncTask *)async_task;
            MVM_repr_push_o(tc, arr, t->body.schedulee);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
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

/* Marks objects for a read task. */
void read_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    ReadInfo *ri = (ReadInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &ri->handle);
}

/* Frees info for a read task. */
static void read_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        ReadInfo *ri = (ReadInfo *)data;
        if (ri->ds)
            MVM_string_decodestream_destory(tc, ri->ds);
        free(data);
    }
}

/* Operations table for async read task. */
static const MVMAsyncTaskOps read_op_table = {
    read_setup,
    read_gc_mark,
    read_gc_free
};

static MVMAsyncTask * read_chars(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                 MVMObject *schedulee, MVMObject *async_type) {
    MVMAsyncTask *task;
    ReadInfo    *ri;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncreadchars target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncreadchars result type must have REPR AsyncTask");

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &read_op_table;
    ri              = calloc(1, sizeof(ReadInfo));
    ri->ds          = MVM_string_decodestream_create(tc, MVM_encoding_type_utf8, 0);
    MVM_ASSIGN_REF(tc, &(task->common.header), ri->handle, h);
    task->body.data = ri;

    /* Hand the task off to the event loop. */
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return task;
}

static MVMAsyncTask * read_bytes(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                 MVMObject *schedulee, MVMObject *buf_type, MVMObject *async_type) {
    MVM_exception_throw_adhoc(tc, "NYI");
}

/* Info we convey about a write task. */
typedef struct {
    MVMOSHandle      *handle;
    MVMString        *str_data;
    MVMObject        *buf_data;
    uv_write_t       *req;
    uv_buf_t          buf;
    MVMThreadContext *tc;
    int               work_idx;
} WriteInfo;

/* Completion handler for an asynchronous write. */
static void on_write(uv_write_t *req, int status) {
    WriteInfo        *wi  = (WriteInfo *)req->data;
    MVMThreadContext *tc  = wi->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = (MVMAsyncTask *)MVM_repr_at_pos_o(tc,
        tc->instance->event_loop_active, wi->work_idx);
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
        free(wi->buf.base);
    free(wi->req);
}

/* Does setup work for an asynchronous write. */
static void write_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncSocketData *handle_data;
    char                 *output;
    int                   output_size, r;

    /* Add to work in progress. */
    WriteInfo *wi = (WriteInfo *)data;
    wi->tc        = tc;
    wi->work_idx  = MVM_repr_elems(tc, tc->instance->event_loop_active);
    MVM_repr_push_o(tc, tc->instance->event_loop_active, async_task);

    /* Encode the string, or extract buf data. */
    if (wi->str_data) {
        MVMuint64 output_size_64;
        output = MVM_string_utf8_encode(tc, wi->str_data, &output_size_64);
        output_size = (int)output_size_64;
    }
    else {
        MVMArray *buffer = (MVMArray *)wi->buf_data;
        output = buffer->body.slots.i8 + buffer->body.start;
        output_size = (int)buffer->body.elems;
    }

    /* Create and initialize write request. */
    wi->req           = malloc(sizeof(uv_write_t));
    wi->buf           = uv_buf_init(output, output_size);
    wi->req->data     = data;
    handle_data       = (MVMIOAsyncSocketData *)wi->handle->body.data;
    if ((r = uv_write(wi->req, handle_data->handle, &(wi->buf), 1, on_write)) < 0) {
        /* Error; need to notify. */
        MVMROOT(tc, async_task, {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask *t   = (MVMAsyncTask *)async_task;
            MVM_repr_push_o(tc, arr, t->body.schedulee);
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

        /* Cleanup handle. */
        free(wi->req);
        wi->req = NULL;
    }
}

/* Marks objects for a write task. */
void write_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    WriteInfo *wi = (WriteInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &wi->handle);
    MVM_gc_worklist_add(tc, worklist, &wi->str_data);
    MVM_gc_worklist_add(tc, worklist, &wi->buf_data);
}

/* Frees info for a write task. */
static void write_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data)
        free(data);
}

/* Operations table for async write task. */
static const MVMAsyncTaskOps write_op_table = {
    write_setup,
    write_gc_mark,
    write_gc_free
};

static MVMAsyncTask * write_str(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                MVMObject *schedulee, MVMString *s, MVMObject *async_type) {
    MVMAsyncTask *task;
    WriteInfo    *wi;

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
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &write_op_table;
    wi              = calloc(1, sizeof(WriteInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->str_data, s);
    task->body.data = wi;

    /* Hand the task off to the event loop. */
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return task;
}

static MVMAsyncTask * write_bytes(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                  MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type) {
    MVMAsyncTask *task;
    WriteInfo    *wi;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncwritebytes target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncwritebytes result type must have REPR AsyncTask");
    if (!IS_CONCRETE(buffer) || REPR(buffer)->ID != MVM_REPR_ID_MVMArray)
        MVM_exception_throw_adhoc(tc, "asyncwritebytes requires a native array to read from");
    if (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_U8
        && ((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "asyncwritebytes requires a native array of uint8 or int8");

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &write_op_table;
    wi              = calloc(1, sizeof(WriteInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->buf_data, buffer);
    task->body.data = wi;

    /* Hand the task off to the event loop. */
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return task;
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

    /* Hand the task off to the event loop. */
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return (MVMObject *)task;
}

/* Info we convey about a socket listen task. */
typedef struct {
    struct sockaddr  *dest;
    uv_tcp_t         *socket;
    MVMThreadContext *tc;
    int               work_idx;
} ListenInfo;

/* Handles an incoming connection. */
static void on_connection(uv_stream_t *server, int status) {
    ListenInfo       *li     = (ListenInfo *)server->data;
    MVMThreadContext *tc     = li->tc;
    MVMObject        *arr    = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t      = (MVMAsyncTask *)MVM_repr_at_pos_o(tc,
        tc->instance->event_loop_active, li->work_idx);

    uv_tcp_t         *client = malloc(sizeof(uv_tcp_t));
    int               r;
    uv_tcp_init(tc->loop, client);

    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if ((r = uv_accept(server, (uv_stream_t *)client)) == 0) {
        /* Allocate and set up handle. */
        MVMOSHandle          *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
        MVMIOAsyncSocketData *data   = calloc(1, sizeof(MVMIOAsyncSocketData));
        data->handle                 = (uv_stream_t *)client;
        result->body.ops             = &op_table;
        result->body.data            = data;
        MVM_repr_push_o(tc, arr, (MVMObject *)result);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    }
    else {
        uv_close((uv_handle_t*)client, NULL);
        free(client);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
        MVMROOT(tc, arr, {
        MVMROOT(tc, t, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(r));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        });
        });
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
}

/* Sets up a socket listener. */
static void listen_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    int r;

    /* Add to work in progress. */
    ListenInfo *li = (ListenInfo *)data;
    li->tc         = tc;
    li->work_idx   = MVM_repr_elems(tc, tc->instance->event_loop_active);
    MVM_repr_push_o(tc, tc->instance->event_loop_active, async_task);

    /* Create and initialize socket and connection. */
    li->socket        = malloc(sizeof(uv_tcp_t));
    li->socket->data  = data;
    if ((r = uv_tcp_init(loop, li->socket)) < 0 ||
        (r = uv_tcp_bind(li->socket, li->dest, 0)) < 0) {
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
        free(li->socket);
        li->socket = NULL;
        return;
    }

    /* Start listening. */
    uv_listen((uv_stream_t *)li->socket, 128, on_connection);
}

/* Frees info for a listen task. */
static void listen_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        ListenInfo *li = (ListenInfo *)data;
        if (li->dest)
            free(li->dest);
        free(li);
    }
}

/* Operations table for async listen task. */
static const MVMAsyncTaskOps listen_op_table = {
    listen_setup,
    NULL,
    listen_gc_free
};

/* Initiates an async socket listener. */
MVMObject * MVM_io_socket_listen_async(MVMThreadContext *tc, MVMObject *queue,
                                       MVMObject *schedulee, MVMString *host,
                                       MVMint64 port, MVMObject *async_type) {
    MVMAsyncTask *task;
    ListenInfo   *li;

    /* Resolve hostname. (Could be done asynchronously too.) */
    struct sockaddr *dest = MVM_io_resolve_host_name(tc, host, port);

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asynclisten target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asynclisten result type must have REPR AsyncTask");

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &listen_op_table;
    li              = calloc(1, sizeof(ListenInfo));
    li->dest        = dest;
    task->body.data = li;

    /* Hand the task off to the event loop. */
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return (MVMObject *)task;
}
