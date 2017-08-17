#include "moar.h"

/* Data that we keep for an asynchronous socket handle. */
typedef struct {
    /* The libuv handle to the socket. */
    uv_stream_t *handle;
} MVMIOAsyncSocketData;

/* Info we convey about a read task. */
typedef struct {
    MVMOSHandle      *handle;
    MVMObject        *buf_type;
    int               seq_number;
    MVMThreadContext *tc;
    int               work_idx;
} ReadInfo;

/* Allocates a buffer of the suggested size. */
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    size_t size = suggested_size > 0 ? suggested_size : 4;
    buf->base   = MVM_malloc(size);
    buf->len    = size;
}

/* Callback used to simply free memory on close. */
static void free_on_close_cb(uv_handle_t *handle) {
    MVM_free(handle);
}

/* Read handler. */
static void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    ReadInfo         *ri  = (ReadInfo *)handle->data;
    MVMThreadContext *tc  = ri->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = MVM_io_eventloop_get_active_work(tc, ri->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (nread >= 0) {
        MVMROOT(tc, t, {
        MVMROOT(tc, arr, {
            MVMArray *res_buf;

            /* Push the sequence number. */
            MVMObject *seq_boxed = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, ri->seq_number++);
            MVM_repr_push_o(tc, arr, seq_boxed);

            /* Produce a buffer and push it. */
            res_buf      = (MVMArray *)MVM_repr_alloc_init(tc, ri->buf_type);
            res_buf->body.slots.i8 = (MVMint8 *)buf->base;
            res_buf->body.start    = 0;
            res_buf->body.ssize    = buf->len;
            res_buf->body.elems    = nread;
            MVM_repr_push_o(tc, arr, (MVMObject *)res_buf);

            /* Finally, no error. */
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        });
        });
    }
    else if (nread == UV_EOF) {
        MVMROOT(tc, t, {
        MVMROOT(tc, arr, {
            MVMObject *final = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, ri->seq_number);
            MVM_repr_push_o(tc, arr, final);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        });
        });
        if (buf->base)
            MVM_free(buf->base);
        uv_read_stop(handle);
        MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
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
        uv_read_stop(handle);
        MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
}

/* Does setup work for setting up asynchronous reads. */
static void read_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncSocketData *handle_data;
    ReadInfo             *ri;
    int                   r;

    /* Ensure not closed. */
    ri = (ReadInfo *)data;
    handle_data = (MVMIOAsyncSocketData *)ri->handle->body.data;
    if (!handle_data->handle || uv_is_closing((uv_handle_t *)handle_data->handle)) {
        /* Closed, so immediately send done. */
        MVMAsyncTask *t = (MVMAsyncTask *)async_task;
        MVMROOT(tc, t, {
            MVMObject *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVM_repr_push_o(tc, arr, t->body.schedulee);
            MVMROOT(tc, arr, {
                MVMObject *final = MVM_repr_box_int(tc,
                    tc->instance->boot_types.BOOTInt, ri->seq_number);
                MVM_repr_push_o(tc, arr, final);
            });
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, t->body.queue, arr);
        });
        return;
    }

    /* Add to work in progress. */
    ri->tc        = tc;
    ri->work_idx  = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Start reading the stream. */
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
        MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
    }
}

/* Stops reading. */
static void read_cancel(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    ReadInfo *ri = (ReadInfo *)data;
    if (ri->work_idx >= 0) {
        MVMIOAsyncSocketData *handle_data = (MVMIOAsyncSocketData *)ri->handle->body.data;
        if (handle_data->handle && !uv_is_closing((uv_handle_t *)handle_data->handle))
            uv_read_stop(handle_data->handle);
        MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
    }
}

/* Marks objects for a read task. */
static void read_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    ReadInfo *ri = (ReadInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &ri->buf_type);
    MVM_gc_worklist_add(tc, worklist, &ri->handle);
}

/* Frees info for a read task. */
static void read_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data)
        MVM_free(data);
}

/* Operations table for async read task. */
static const MVMAsyncTaskOps read_op_table = {
    read_setup,
    NULL,
    read_cancel,
    read_gc_mark,
    read_gc_free
};

