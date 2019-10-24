#include "moar.h"

/* Data that we keep for an asynchronous socket handle. */
typedef struct {
    /* The libuv handle to the socket. */
    uv_stream_t *handle;
} MVMIOAsyncSocketData;

/* Info we convey about a read task. */
typedef struct {
    MVMThreadContext *tc;
    int               work_idx;
    MVMOSHandle      *handle;
    MVMObject        *buf_type;
    int               seq_number;
    int               error;
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
    ReadInfo         *ri   = (ReadInfo *)handle->data;
    MVMThreadContext *tc   = ri->tc;
    MVMAsyncTask     *t    = MVM_io_eventloop_get_active_work(tc, ri->work_idx);
    MVMObject        *arr;

    if (nread >= 0) {
        MVMROOT3(tc, t, ri->handle, ri->buf_type, {
            arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        });
        MVM_repr_push_o(tc, arr, t->body.schedulee);
        MVMROOT4(tc, t, arr, ri->handle, ri->buf_type, {
            /* Push the sequence number, produce a buffer, and push that as
             * well. */
            MVMObject *seq_boxed   = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, ri->seq_number++);
            MVMArray  *res_buf     = (MVMArray *)MVM_repr_alloc_init(tc, ri->buf_type);
            res_buf->body.slots.i8 = (MVMint8 *)buf->base;
            res_buf->body.start    = 0;
            res_buf->body.ssize    = buf->len;
            res_buf->body.elems    = nread;
            MVM_repr_push_o(tc, arr, seq_boxed);
            MVM_repr_push_o(tc, arr, (MVMObject *)res_buf);
        });
        /* Finally, no error. */
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        MVM_repr_push_o(tc, t->body.queue, arr);
    }
    else {
        MVMIOAsyncSocketData *handle_data = (MVMIOAsyncSocketData *)ri->handle->body.data;
        uv_handle_t          *conn_handle = (uv_handle_t *)handle_data->handle;

        MVMROOT3(tc, t, ri->handle, ri->buf_type, {
            arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        });
        MVM_repr_push_o(tc, arr, t->body.schedulee);
        if (nread == UV_EOF) {
            /* End of file; push read count. */
            MVMROOT4(tc, t, arr, ri->handle, ri->buf_type, {
                MVMObject *final = MVM_repr_box_int(tc,
                    tc->instance->boot_types.BOOTInt, ri->seq_number);
                MVM_repr_push_o(tc, arr, final);
            });
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        }
        else {
            /* Error; need to notify. */
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVMROOT4(tc, t, arr, ri->handle, ri->buf_type, {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, uv_strerror(nread));
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            });
        }
        MVM_repr_push_o(tc, t->body.queue, arr);

        /* Clean up. */
        if (buf->base)
            MVM_free(buf->base);
        if (conn_handle && !uv_is_closing(conn_handle)) {
            uv_close(conn_handle, free_on_close_cb);
            handle_data->handle = NULL;
        }

        MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
    }
}

/* Does setup work for setting up asynchronous reads. */
static void read_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    ReadInfo             *ri          = (ReadInfo *)data;
    MVMIOAsyncSocketData *handle_data = (MVMIOAsyncSocketData *)ri->handle->body.data;
    uv_handle_t          *handle;
    MVMAsyncTask         *t           = (MVMAsyncTask *)async_task;
    MVMObject            *arr;

    /* Add to work in progress. */
    ri->tc       = tc;
    ri->work_idx = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Ensure not closed. */
    if (handle_data->handle == NULL)
        goto closed;

    /* Get our handle; set its data so we can access the ReadInfo struct in
     * on_read. */
    handle       = (uv_handle_t *)handle_data->handle;
    handle->data = data;

    /* Ensure not closed. */
    if (uv_is_closing(handle))
        goto closed;

    /* Start reading the stream. */
    if ((ri->error = uv_read_start(handle_data->handle, on_alloc, on_read)) == 0)
        /* Success; finish up in on_read. */
        return;

    /* Error; need to notify. */
    MVMROOT3(tc, t, ri->handle, ri->buf_type, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVMROOT4(tc, t, arr, ri->handle, ri->buf_type, {
        MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
            tc->instance->VMString, uv_strerror(ri->error));
        MVMObject *msg_box = MVM_repr_box_str(tc,
            tc->instance->boot_types.BOOTStr, msg_str);
        MVM_repr_push_o(tc, arr, msg_box);
    });
    MVM_repr_push_o(tc, t->body.queue, arr);
    MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
    return;

