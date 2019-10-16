#include "moar.h"

/* Number of bytes we accept per read. */
#define CHUNK_SIZE 65536

/* Data that we keep for an asynchronous UDP socket handle. */
typedef struct {
    /* The libuv handle to the socket. */
    uv_udp_t   *handle;
} MVMIOAsyncUDPSocketData;

/* Info we convey about a read task. */
typedef struct {
    MVMThreadContext *tc;
    int               work_idx;
    MVMOSHandle      *handle;
    MVMObject        *buf_type;
    int               seq_number;
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

/* XXX this is duplicated from asyncsocket.c; put it in some shared file */
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

/* Read handler. */
static void on_read(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
    ReadInfo         *ri   = (ReadInfo *)handle->data;
    MVMThreadContext *tc   = ri->tc;
    MVMAsyncTask     *t;
    MVMObject        *arr;

    /* libuv will call on_read once after all datagram read operations
     * to "give us back a buffer". in that case, nread and addr are NULL.
     * This is an artifact of the underlying implementation and we shouldn't
     * pass it through to the user. */
    if (nread == 0 && addr == NULL)
        return;

    t = MVM_io_eventloop_get_active_work(tc, ri->work_idx);
    MVMROOT(tc, t, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (nread >= 0) {
        MVMROOT2(tc, t, arr, {
            /* Success; start by pushing the sequence number, then produce a
             * buffer an push that as well. */
            MVMObject *seq_boxed = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, ri->seq_number++);
            MVMArray *res_buf    = (MVMArray *)MVM_repr_alloc_init(tc, ri->buf_type);
            res_buf->body.slots.i8 = (MVMint8 *)buf->base;
            res_buf->body.start    = 0;
            res_buf->body.ssize    = buf->len;
            res_buf->body.elems    = nread;
            MVM_repr_push_o(tc, arr, seq_boxed);
            MVM_repr_push_o(tc, arr, (MVMObject *)res_buf);
        });

        /* Next, no error... */
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);

        /* ...and finally, address and port. */
        MVMROOT2(tc, t, arr, {
            push_name_and_port(tc, (struct sockaddr_storage *)addr, arr);
        });

        MVM_repr_push_o(tc, t->body.queue, arr);
    }
    else {
        if (nread == UV_EOF) {
            /* End of file; push bytes read. */
            MVMROOT2(tc, t, arr, {
                MVMObject *final = MVM_repr_box_int(tc,
                    tc->instance->boot_types.BOOTInt, ri->seq_number);
                MVM_repr_push_o(tc, arr, final);
            });
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        }
        else {
            /* Error; push the error message. */
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVMROOT2(tc, t, arr, {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, uv_strerror(nread));
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            });
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        }

        MVM_repr_push_o(tc, t->body.queue, arr);

        /* Clean up. */
        if (handle != NULL && !uv_is_closing((uv_handle_t *)handle))
            uv_udp_recv_stop(handle);
        if (buf->base != NULL)
            MVM_free(buf->base);

        MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
    }
}

/* Does setup work for setting up asynchronous reads. */
static void read_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncUDPSocketData *handle_data;
    MVMAsyncTask            *t;
    MVMObject               *arr;
    int                      r;

    /* Add to work in progress. */
    ReadInfo *ri  = (ReadInfo *)data;
    ri->tc        = tc;
    ri->work_idx  = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Start reading the stream. */
    handle_data               = (MVMIOAsyncUDPSocketData *)ri->handle->body.data;
    handle_data->handle->data = data;
    if ((r = uv_udp_recv_start(handle_data->handle, on_alloc, on_read)) == 0)
        /* Success; finish up in on_read. */
        return;

    /* Error; need to notify. */
    t = (MVMAsyncTask *)async_task;
    MVMROOT(tc, t, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    MVMROOT2(tc, t, arr, {
        MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
            tc->instance->VMString, uv_strerror(r));
        MVMObject *msg_box = MVM_repr_box_str(tc,
            tc->instance->boot_types.BOOTStr, msg_str);
        MVM_repr_push_o(tc, arr, msg_box);
    });
    MVM_repr_push_o(tc, t->body.queue, arr);
}

