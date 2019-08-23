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

/* XXX this is duplicated from asyncsocket.c; put it in some shared file */
static void push_name_and_port(MVMThreadContext *tc, struct sockaddr_storage *name, MVMObject *arr) {
    char addrstr[INET6_ADDRSTRLEN + 1];
    /* XXX windows support kludge. 64 bit is much too big, but we'll
     * get the proper data from the struct anyway, however windows
     * decides to declare it. */
    MVMuint64 port;
    MVMObject *host_o;
    MVMObject *port_o;
    if (name) {
        switch (name->ss_family) {
            case AF_INET6: {
                uv_ip6_name((struct sockaddr_in6*)name, addrstr, INET6_ADDRSTRLEN + 1);
                port = ntohs(((struct sockaddr_in6*)name)->sin6_port);
                break;
            }
            case AF_INET: {
                uv_ip4_name((struct sockaddr_in*)name, addrstr, INET6_ADDRSTRLEN + 1);
                port = ntohs(((struct sockaddr_in*)name)->sin_port);
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
            MVMROOT(tc, port_o, {
                host_o = (MVMObject *)MVM_repr_box_str(tc, tc->instance->boot_types.BOOTStr,
                        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, addrstr));
            });
        });
    } else {
        host_o = tc->instance->boot_types.BOOTStr;
        port_o = tc->instance->boot_types.BOOTInt;
    }
    MVM_repr_push_o(tc, arr, host_o);
    MVM_repr_push_o(tc, arr, port_o);
}

/* Read handler. */
static void on_read(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
    ReadInfo         *ri  = (ReadInfo *)handle->data;
    MVMThreadContext *tc  = ri->tc;
    MVMObject        *arr;
    MVMAsyncTask     *t;

    /* libuv will call on_read once after all datagram read operations
     * to "give us back a buffer". in that case, nread and addr are NULL.
     * This is an artifact of the underlying implementation and we shouldn't
     * pass it through to the user. */

    if (nread == 0 && addr == NULL)
        return;

    arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    t = MVM_io_eventloop_get_active_work(tc, ri->work_idx);

    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (nread >= 0) {
        MVMROOT2(tc, t, arr, {
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

            /* next, no error. */
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);

            /* and finally, address and port */
            push_name_and_port(tc, (struct sockaddr_storage *)addr, arr);
        });
    }
    else if (nread == UV_EOF) {
        MVMROOT2(tc, t, arr, {
            MVMObject *final = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt, ri->seq_number);
            MVM_repr_push_o(tc, arr, final);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        });
        if (buf->base)
            MVM_free(buf->base);
        uv_udp_recv_stop(handle);
        MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
    }
    else {
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
        MVMROOT2(tc, t, arr, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(nread));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        });
        if (buf->base)
            MVM_free(buf->base);
        uv_udp_recv_stop(handle);
        MVM_io_eventloop_remove_active_work(tc, &(ri->work_idx));
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
}

/* Does setup work for setting up asynchronous reads. */
static void read_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMIOAsyncUDPSocketData *handle_data;
    int                      r;

    /* Add to work in progress. */
    ReadInfo *ri  = (ReadInfo *)data;
    ri->tc        = tc;
    ri->work_idx  = MVM_io_eventloop_add_active_work(tc, async_task);

    /* Start reading the stream. */
    handle_data               = (MVMIOAsyncUDPSocketData *)ri->handle->body.data;
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
    if (data)
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
    ReadInfo    *ri;

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
    MVMROOT4(tc, queue, schedulee, h, buf_type, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
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
    MVMThreadContext *tc;
    MVMObject        *async_task;
    int               work_idx;
    MVMOSHandle      *handle;
    MVMObject        *buf_data;
    uv_udp_send_t    *req;
    uv_buf_t          buf;
    struct addrinfo  *dest_addr;
    int               error;
} WriteInfo;