closed:
    /* Closed, so immediately send done. */
    MVMROOT3(tc, t, ri->handle, ri->buf_type, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVMROOT4(tc, t, arr, ri->handle, ri->buf_type, {
        MVMObject *final = MVM_repr_box_int(tc,
            tc->instance->boot_types.BOOTInt, ri->seq_number);
        MVM_repr_push_o(tc, arr, final);
    });
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVM_repr_push_o(tc, t->body.queue, arr);
    MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
    return;
}

/* Stops reading. */
static void read_cancel(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    ReadInfo *ri = (ReadInfo *)data;

    if (ri->work_idx >= 0) {
        MVMIOAsyncSocketData *handle_data = (MVMIOAsyncSocketData *)ri->handle->body.data;
        uv_handle_t          *handle      = (uv_handle_t *)handle_data->handle;
        if (handle != NULL && !uv_is_closing(handle))
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
    if (data != NULL)
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
    ReadInfo     *ri;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncreadbytes target queue must have ConcBlockingQueue REPR (got %s)",
             MVM_6model_get_stable_debug_name(tc, queue->st));
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
    MVMROOT5(tc, h, queue, schedulee, buf_type, async_type, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });

    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    ri              = MVM_calloc(1, sizeof(ReadInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), ri->buf_type, buf_type);
    MVM_ASSIGN_REF(tc, &(task->common.header), ri->handle, h);
    task->body.data = ri;
    task->body.ops  = &read_op_table;

    /* Hand the task off to the event loop. */
    MVMROOT6(tc, h, queue, schedulee, buf_type, async_type, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return task;
}

/* Info we convey about a write task. */
typedef struct {
    MVMThreadContext *tc;
    int               work_idx;
    MVMOSHandle      *handle;
    MVMObject        *buf_data;
    uv_write_t       *req;
    uv_buf_t          buf;
    int               error;
} WriteInfo;

/* Completion handler for an asynchronous write. */
static void on_write(uv_write_t *req, int status) {
    WriteInfo        *wi = (WriteInfo *)req->data;
    MVMThreadContext *tc = wi->tc;
    MVMAsyncTask     *t  = MVM_io_eventloop_get_active_work(tc, wi->work_idx);
    MVMROOT3(tc, t, wi->handle, wi->buf_data, {
        MVMObject *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        MVM_repr_push_o(tc, arr, t->body.schedulee);
        if (status == 0) {
            /* Success; push write length. */
            MVMROOT(tc, arr, {
                MVMObject *bytes_box = MVM_repr_box_int(tc,
                    tc->instance->boot_types.BOOTInt,
                    wi->buf.len);
                MVM_repr_push_o(tc, arr, bytes_box);
            });
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        }
        else {
            /* Error; need to notify. */
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVMROOT(tc, arr, {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, uv_strerror(status));
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            });
        }
        MVM_repr_push_o(tc, t->body.queue, arr);
    });
    MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
}

/* Does setup work for an asynchronous write. */
static void write_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    WriteInfo            *wi           = (WriteInfo *)data;
    MVMIOAsyncSocketData *handle_data  = (MVMIOAsyncSocketData *)wi->handle->body.data;
    uv_handle_t          *handle;
    MVMArray             *buffer;
    char                 *output;
    int                   output_size;
    MVMAsyncTask         *t            = (MVMAsyncTask *)async_task;
    MVMObject            *arr;

    /* Add to work in progress. */
    wi->tc       = tc;
    wi->work_idx = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Extract buf data. */
    buffer      = (MVMArray *)wi->buf_data;
    output      = (char *)(buffer->body.slots.i8 + buffer->body.start);
    output_size = (int)buffer->body.elems;

    /* Create and initialize write request. */
    wi->req       = MVM_malloc(sizeof(uv_write_t));
    wi->req->data = data;
    wi->buf       = uv_buf_init(output, output_size);

    /* Ensure not closed. */
    if (handle_data->handle == NULL)
        goto closed;

    /* Get our handle. */
    handle = (uv_handle_t *)handle_data->handle;

    /* Ensure not closed. */
    if (uv_is_closing(handle))
        goto closed;

    /* Do our write. */
    if ((wi->error = uv_write(wi->req, handle_data->handle, &(wi->buf), 1, on_write)) == 0)
        /* Success; finish up in on_write. */
        return;

    /* Error; need to notify. */
    MVMROOT3(tc, t, wi->handle, wi->buf_data, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVMROOT4(tc, t, arr, wi->handle, wi->buf_data, {
        MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
            tc->instance->VMString, uv_strerror(wi->error));
        MVMObject *msg_box = MVM_repr_box_str(tc,
            tc->instance->boot_types.BOOTStr, msg_str);
        MVM_repr_push_o(tc, arr, msg_box);
    });
    MVM_repr_push_o(tc, t->body.queue, arr);

    /* Clean up our handle. */
    if (handle != NULL && !uv_is_closing(handle)) {
        uv_close(handle, free_on_close_cb);
        handle_data->handle = NULL;
    }

    MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
    return;