static MVMAsyncTask * read_bytes(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                 MVMObject *schedulee, MVMObject *buf_type, MVMObject *async_type) {
    MVMAsyncTask *task;
    ReadInfo    *ri;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncreadbytes target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncreadbytes result type must have REPR AsyncTask");
    if (REPR(buf_type)->ID == MVM_REPR_ID_VMArray) {
        MVMint32 slot_type = ((MVMArrayREPRData *)STABLE(buf_type)->REPR_data)->slot_type;
        if (slot_type != MVM_ARRAY_U8 && slot_type != MVM_ARRAY_I8)
            MVM_exception_throw_adhoc(tc, "asyncreadbytes buffer type must be an array of uint8 or int8");
    }
    else {
        MVM_exception_throw_adhoc(tc, "asyncreadbytes buffer type must be an array");
    }

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
    MVMROOT(tc, h, {
    MVMROOT(tc, buf_type, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &read_op_table;
    ri              = MVM_calloc(1, sizeof(ReadInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), ri->buf_type, buf_type);
    MVM_ASSIGN_REF(tc, &(task->common.header), ri->handle, h);
    task->body.data = ri;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return task;
}

/* Info we convey about a write task. */
typedef struct {
    MVMOSHandle      *handle;
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
    MVM_free(wi->req);
    MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
}

/* Does setup work for an asynchronous write. */
static void write_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncSocketData *handle_data;
    MVMArray             *buffer;
    WriteInfo            *wi;
    char                 *output;
    int                   output_size, r;

    /* Ensure not closed. */
    wi = (WriteInfo *)data;
    handle_data = (MVMIOAsyncSocketData *)wi->handle->body.data;
    if (!handle_data->handle || uv_is_closing((uv_handle_t *)handle_data->handle)) {
        MVMROOT(tc, async_task, {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask *t   = (MVMAsyncTask *)async_task;
            MVM_repr_push_o(tc, arr, t->body.schedulee);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVMROOT(tc, arr, {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, "Cannot write to a closed socket");
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            });
            MVM_repr_push_o(tc, t->body.queue, arr);
        });
        return;
    }

    /* Add to work in progress. */
    wi->tc = tc;
    wi->work_idx = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Extract buf data. */
    buffer = (MVMArray *)wi->buf_data;
    output = (char *)(buffer->body.slots.i8 + buffer->body.start);
    output_size = (int)buffer->body.elems;

    /* Create and initialize write request. */
    wi->req           = MVM_malloc(sizeof(uv_write_t));
    wi->buf           = uv_buf_init(output, output_size);
    wi->req->data     = data;

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
        MVM_free(wi->req);
        wi->req = NULL;
        MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
    }
}

/* Marks objects for a write task. */
static void write_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    WriteInfo *wi = (WriteInfo *)data;
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
    WriteInfo    *wi;

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
    wi              = MVM_calloc(1, sizeof(WriteInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->buf_data, buffer);
    task->body.data = wi;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return task;
}

/* Info we convey about a socket close task. */
typedef struct {
    MVMOSHandle *handle;
} CloseInfo;

/* Does an asynchronous close (since it must run on the event loop). */
static void close_perform(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    CloseInfo *ci = (CloseInfo *)data;
    MVMIOAsyncSocketData *handle_data = (MVMIOAsyncSocketData *)ci->handle->body.data;
    uv_handle_t *handle = (uv_handle_t *)handle_data->handle;
    if (handle && !uv_is_closing(handle)) {
        handle_data->handle = NULL;
        uv_close(handle, free_on_close_cb);
    }
}

/* Marks objects for a close task. */
static void close_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    CloseInfo *ci = (CloseInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &ci->handle);
}

/* Frees info for a close task. */
static void close_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data)
        MVM_free(data);
}

/* Operations table for async close task. */
static const MVMAsyncTaskOps close_op_table = {
    close_perform,
    NULL,
    NULL,
    close_gc_mark,
    close_gc_free
};

static MVMint64 close_socket(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMAsyncTask *task;
    CloseInfo *ci;

    MVMROOT(tc, h, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc,
            tc->instance->boot_types.BOOTAsync);
    });
    task->body.ops = &close_op_table;
    ci = MVM_calloc(1, sizeof(CloseInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), ci->handle, h);
    task->body.data = ci;
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return 0;
}