/* Marks objects for a read task. */
static void read_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    ReadInfo *ri = (ReadInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &ri->handle);
    MVM_gc_worklist_add(tc, worklist, &ri->buf_type);
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
    NULL,
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
    MVM_ASSIGN_REF(tc, &(task->common.header), ri->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), ri->buf_type, buf_type);
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
    MVMObject        *async_task;
    int               work_idx;
    MVMOSHandle      *handle;
    MVMObject        *buf_data;
    struct addrinfo  *records;
    struct addrinfo  *cur_record;
    uv_udp_send_t    *req;
    uv_buf_t          buf;
    int               error;
} WriteInfo;

/* Completion handler for an asynchronous write. */
static void on_write(uv_udp_send_t *req, int status) {
    WriteInfo        *wi   = (WriteInfo *)req->data;
    MVMThreadContext *tc   = wi->tc;
    MVMAsyncTask     *t    = MVM_io_eventloop_get_active_work(tc, wi->work_idx);
    MVMObject        *arr;

    MVMROOT3(tc, t, wi->handle, wi->buf_data, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (status == 0) {
        MVMROOT4(tc, t, arr, wi->handle, wi->buf_data, {
            MVMObject *bytes_box = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt,
                wi->buf.len);
            MVM_repr_push_o(tc, arr, bytes_box);
        });
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    }
    else {
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        MVMROOT4(tc, t, arr, wi->handle, wi->buf_data, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(status));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        });
    }
    MVM_repr_push_o(tc, t->body.queue, arr);

    /* Clean up. */
    MVM_free(wi->req);

    MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
}

/* Does setup work for an asynchronous write. */
static void write_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMArray                *buffer;
    char                    *output;
    int                      output_size;
    MVMIOAsyncUDPSocketData *handle_data;
    uv_handle_t             *handle;
    MVMAsyncTask            *t;
    MVMObject               *arr;
    int                      e;

    /* Add to work in progress. */
    WriteInfo *wi  = (WriteInfo *)data;
    wi->tc         = tc;
    wi->async_task = async_task;
    wi->work_idx   = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Extract buf data. */
    buffer      = (MVMArray *)wi->buf_data;
    output      = (char *)(buffer->body.slots.i8 + buffer->body.start);
    output_size = (int)buffer->body.elems;

    /* Create and initialize write request. */
    wi->req       = MVM_malloc(sizeof(uv_udp_send_t));
    wi->req->data = wi;
    wi->buf       = uv_buf_init(output, output_size);

    handle_data = (MVMIOAsyncUDPSocketData *)wi->handle->body.data;
    handle      = (uv_handle_t *)handle_data->handle;
    if (uv_is_closing(handle))
        MVM_exception_throw_adhoc(tc, "cannot send over a closed socket");

    for (; wi->cur_record != NULL; wi->cur_record = wi->cur_record->ai_next) {
        if ((wi->error = uv_udp_send(wi->req, (uv_udp_t *)handle, &(wi->buf), 1, wi->cur_record->ai_addr, on_write)) == 0)
            /* Success; finish up in on_write. */
            return;

        /* Error; try again with the next address, if any, before throwing. */
    }

    /* Error; no addresses could be used, so we need to notify. */
    t = (MVMAsyncTask *)async_task;
    MVMROOT(tc, t, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
    MVMROOT2(tc, t, arr, {
        MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
            tc->instance->VMString, uv_strerror(wi->error));
        MVMObject *msg_box = MVM_repr_box_str(tc,
            tc->instance->boot_types.BOOTStr, msg_str);
        MVM_repr_push_o(tc, arr, msg_box);
    });
    MVM_repr_push_o(tc, t->body.queue, arr);

    /* Clean up handle. */
    MVM_free(wi->req);

    MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
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
        if (wi->records != NULL)
            freeaddrinfo(wi->records);
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