closed:
    /* Handle closed; need to notify. */
    MVMROOT3(tc, t, wi->handle, wi->buf_data, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVMROOT4(tc, t, arr, wi->handle, wi->buf_data, {
        MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
            tc->instance->VMString, "Cannot write to a closed socket");
        MVMObject *msg_box = MVM_repr_box_str(tc,
            tc->instance->boot_types.BOOTStr, msg_str);
        MVM_repr_push_o(tc, arr, msg_box);
    });
    MVM_repr_push_o(tc, t->body.queue, arr);
    MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
    return;
}

/* Marks objects for a write task. */
static void write_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    WriteInfo *wi = (WriteInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &wi->handle);
    MVM_gc_worklist_add(tc, worklist, &wi->buf_data);
}

/* Frees info for a write task. */
static void write_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data != NULL) {
        WriteInfo *wi = (WriteInfo *)data;
        if (wi->req != NULL)
            MVM_free_null(wi->req);
        MVM_free(data);
    }
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
    MVMROOT5(tc, h, queue, schedulee, buffer, async_type, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });

    wi              = MVM_calloc(1, sizeof(WriteInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->buf_data, buffer);
    task->body.data = wi;
    task->body.ops  = &write_op_table;

    /* Hand the task off to the event loop. */
    MVMROOT6(tc, h, queue, schedulee, buffer, async_type, task, {
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
    CloseInfo            *ci          = (CloseInfo *)data;
    MVMIOAsyncSocketData *handle_data = (MVMIOAsyncSocketData *)ci->handle->body.data;
    uv_handle_t          *handle      = (uv_handle_t *)handle_data->handle;
    if (handle != NULL && !uv_is_closing(handle)) {
        uv_close(handle, free_on_close_cb);
        handle_data->handle = NULL;
    }
}

/* Marks objects for a close task. */
static void close_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    CloseInfo *ci = (CloseInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &ci->handle);
}

/* Frees info for a close task. */
static void close_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data != NULL)
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
    CloseInfo    *ci;

    MVMROOT(tc, h, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTAsync);
    });
    ci              = MVM_calloc(1, sizeof(CloseInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), ci->handle, h);
    task->body.data = ci;
    task->body.ops  = &close_op_table;
    MVMROOT2(tc, h, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return 0;
}

static MVMint64 socket_is_tty(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncSocketData *data   = (MVMIOAsyncSocketData *)h->body.data;
    uv_handle_t          *handle = (uv_handle_t *)data->handle;
    return (MVMint64)(handle->type == UV_TTY);
}

static MVMint64 socket_handle(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncSocketData *data   = (MVMIOAsyncSocketData *)h->body.data;
    uv_handle_t          *handle = (uv_handle_t *)data->handle;
    int                   fd;
    uv_os_fd_t            fh;

    uv_fileno(handle, &fh);
    fd = uv_open_osfhandle(fh);
    return (MVMint64)fd;
}

/* IO ops table, populated with functions. */
static const MVMIOClosable      closable       = { close_socket };
static const MVMIOAsyncReadable async_readable = { read_bytes };
static const MVMIOAsyncWritable async_writable = { write_bytes };
static const MVMIOIntrospection introspection  = { socket_is_tty,
                                                   socket_handle };
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
    &introspection,
    NULL,
    NULL,
    NULL
};

/* Note: every MVMObject from the function calling this that needs to be rooted
 * before allocating the host and port already is. */