/* IO ops table, populated with functions. */
static const MVMIOClosable      closable       = { close_socket };
static const MVMIOAsyncReadable async_readable = { read_bytes };
static const MVMIOAsyncWritable async_writable = { write_bytes };
static const MVMIOOps op_table = {
    &closable,
    NULL,
    NULL,
    &async_readable,
    &async_writable,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static void push_name_and_port(MVMThreadContext *tc, struct sockaddr_storage *name, MVMObject *arr) {
    char addrstr[INET6_ADDRSTRLEN + 1];
    /* XXX windows support kludge. 64 bit is much too big, but we'll
     * get the proper data from the struct anyway, however windows
     * decides to declare it. */
    MVMuint64 port;
    MVMObject *host_o;
    MVMObject *port_o;
    switch (name->ss_family) {
        case AF_INET6: {
            uv_ip6_name((struct sockaddr_in6*)name, addrstr, INET6_ADDRSTRLEN + 1);
            port = ((struct sockaddr_in6*)name)->sin6_port;
            break;
        }
        case AF_INET: {
            uv_ip4_name((struct sockaddr_in*)name, addrstr, INET6_ADDRSTRLEN + 1);
            port = ((struct sockaddr_in*)name)->sin_port;
            break;
        }
        default:
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            return;
            break;
    }
    MVMROOT(tc, arr, {
        port_o = MVM_repr_box_int(tc, tc->instance->boot_types.BOOTInt, port);
        MVMROOT(tc, port, {
            host_o = (MVMObject *)MVM_repr_box_str(tc, tc->instance->boot_types.BOOTStr,
                    MVM_string_ascii_decode_nt(tc, tc->instance->VMString, addrstr));
        });
    });
    MVM_repr_push_o(tc, arr, host_o);
    MVM_repr_push_o(tc, arr, port_o);
}

/* Info we convey about a connection attempt task. */
typedef struct {
    struct sockaddr  *dest;
    uv_tcp_t         *socket;
    uv_connect_t     *connect;
    MVMThreadContext *tc;
    int               work_idx;
} ConnectInfo;

/* When a connection takes place, need to send result. */
static void on_connect(uv_connect_t* req, int status) {
    ConnectInfo      *ci  = (ConnectInfo *)req->data;
    MVMThreadContext *tc  = ci->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = MVM_io_eventloop_get_active_work(tc, ci->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (status >= 0) {
        /* Allocate and set up handle. */
        MVMROOT(tc, arr, {
        MVMROOT(tc, t, {
            MVMOSHandle          *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
            MVMIOAsyncSocketData *data   = MVM_calloc(1, sizeof(MVMIOAsyncSocketData));
            data->handle                 = (uv_stream_t *)ci->socket;
            result->body.ops             = &op_table;
            result->body.data            = data;
            MVM_repr_push_o(tc, arr, (MVMObject *)result);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);

            MVMROOT(tc, result, {
                struct sockaddr_storage sockaddr;
                int name_len = sizeof(struct sockaddr_storage);

                uv_tcp_getpeername(ci->socket, (struct sockaddr *)&sockaddr, &name_len);
                push_name_and_port(tc, &sockaddr, arr);

                uv_tcp_getsockname(ci->socket, (struct sockaddr *)&sockaddr, &name_len);
                push_name_and_port(tc, &sockaddr, arr);
            });
        });
        });
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
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        });
        });
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
    MVM_free(req);
    MVM_io_eventloop_remove_active_work(tc, &(ci->work_idx));
}

/* Initilalize the connection on the event loop. */
static void connect_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    int r;

    /* Add to work in progress. */
    ConnectInfo *ci = (ConnectInfo *)data;
    ci->tc        = tc;
    ci->work_idx  = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Create and initialize socket and connection. */
    ci->socket        = MVM_malloc(sizeof(uv_tcp_t));
    ci->connect       = MVM_malloc(sizeof(uv_connect_t));
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
        MVM_free(ci->connect);
        ci->connect = NULL;
        uv_close((uv_handle_t *)ci->socket, free_on_close_cb);
        ci->socket = NULL;
        MVM_io_eventloop_remove_active_work(tc, &(ci->work_idx));
    }
}

/* Frees info for a connection task. */
static void connect_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        ConnectInfo *ci = (ConnectInfo *)data;
        if (ci->dest)
            MVM_free(ci->dest);
        MVM_free(ci);
    }
}