/* Completion handler for an asynchronous write. */
static void on_write(uv_udp_send_t *req, int status) {
    WriteInfo        *wi  = (WriteInfo *)req->data;
    MVMThreadContext *tc  = wi->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = MVM_io_eventloop_get_active_work(tc, wi->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    if (status >= 0) {
        MVMROOT2(tc, arr, t, {
            MVMObject *bytes_box = MVM_repr_box_int(tc,
                tc->instance->boot_types.BOOTInt,
                wi->buf.len);
            MVM_repr_push_o(tc, arr, bytes_box);
        });
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
    }
    else {
        MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
        MVMROOT2(tc, arr, t, {
            MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                tc->instance->VMString, uv_strerror(status));
            MVMObject *msg_box = MVM_repr_box_str(tc,
                tc->instance->boot_types.BOOTStr, msg_str);
            MVM_repr_push_o(tc, arr, msg_box);
        });
    }
    MVM_repr_push_o(tc, t->body.queue, arr);
    MVM_free(wi->req);
    MVM_io_eventloop_remove_active_work(tc, &(wi->work_idx));
}

/* Does setup work for an asynchronous write. */
static void write_setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    MVMArray        *buffer;
    char            *output;
    int              output_size;
    uv_udp_t        *handle;
    struct addrinfo *record;

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
    wi->req   = MVM_malloc(sizeof(uv_udp_send_t));
    wi->buf   = uv_buf_init(output, output_size);
    wi->error = 0;

    handle = ((MVMIOAsyncUDPSocketData *)wi->handle->body.data)->handle;
    if (uv_is_closing((uv_handle_t *)handle))
        MVM_exception_throw_adhoc(tc, "cannot send over a closed socket");

    for (record = wi->dest_addr; record != NULL; record = record->ai_next) {
        char *hostname = MVM_malloc(sizeof(char));
        inet_ntop(record->ai_family, &((struct sockaddr_in *)record->ai_addr)->sin_addr, hostname, record->ai_addrlen);
        printf("writing to %s %d %d %d\n", hostname, record->ai_family, record->ai_socktype, record->ai_protocol);
        MVM_free(hostname);

        if ((wi->error = uv_udp_send(wi->req, handle, &(wi->buf), 1, record->ai_addr, on_write)) >= 0)
            break;
    }

    if (wi->error < 0) {
        /* Error; need to notify. */
        MVMROOT(tc, wi->async_task, {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask *t   = (MVMAsyncTask *)wi->async_task;
            MVM_repr_push_o(tc, arr, t->body.schedulee);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTInt);
            MVMROOT(tc, arr, {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, uv_strerror(wi->error));
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            });
            MVM_repr_push_o(tc, t->body.queue, arr);
        });

        /* Clean up handle. */
        MVM_free(wi->req);
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
    if (data != NULL) {
        WriteInfo *wi = (WriteInfo *)data;
        if (wi->dest_addr != NULL)
            freeaddrinfo(wi->dest_addr);
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
    struct addrinfo *dest_addr = NULL;

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
    MVMROOT4(tc, queue, schedulee, h, buffer, {
        MVMROOT(tc, async_type, {
            dest_addr = MVM_io_resolve_host_name(tc, host, port, SOCKET_FAMILY_UNSPEC, SOCKET_TYPE_DGRAM, SOCKET_PROTOCOL_UDP, 0);
        });
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
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

static MVMint64 socket_is_tty(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncUDPSocketData *data   = (MVMIOAsyncUDPSocketData *)h->body.data;
    uv_handle_t             *handle = (uv_handle_t *)data->handle;
    return (MVMint64)(handle->type == UV_TTY);
}

static MVMint64 socket_handle(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOAsyncUDPSocketData *data   = (MVMIOAsyncUDPSocketData *)h->body.data;
    uv_handle_t             *handle = (uv_handle_t *)data->handle;
    int        fd;
    uv_os_fd_t fh;

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
    struct addrinfo  *bind_addr;
    MVMint64          flags;
    int               error;
} SocketSetupInfo;

/* Does the actual work of initializing the UDP socket on the event loop. */
static void do_setup_setup(uv_handle_t *handle) {
    SocketSetupInfo  *ssi     = (SocketSetupInfo *)handle->data;
    MVMThreadContext *tc      = ssi->tc;
    struct addrinfo  *record;

    for (record = ssi->bind_addr; record != NULL; record = record->ai_next) {
        char *hostname = MVM_malloc(sizeof(char));
        inet_ntop(record->ai_family, &((struct sockaddr_in *)record->ai_addr)->sin_addr, hostname, record->ai_addrlen);
        printf("binding on %s %d %d %d\n", hostname, record->ai_family, record->ai_socktype, record->ai_protocol);
        MVM_free(hostname);

        if ((ssi->error = uv_udp_init(ssi->loop, ssi->handle)) >= 0) {
            if (ssi->bind_addr != NULL)
                ssi->error = uv_udp_bind(ssi->handle, record->ai_addr, 0);
            if (ssi->error >= 0 && (ssi->flags & 1))
                ssi->error = uv_udp_set_broadcast(ssi->handle, 1);
        }

        if (ssi->error >= 0)
            break;
        else {
            struct addrinfo *next = record->ai_next;
            record->ai_next = NULL;
            freeaddrinfo(ssi->bind_addr);
            ssi->bind_addr = next;
            uv_close(handle, do_setup_setup);
            return;
        }
    }

    if (ssi->error >= 0) {
        /* UDP handle initialized; wrap it up in an I/O handle and send. */
        MVMAsyncTask *t   = (MVMAsyncTask *)ssi->async_task;
        MVMObject    *arr;
        MVMROOT(tc, t, {
            arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVM_repr_push_o(tc, arr, t->body.schedulee);
            MVMROOT(tc, arr, {
                MVMOSHandle             *result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
                MVMIOAsyncUDPSocketData *data   = MVM_calloc(1, sizeof(MVMIOAsyncUDPSocketData));
                ssi->handle->data               = NULL;
                data->handle                    = ssi->handle;
                result->body.ops                = &op_table;
                result->body.data               = data;
                MVM_repr_push_o(tc, arr, (MVMObject *)result);
                MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTStr);
                MVM_repr_push_o(tc, t->body.queue, arr);
            });
        });
    }
    else {
        /* Something failed; need to notify. */
        MVMROOT(tc, ssi->async_task, {
            MVMObject    *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            MVMAsyncTask *t   = (MVMAsyncTask *)ssi->async_task;
            MVM_repr_push_o(tc, arr, t->body.schedulee);
            MVM_repr_push_o(tc, arr, tc->instance->boot_types.BOOTIO);
            MVMROOT2(tc, arr, t, {
                MVMString *msg_str = MVM_string_ascii_decode_nt(tc,
                    tc->instance->VMString, uv_strerror(ssi->error));
                MVMObject *msg_box = MVM_repr_box_str(tc,
                    tc->instance->boot_types.BOOTStr, msg_str);
                MVM_repr_push_o(tc, arr, msg_box);
            });
            MVM_repr_push_o(tc, t->body.queue, arr);
        });
    }
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
    ssi->error           = 0;

    do_setup_setup((uv_handle_t *)ssi->handle);
}

/* Frees info for a connection task. */
static void setup_gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data != NULL) {
        SocketSetupInfo *ssi = (SocketSetupInfo *)data;
        if (ssi->bind_addr != NULL)
            freeaddrinfo(ssi->bind_addr);
        MVM_free(ssi);
    }
}

/* Operations table for async connect task. */
static const MVMAsyncTaskOps setup_op_table = {
    setup_setup,
    NULL,
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
    struct addrinfo *bind_addr = NULL;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "asyncudp target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "asyncudp result type must have REPR AsyncTask");

    /* Resolve hostname. (Could be done asynchronously too.) */
    if (host && IS_CONCRETE(host)) {
        MVMROOT3(tc, queue, schedulee, async_type, {
            bind_addr = MVM_io_resolve_host_name(tc, host, port, SOCKET_FAMILY_UNSPEC, SOCKET_TYPE_DGRAM, SOCKET_PROTOCOL_UDP, 1);
        });
    }

    /* Create async task handle. */
    MVMROOT2(tc, queue, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
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