static MVMAsyncTask * write_bytes_to(MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
                                     MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type,
                                     MVMString *host, MVMint64 port) {
    MVMAsyncTask    *task;
    WriteInfo       *wi;
    struct addrinfo *records;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncwritebytesto target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncwritebytesto result type must have REPR AsyncTask");
    if (!IS_CONCRETE(buffer) || REPR(buffer)->ID != MVM_REPR_ID_VMArray)
        MVM_exception_throw_adhoc(tc, "asyncwritebytesto requires a native array to read from");
    if (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_U8
        && ((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "asyncwritebytesto requires a native array of uint8 or int8");

    /* Resolve destination and create async task handle. */
    MVMROOT6(tc, h, queue, schedulee, buffer, async_type, host, {
        records = MVM_io_resolve_host_name(tc, host, port, SOCKET_FAMILY_UNSPEC, SOCKET_TYPE_DGRAM, SOCKET_PROTOCOL_UDP, 0);
        task    = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    wi              = MVM_calloc(1, sizeof(WriteInfo));
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->handle, h);
    MVM_ASSIGN_REF(tc, &(task->common.header), wi->buf_data, buffer);
    wi->records     = records;
    wi->cur_record  = records;
    task->body.data = wi;
    task->body.ops  = &write_op_table;

    /* Hand the task off to the event loop. */
    MVMROOT6(tc, h, queue, schedulee, buffer, async_type, host, {
        MVMROOT(tc, task, {
            MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
        });
    });

    return task;
}

/* Does an asynchronous close (since it must run on the event loop). */
static void close_perform(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    uv_handle_t *handle = (uv_handle_t *)data;

    if (handle == NULL || uv_is_closing(handle))
        MVM_exception_throw_adhoc(tc, "cannot close a closed socket");

    uv_close(handle, free_on_close_cb);
}

/* Operations table for async close task. */
static const MVMAsyncTaskOps close_op_table = {
    close_perform,
    NULL,
    NULL,
    NULL,
    NULL
};

static MVMint64 close_socket(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncUDPSocketData *data = (MVMIOAsyncUDPSocketData *)h->body.data;
    MVMAsyncTask            *task;
    MVMROOT(tc, h, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc,
            tc->instance->boot_types.BOOTAsync);
    });
    task->body.ops  = &close_op_table;
    task->body.data = data->handle;
    MVMROOT2(tc, h, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });
    return 0;
}

static MVMint64 socket_is_tty(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncUDPSocketData *data   = (MVMIOAsyncUDPSocketData *)h->body.data;
    uv_handle_t             *handle = (uv_handle_t *)data->handle;
    return (MVMint64)(handle->type == UV_TTY);
}

static MVMint64 socket_handle(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncUDPSocketData *data   = (MVMIOAsyncUDPSocketData *)h->body.data;
    uv_handle_t             *handle = (uv_handle_t *)data->handle;
    int                      fd;
    uv_os_fd_t               fh;

    uv_fileno(handle, &fh);
    fd = uv_open_osfhandle(fh);
    return (MVMint64)fd;
}

/* IO ops table, populated with functions. */
static const MVMIOClosable        closable          = { close_socket };
static const MVMIOAsyncReadable   async_readable    = { read_bytes };
static const MVMIOAsyncWritableTo async_writable_to = { write_bytes_to };
static const MVMIOIntrospection   introspection     = { socket_is_tty,
                                                        socket_handle };
static const MVMIOOps op_table = {
    &closable,
    NULL,
    NULL,
    &async_readable,
    NULL,
    &async_writable_to,
    NULL,
    NULL,
    NULL,
    NULL,
    &introspection,
    NULL,
    NULL,
    NULL
};

/* Info we convey about a socket setup task. */
typedef struct {
    MVMThreadContext *tc;
    uv_loop_t        *loop;
    MVMObject        *async_task;
    uv_udp_t         *handle;
    int               bind;
    struct addrinfo  *records;
    struct addrinfo  *cur_record;
    MVMint64          flags;
    int               error;
} SocketSetupInfo;