static void push_name_and_port(MVMThreadContext *tc, struct sockaddr_storage *name, MVMObject *arr) {
    char      addrstr[INET6_ADDRSTRLEN + 1];
    /* XXX windows support kludge. 64 bit is much too big, but we'll
     * get the proper data from the struct anyway, however windows
     * decides to declare it. */
    MVMuint64 port;

    if (name == NULL)
        goto error;

    switch (name->ss_family) {
        case AF_INET6: {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)name;
            uv_ip6_name(addr, addrstr, INET6_ADDRSTRLEN + 1);
            port = ntohs(addr->sin6_port);
            break;
        }
        case AF_INET: {
            struct sockaddr_in *addr = (struct sockaddr_in *)name;
            uv_ip4_name(addr, addrstr, INET6_ADDRSTRLEN + 1);
            port = ntohs(addr->sin_port);
            break;
        }
        default:
            goto error;
    }

    MVMROOT(tc, arr, {
        MVMObject *host_o  = (MVMObject *)MVM_repr_box_str(tc, tc->instance->boot_types.BOOTStr,
                MVM_string_ascii_decode_nt(tc, tc->instance->VMString, addrstr));
        MVMObject *port_o;
        MVM_repr_push_o(tc, arr, host_o);
        MVMROOT(tc, host_o, {
            port_o = MVM_repr_box_int(tc, tc->instance->boot_types.BOOTInt, port);
        });
        MVM_repr_push_o(tc, arr, port_o);
    });
    return;

error:
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    return;
}

/* Info we convey about a connection attempt task. */
typedef struct {
    MVMThreadContext *tc;
    uv_loop_t        *loop;
    MVMObject        *async_task;
    int               work_idx;
    uv_tcp_t         *socket;
    uv_connect_t     *connect;
    struct addrinfo  *address_info;
    int               error;
} ConnectInfo;

/* When a connection takes place, need to send result. */
static void on_connect(uv_connect_t* req, int status) {
    ConnectInfo      *ci   = (ConnectInfo *)req->data;
    MVMThreadContext *tc   = ci->tc;
    MVMAsyncTask     *t    = MVM_io_eventloop_get_active_work(tc, ci->work_idx);
    MVMObject        *arr;

    MVMROOT(tc, t, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (status == 0) {
        /* Allocate and set up handle. */
        MVMROOT2(tc, t, arr, {
            MVMOSHandle          *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
            MVMIOAsyncSocketData *data   = MVM_calloc(1, sizeof(MVMIOAsyncSocketData));
            data->handle                 = (uv_stream_t *)ci->socket;
            result->body.ops             = &op_table;
            result->body.data            = data;
            MVM_repr_push_o(tc, arr, (MVMObject *)result);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);

            {
                struct sockaddr_storage name;
                int                     name_len  = sizeof(struct sockaddr_storage);

                uv_tcp_getpeername(ci->socket, (struct sockaddr *)&name, &name_len);
                push_name_and_port(tc, &name, arr);

                uv_tcp_getsockname(ci->socket, (struct sockaddr *)&name, &name_len);
                push_name_and_port(tc, &name, arr);
            }
        });
    }
    else {
        /* Error; need to notify. */
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
        MVMROOT2(tc, t, arr, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(status));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        });
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
    MVM_io_eventloop_remove_active_work(tc, &(ci->work_idx));
}

/* Does the actual work of making the connection. */
static void do_connect_setup(uv_handle_t *handle) {
    ConnectInfo      *ci   = (ConnectInfo *)handle->data;
    MVMThreadContext *tc   = ci->tc;
    MVMAsyncTask     *t    = (MVMAsyncTask *)ci->async_task;
    MVMObject        *arr;

    if ((ci->error = uv_tcp_init(ci->loop, ci->socket)) != 0)
        goto error;

    if ((ci->error = uv_tcp_connect(ci->connect, ci->socket, ci->address_info->ai_addr, on_connect)) != 0)
        goto error;

    /* Finish up in on_connect. */
    return;

error:
    /* Error; no addresses could be used, so we need to notify. */
    MVMROOT(tc, t, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
    MVMROOT2(tc, t, arr, {
        MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
            tc->instance->VMString, uv_strerror(ci->error));
        MVMObject *msg_box = MVM_repr_box_str(tc,
            tc->instance->boot_types.BOOTStr, msg_str);
        MVM_repr_push_o(tc, arr, msg_box);
    });
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVM_repr_push_o(tc, t->body.queue, arr);

    /* Clean up our handles. */
    if (ci->socket != NULL && !uv_is_closing(handle)) {
        uv_close(handle, free_on_close_cb);
        ci->socket = NULL;
    }
    MVM_free_null(ci->connect);

    MVM_io_eventloop_remove_active_work(tc, &(ci->work_idx));
}

