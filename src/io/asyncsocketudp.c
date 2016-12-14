#include "moar.h"

/* Number of bytes we accept per read. */
#define CHUNK_SIZE 65536

/* Data that we keep for an asynchronous UDP socket handle. */
typedef struct {
    /* The libuv handle to the socket. */
    uv_udp_t *handle;

    /* Decode stream, for turning bytes into strings. */
    MVMDecodeStream *ds;
} MVMIOAsyncUDPSocketData;

/* Info we convey about a read task. */
typedef struct {
    MVMOSHandle      *handle;
    MVMDecodeStream  *ds;
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
static void on_read(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
    ReadInfo         *ri  = (ReadInfo *)handle->data;
    MVMThreadContext *tc  = ri->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = (MVMAsyncTask *)MVM_repr_at_pos_o(tc,
        tc->instance->event_loop_active, ri->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (nread >= 0) {
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
                MVMArray *res_buf      = (MVMArray *)MVM_repr_alloc_init(tc, ri->buf_type);
                res_buf->body.slots.i8 = (MVMint8 *)buf->base;
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
            MVMObject *final = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, ri->seq_number);
            MVM_repr_push_o(tc, arr, final);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        });
        });
        if (buf->base)
            MVM_free(buf->base);
        uv_udp_recv_stop(handle);
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
        uv_udp_recv_stop(handle);
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
}

/* Does setup work for setting up asynchronous reads. */
static void read_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncUDPSocketData *handle_data;
    int                   r;

    /* Add to work in progress. */
    ReadInfo *ri  = (ReadInfo *)data;
    ri->tc        = tc;
    ri->work_idx  = MVM_repr_elems(tc, tc->instance->event_loop_active);
    MVM_repr_push_o(tc, tc->instance->event_loop_active, async_task);

    /* Start reading the stream. */
    handle_data = (MVMIOAsyncUDPSocketData *)ri->handle->body.data;
    handle_data->handle->data = data;
    if ((r = uv_udp_recv_start(handle_data->handle, on_alloc, on_read)) < 0) {
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
static void read_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    ReadInfo *ri = (ReadInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &ri->buf_type);
    MVM_gc_worklist_add(tc, worklist, &ri->handle);
}

/* Frees info for a read task. */
static void read_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        ReadInfo *ri = (ReadInfo *)data;
        if (ri->ds)
            MVM_string_decodestream_destory(tc, ri->ds);
        MVM_free(data);
    }
}

/* Operations table for async read task. */
static const MVMAsyncTaskOps read_op_table = {
    read_setup,
    NULL,
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
    MVMROOT(tc, h, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &read_op_table;
    ri              = MVM_calloc(1, sizeof(ReadInfo));
    ri->ds          = MVM_string_decodestream_create(tc, MVM_encoding_type_utf8, 0, 0);
    MVM_ASSIGN_REF(tc, &(task->common.header), ri->handle, h);
    task->body.data = ri;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return task;
}

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
    if (REPR(buf_type)->ID == MVM_REPR_ID_MVMArray) {
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
    MVMString        *str_data;
    MVMObject        *buf_data;
    uv_udp_send_t    *req;
    uv_buf_t          buf;
    MVMThreadContext *tc;
    int               work_idx;
    struct sockaddr  *dest_addr;
} WriteInfo;

/* Completion handler for an asynchronous write. */
static void on_write(uv_udp_send_t *req, int status) {
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
        MVM_free(wi->buf.base);
    MVM_free(wi->req);
}

/* Does setup work for an asynchronous write. */
static void write_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncUDPSocketData *handle_data;
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
        output = MVM_string_utf8_encode(tc, wi->str_data, &output_size_64, 0);
        output_size = (int)output_size_64;
    }
    else {
        MVMArray *buffer = (MVMArray *)wi->buf_data;
        output = (char *)(buffer->body.slots.i8 + buffer->body.start);
        output_size = (int)buffer->body.elems;
    }

    /* Create and initialize write request. */
    wi->req           = MVM_malloc(sizeof(uv_udp_send_t));
    wi->buf           = uv_buf_init(output, output_size);
    wi->req->data     = data;
    handle_data       = (MVMIOAsyncUDPSocketData *)wi->handle->body.data;

    if (uv_is_closing((uv_handle_t *)handle_data->handle))
        MVM_exception_throw_adhoc(tc, "cannot write to a closed socket");

    if ((r = uv_udp_send(wi->req, handle_data->handle, &(wi->buf), 1, wi->dest_addr, on_write)) < 0) {
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
    }
}

/* Marks objects for a write task. */
static void write_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    WriteInfo *wi = (WriteInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &wi->handle);
    MVM_gc_worklist_add(tc, worklist, &wi->str_data);
    MVM_gc_worklist_add(tc, worklist, &wi->buf_data);
}

/* Frees info for a write task. */
static void write_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        WriteInfo *wi = (WriteInfo *)data;
        if (wi->dest_addr)
            MVM_free(wi->dest_addr);
        MVM_free(data);
    }
}

/* Operations table for async write task. */
static const MVMAsyncTaskOps write_op_table = {
    write_setup,
    NULL,
    write_gc_mark,
    write_gc_free
};