/* Does the actual work of initializing the UDP socket on the event loop. */
static void do_setup_setup(uv_handle_t *handle) {
    SocketSetupInfo  *ssi  = (SocketSetupInfo *)handle->data;
    MVMThreadContext *tc   = ssi->tc;
    MVMAsyncTask     *t    = (MVMAsyncTask *)ssi->async_task;
    MVMObject        *arr;

    if (ssi->bind) {
        if (ssi->cur_record == NULL) {
            /* Clean up. */
            handle->data = NULL;
            MVM_free(handle);
        } else {
            /* Create and bind the socket. */
            ssi->error = uv_udp_init(ssi->loop, ssi->handle);
            if (ssi->error == 0 && ssi->flags & 1)
                ssi->error = uv_udp_set_broadcast(ssi->handle, 1);
            if (ssi->error == 0)
                ssi->error = uv_udp_bind(ssi->handle, ssi->cur_record->ai_addr, 0);

            if (ssi->error != 0) {
                /* Error; try again with the next address, if any, before throwing. */
                ssi->cur_record = ssi->cur_record->ai_next;
                uv_close(handle, do_setup_setup);
                return;
            }
        }
    } else {
        /* Create the socket. */
        ssi->error = uv_udp_init(ssi->loop, ssi->handle);
        if (ssi->error == 0 && ssi->flags & 1)
            ssi->error = uv_udp_set_broadcast(ssi->handle, 1);
    }

    MVMROOT(tc, t, {
        arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    });
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (ssi->error == 0) {
        /* UDP handle initialized; wrap it up in an I/O handle and send. */
        MVMROOT2(tc, t, arr, {
            MVMOSHandle             *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
            MVMIOAsyncUDPSocketData *data   = MVM_calloc(1, sizeof(MVMIOAsyncUDPSocketData));
            data->handle                    = ssi->handle;
            result->body.ops                = &op_table;
            result->body.data               = data;
            MVM_repr_push_o(tc, arr, (MVMObject *)result);
        });
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    }
    else {
        /* Error; no addresses could be used, so we need to notify. */
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
        MVMROOT2(tc, t, arr, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(ssi->error));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        });
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
}


/* Initilalize the UDP socket on the event loop. */
static void setup_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    /* Set up the UDP handle. */
    SocketSetupInfo *ssi = (SocketSetupInfo *)data;
    ssi->handle          = MVM_malloc(sizeof(uv_udp_t));
    ssi->handle->data    = ssi;
    ssi->tc              = tc;
    ssi->loop            = loop;
    ssi->async_task      = async_task;

    do_setup_setup((uv_handle_t *)ssi->handle);
}

/* Frees info for a connection task. */
static void setup_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data != NULL) {
        SocketSetupInfo *ssi = (SocketSetupInfo *)data;
        if (ssi->records != NULL)
            freeaddrinfo(ssi->records);
        MVM_free(ssi);
    }
}

/* Marks async connect task. */
static void setup_gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    SocketSetupInfo *ssi = (SocketSetupInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &ssi->async_task);
}

/* Operations table for async connect task. */
static const MVMAsyncTaskOps setup_op_table = {
    setup_setup,
    NULL,
    NULL,
    setup_gc_mark,
    setup_gc_free
};

/* Creates a UDP socket and binds it to the specified host/port. */
MVMObject * MVM_io_socket_udp_async(MVMThreadContext *tc, MVMObject *queue,
                                    MVMObject *schedulee, MVMString *host,
                                    MVMint64 port, MVMint64 flags,
                                    MVMObject *async_type) {
    int              bind    = (host != NULL && IS_CONCRETE(host));
    struct addrinfo *records = NULL;
    MVMAsyncTask    *task;
    SocketSetupInfo *ssi;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncudp target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncudp result type must have REPR AsyncTask");

    MVMROOT4(tc, queue, schedulee, host, async_type, {
        /* Resolve hostname. (Could be done asynchronously too.) */
        if (bind)
            records = MVM_io_resolve_host_name(tc, host, port, SOCKET_FAMILY_UNSPEC, SOCKET_TYPE_DGRAM, SOCKET_PROTOCOL_UDP, 1);

        /* Create async task handle. */
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops  = &setup_op_table;
    ssi             = MVM_calloc(1, sizeof(SocketSetupInfo));
    ssi->bind       = bind;
    ssi->records    = records;
    ssi->cur_record = records;
    ssi->flags      = flags;
    task->body.data = ssi;

    /* Hand the task off to the event loop. */
    MVMROOT5(tc, queue, schedulee, host, async_type, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return (MVMObject *)task;
}