/* Initilalize the connection on the event loop. */
static void connect_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    /* Add to work in progress. */
    ConnectInfo *ci = (ConnectInfo *)data;
    ci->tc          = tc;
    ci->loop        = loop;
    ci->async_task  = async_task;
    ci->work_idx    = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Create and initialize socket and connection. */
    ci->socket        = MVM_malloc(sizeof(uv_tcp_t));
    ci->socket->data  = data;
    ci->connect       = MVM_malloc(sizeof(uv_connect_t));
    ci->connect->data = data;

    do_connect_setup((uv_handle_t *)ci->socket);
}

/* Frees info for a connection task. */
static void connect_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        ConnectInfo *ci = (ConnectInfo *)data;
        if (ci->address_info != NULL)
            freeaddrinfo(ci->address_info);
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
    MVMAsyncTask    *task;
    struct addrinfo *address_info;
    ConnectInfo     *ci;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncconnect target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncconnect result type must have REPR AsyncTask");

    MVMROOT4(tc, queue, schedulee, host, async_type, {
        /* Resolve hostname. (Could be done asynchronously too.) */
        address_info = MVM_io_resolve_host_name(tc, host, port, SOCKET_FAMILY_UNSPEC, SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP, 0);
        /* Create async task handle. */
        task         = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });

    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue); MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    ci               = MVM_calloc(1, sizeof(ConnectInfo));
    ci->address_info = address_info;
    task->body.data  = ci;
    task->body.ops   = &connect_op_table;

    /* Hand the task off to the event loop. */
    MVMROOT5(tc, queue, schedulee, host, async_type, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return (MVMObject *)task;
}

/* Info we convey about a socket listen task. */
typedef struct {
    MVMThreadContext *tc;
    uv_loop_t        *loop;
    MVMObject        *async_task;
    int               work_idx;
    int               backlog;
    uv_tcp_t         *socket;
    struct addrinfo  *address_info;
    int               error;
} ListenInfo;

/* Handles an incoming connection. */
static void on_connection(uv_stream_t *server, int status) {
    ListenInfo       *li     = (ListenInfo *)server->data;
    MVMThreadContext *tc     = li->tc;
    MVMAsyncTask     *t      = MVM_io_eventloop_get_active_work(tc, li->work_idx);
    MVMObject        *arr;
    uv_tcp_t         *client = MVM_malloc(sizeof(uv_tcp_t));
    int               r;

    MVMROOT2(tc, t, li->async_task, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);

    if ((r = uv_tcp_init(server->loop, client)) == 0 &&
        (r = uv_accept(server, (uv_stream_t *)client)) == 0) {
        /* Allocate and set up handle. */
        MVMROOT3(tc, t, arr, li->async_task, {
            struct sockaddr_storage  name;
            int                      name_len = sizeof(struct sockaddr_storage);

            {
                MVMOSHandle          *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
                MVMIOAsyncSocketData *data   = MVM_calloc(1, sizeof(MVMIOAsyncSocketData));
                data->handle                 = (uv_stream_t *)client;
                result->body.ops             = &op_table;
                result->body.data            = data;

                MVM_repr_push_o(tc, arr, (MVMObject *)result);
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);

                uv_tcp_getpeername(client, (struct sockaddr *)&name, &name_len);
                push_name_and_port(tc, &name, arr);
            }

            {
                MVMOSHandle          *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
                MVMIOAsyncSocketData *data   = MVM_calloc(1, sizeof(MVMIOAsyncSocketData));
                data->handle                 = (uv_stream_t *)li->socket;
                result->body.ops             = &op_table;
                result->body.data            = data;

                MVM_repr_push_o(tc, arr, (MVMObject *)result);

                uv_tcp_getsockname(client, (struct sockaddr *)&name, &name_len);
                push_name_and_port(tc, &name, arr);
            }
        });
    }
    else {
        /* Error; need to notify. */
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
        MVMROOT3(tc, t, arr, li->async_task, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(r));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        });
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);

        if (client != NULL && !uv_is_closing((uv_handle_t *)client))
            uv_close((uv_handle_t*)client, free_on_close_cb);
    }

    MVM_repr_push_o(tc, t->body.queue, arr);
}