/* Operations table for async connect task. */
static const MVMAsyncTaskOps connect_op_table = {
    connect_setup,
    NULL,
    NULL,
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
    ci              = MVM_calloc(1, sizeof(ConnectInfo));
    ci->dest        = dest;
    task->body.data = ci;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return (MVMObject *)task;
}

/* Info we convey about a socket listen task. */
typedef struct {
    struct sockaddr  *dest;
    uv_tcp_t         *socket;
    MVMThreadContext *tc;
    int               work_idx;
    int               backlog;
} ListenInfo;


/* Handles an incoming connection. */
static void on_connection(uv_stream_t *server, int status) {
    ListenInfo       *li     = (ListenInfo *)server->data;
    MVMThreadContext *tc     = li->tc;
    MVMObject        *arr    = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t      = MVM_io_eventloop_get_active_work(tc, li->work_idx);

    uv_tcp_t         *client = MVM_malloc(sizeof(uv_tcp_t));
    int               r;
    uv_tcp_init(tc->loop, client);

    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if ((r = uv_accept(server, (uv_stream_t *)client)) == 0) {
        /* Allocate and set up handle. */
        MVMROOT(tc, arr, {
        MVMROOT(tc, t, {
            MVMOSHandle          *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
            MVMIOAsyncSocketData *data   = MVM_calloc(1, sizeof(MVMIOAsyncSocketData));
            data->handle                 = (uv_stream_t *)client;
            result->body.ops             = &op_table;
            result->body.data            = data;

            MVM_repr_push_o(tc, arr, (MVMObject *)result);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);

            MVMROOT(tc, result, {
                struct sockaddr_storage sockaddr;
                int name_len = sizeof(struct sockaddr_storage);

                uv_tcp_getpeername(client, (struct sockaddr *)&sockaddr, &name_len);
                push_name_and_port(tc, &sockaddr, arr);

                uv_tcp_getsockname(client, (struct sockaddr *)&sockaddr, &name_len);
                push_name_and_port(tc, &sockaddr, arr);
            });
        });
        });
    }
    else {
        uv_close((uv_handle_t*)client, NULL);
        MVM_free(client);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
        MVMROOT(tc, arr, {
        MVMROOT(tc, t, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(r));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
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
    li->work_idx   = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Create and initialize socket and connection, and start listening. */
    li->socket        = MVM_malloc(sizeof(uv_tcp_t));
    li->socket->data  = data;
    if ((r = uv_tcp_init(loop, li->socket)) < 0 ||
        (r = uv_tcp_bind(li->socket, li->dest, 0)) < 0 ||
        (r = uv_listen((uv_stream_t *)li->socket, li->backlog, on_connection))) {
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
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            });
            MVM_repr_push_o(tc, t->body.queue, arr);
        });
        uv_close((uv_handle_t *)li->socket, free_on_close_cb);
        li->socket = NULL;
        MVM_io_eventloop_remove_active_work(tc, &(li->work_idx));
        return;
    }
}

/* Stops listening. */
static void on_listen_cancelled(uv_handle_t *handle) {
    ListenInfo       *li = (ListenInfo *)handle->data;
    MVMThreadContext *tc = li->tc;
    MVM_io_eventloop_send_cancellation_notification(tc,
        MVM_io_eventloop_get_active_work(tc, li->work_idx));
    MVM_io_eventloop_remove_active_work(tc, &(li->work_idx));
}
static void listen_cancel(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    ListenInfo *li = (ListenInfo *)data;
    if (li->socket) {
        uv_close((uv_handle_t *)li->socket, on_listen_cancelled);
        li->socket = NULL;
    }
}

/* Frees info for a listen task. */
static void listen_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        ListenInfo *li = (ListenInfo *)data;
        if (li->dest)
            MVM_free(li->dest);
        MVM_free(li);
    }
}

/* Operations table for async listen task. */
static const MVMAsyncTaskOps listen_op_table = {
    listen_setup,
    NULL,
    listen_cancel,
    NULL,
    listen_gc_free
};

/* Initiates an async socket listener. */
MVMObject * MVM_io_socket_listen_async(MVMThreadContext *tc, MVMObject *queue,
                                       MVMObject *schedulee, MVMString *host,
                                       MVMint64 port, MVMint32 backlog, MVMObject *async_type) {
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
    li              = MVM_calloc(1, sizeof(ListenInfo));
    li->dest        = dest;
    li->backlog     = backlog;
    task->body.data = li;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return (MVMObject *)task;
}