static MVMAsyncTask * write_str_to(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                   MVMObject *schedulee, MVMString *s, MVMObject *async_type,
                                   MVMString *host, MVMint64 port) {
    MVMAsyncTask    *task;
    WriteInfo       *wi;
    struct sockaddr *dest_addr;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncwritestrto target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncwritestrto result type must have REPR AsyncTask");

    /* Resolve destination. */
    dest_addr = MVM_io_resolve_host_name(tc, host, port);

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
    wi              = MVM_calloc(1, sizeof(WriteInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->str_data, s);
    wi->dest_addr = dest_addr;
    task->body.data = wi;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return task;
}

static MVMAsyncTask * write_bytes_to(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                     MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type,
                                     MVMString *host, MVMint64 port) {
    MVMAsyncTask    *task;
    WriteInfo       *wi;
    struct sockaddr *dest_addr;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncwritebytesto target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncwritebytesto result type must have REPR AsyncTask");
    if (!IS_CONCRETE(buffer) || REPR(buffer)->ID != MVM_REPR_ID_MVMArray)
        MVM_exception_throw_adhoc(tc, "asyncwritebytesto requires a native array to read from");
    if (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_U8
        && ((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "asyncwritebytesto requires a native array of uint8 or int8");

    /* Resolve destination. */
    dest_addr = MVM_io_resolve_host_name(tc, host, port);

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
    wi->dest_addr = dest_addr;
    task->body.data = wi;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return task;
}

/* Does an asynchronous close (since it must run on the event loop). */
static void close_perform(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    uv_handle_t *handle = (uv_handle_t *)data;

    if (uv_is_closing(handle))
        MVM_exception_throw_adhoc(tc, "cannot close a closed socket");

    uv_close(handle, free_on_close_cb);
}

/* Operations table for async close task. */
static const MVMAsyncTaskOps close_op_table = {
    close_perform,
    NULL,
    NULL,
    NULL
};

static MVMint64 close_socket(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncUDPSocketData *data = (MVMIOAsyncUDPSocketData *)h->body.data;
    MVMAsyncTask *task;

    MVMROOT(tc, h, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc,
            tc->instance->boot_types.BOOTAsync);
    });
    task->body.ops  = &close_op_table;
    task->body.data = data->handle;
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return 0;
}

static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIOAsyncUDPSocketData *data = (MVMIOAsyncUDPSocketData *)d;
    if (data->ds) {
        MVM_string_decodestream_destory(tc, data->ds);
        data->ds = NULL;
    }
}

/* IO ops table, populated with functions. */
static const MVMIOClosable        closable          = { close_socket };
static const MVMIOAsyncReadable   async_readable    = { read_chars, read_bytes };
static const MVMIOAsyncWritableTo async_writable_to = { write_str_to, write_bytes_to };
static const MVMIOOps op_table = {
    &closable,
    NULL,
    NULL,
    NULL,
    &async_readable,
    NULL,
    &async_writable_to,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    gc_free
};

/* Info we convey about a socket setup task. */
typedef struct {
    struct sockaddr  *bind_addr;
    MVMint64          flags;
} SocketSetupInfo;

/* Initilalize the UDP socket on the event loop. */
static void setup_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    /* Set up the UDP handle. */
    SocketSetupInfo *ssi = (SocketSetupInfo *)data;
    uv_udp_t *udp_handle = MVM_malloc(sizeof(uv_udp_t));
    int r;
    if ((r = uv_udp_init(loop, udp_handle)) >= 0) {
        if (ssi->bind_addr)
            r = uv_udp_bind(udp_handle, ssi->bind_addr, 0);
        if (r >= 0 && (ssi->flags & 1))
            r = uv_udp_set_broadcast(udp_handle, 1);
    }

    if (r >= 0) {
        /* UDP handle initialized; wrap it up in an I/O handle and send. */
        MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        MVMAsyncTask *t   = (MVMAsyncTask *)async_task;
        MVM_repr_push_o(tc, arr, t->body.schedulee);
        MVMROOT(tc, arr, {
        MVMROOT(tc, t, {
            MVMOSHandle          *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
            MVMIOAsyncUDPSocketData *data   = MVM_calloc(1, sizeof(MVMIOAsyncUDPSocketData));
            data->handle                 = udp_handle;
            result->body.ops             = &op_table;
            result->body.data            = data;
            MVM_repr_push_o(tc, arr, (MVMObject *)result);
        });
        });
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        MVM_repr_push_o(tc, t->body.queue, arr);
    }
    else {
        /* Something failed; need to notify. */
        MVMROOT(tc, async_task, {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask *t   = (MVMAsyncTask *)async_task;
            MVM_repr_push_o(tc, arr, t->body.schedulee);
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
            MVM_repr_push_o(tc, t->body.queue, arr);
            uv_close((uv_handle_t *)udp_handle, free_on_close_cb);
        });
    }
}

/* Frees info for a connection task. */
static void setup_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data) {
        SocketSetupInfo *ssi = (SocketSetupInfo *)data;
        if (ssi->bind_addr)
            MVM_free(ssi->bind_addr);
        MVM_free(ssi);
    }
}

/* Operations table for async connect task. */
static const MVMAsyncTaskOps setup_op_table = {
    setup_setup,
    NULL,
    NULL,
    setup_gc_free
};

/* Creates a UDP socket and binds it to the specified host/port. */
MVMObject * MVM_io_socket_udp_async(MVMThreadContext *tc, MVMObject *queue,
                                    MVMObject *schedulee, MVMString *host,
                                    MVMint64 port, MVMint64 flags,
                                    MVMObject *async_type) {
    MVMAsyncTask    *task;
    SocketSetupInfo *ssi;
    struct sockaddr *bind_addr = NULL;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncudp target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncudp result type must have REPR AsyncTask");

    /* Resolve hostname. (Could be done asynchronously too.) */
    if (host && IS_CONCRETE(host))
        bind_addr = MVM_io_resolve_host_name(tc, host, port);

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &setup_op_table;
    ssi             = MVM_calloc(1, sizeof(SocketSetupInfo));
    ssi->bind_addr  = bind_addr;
    ssi->flags      = flags;
    task->body.data = ssi;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return (MVMObject *)task;
}