static void do_listen_setup(uv_handle_t *handle) {
    ListenInfo       *li   = (ListenInfo *)handle->data;
    MVMThreadContext *tc   = li->tc;
    MVMAsyncTask     *t    = (MVMAsyncTask *)li->async_task;
    MVMObject        *arr;

    if ((li->error = uv_tcp_init(li->loop, li->socket)) != 0)
        goto error;

    if ((li->error = uv_tcp_bind(li->socket, li->address_info->ai_addr, 0)) != 0)
        goto error;

    if ((li->error = uv_listen((uv_stream_t *)li->socket, li->backlog, on_connection)) != 0)
        goto error;

    /* Success; allocate our handle. */
    MVMROOT(tc, t, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVMROOT2(tc, t, arr, {
        MVMOSHandle             *result   = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
        MVMIOAsyncSocketData    *data     = MVM_calloc(1, sizeof(MVMIOAsyncSocketData));
        data->handle      = (uv_stream_t *)li->socket;
        result->body.ops  = &op_table;
        result->body.data = data;
        MVM_repr_push_o(tc, arr, (MVMObject *)result);

        {
            struct sockaddr_storage  name;
            int                      name_len = sizeof(struct sockaddr_storage);
            uv_tcp_getsockname(li->socket, (struct sockaddr *)&name, &name_len);
            push_name_and_port(tc, &name, arr);
        }
    });
    MVM_repr_push_o(tc, t->body.queue, arr);
    return;

error:
    /* Error; no addresses could be used, so we need to notify. */
    MVMROOT(tc, t, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
    MVMROOT2(tc, t, arr, {
        MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
            tc->instance->VMString, uv_strerror(li->error));
        MVMObject *msg_box = MVM_repr_box_str(tc,
            tc->instance->boot_types.BOOTStr, msg_str);
        MVM_repr_push_o(tc, arr, msg_box);
    });
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVM_repr_push_o(tc, t->body.queue, arr);

    /* Clean up our handle. */
    if (li->socket != NULL && !uv_is_closing(handle)) {
        uv_close(handle, free_on_close_cb);
        li->socket = NULL;
    }

    MVM_io_eventloop_remove_active_work(tc, &(li->work_idx));
    return;
}
/* Sets up a socket listener. */
static void listen_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    /* Add to work in progress. */
    ListenInfo *li = (ListenInfo *)data;
    li->tc         = tc;
    li->loop       = loop;
    li->async_task = async_task;
    li->work_idx   = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Create and initialize socket and connection, and start listening. */
    li->socket       = MVM_malloc(sizeof(uv_tcp_t));
    li->socket->data = data;

    do_listen_setup((uv_handle_t *)li->socket);
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
    if (li->socket != NULL) {
        uv_close((uv_handle_t *)li->socket, on_listen_cancelled);
        li->socket = NULL;
    }
}

/* Marks objects for a listen task. */
static void listen_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    ListenInfo *li = (ListenInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &li->async_task);
}

/* Frees info for a listen task. */
static void listen_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data != NULL) {
        ListenInfo *li = (ListenInfo *)data;
        if (li->address_info != NULL)
            freeaddrinfo(li->address_info);
        MVM_free(li);
    }
}

/* Operations table for async listen task. */
static const MVMAsyncTaskOps listen_op_table = {
    listen_setup,
    NULL,
    listen_cancel,
    listen_gc_mark,
    listen_gc_free
};

/* Initiates an async socket listener. */
MVMObject * MVM_io_socket_listen_async(MVMThreadContext *tc, MVMObject *queue,
                                       MVMObject *schedulee, MVMString *host,
                                       MVMint64 port, MVMint32 backlog, MVMObject *async_type) {
    MVMAsyncTask    *task;
    ListenInfo      *li;
    struct addrinfo *address_info;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asynclisten target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asynclisten result type must have REPR AsyncTask");

    MVMROOT4(tc, queue, schedulee, host, async_type, {
        /* Resolve hostname. (Could be done asynchronously too.) */
        address_info = MVM_io_resolve_host_name(tc, host, port, SOCKET_FAMILY_UNSPEC, SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP, 1);
        /* Create async task handle. */
        task         = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });

    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    li               = MVM_calloc(1, sizeof(ListenInfo));
    li->backlog      = backlog;
    li->address_info = address_info;
    task->body.data  = li;
    task->body.ops   = &listen_op_table;

    /* Hand the task off to the event loop. */
    MVMROOT5(tc, queue, schedulee, host, async_type, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return (MVMObject *)task;
}
