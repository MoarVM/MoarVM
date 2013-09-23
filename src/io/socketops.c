#include "moarvm.h"

static void verify_socket_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {

    /* work on only MVMOSHandle of type MVM_OSHANDLE_FILE */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", msg);
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.type != MVM_OSHANDLE_TCP && (*handle)->body.type != MVM_OSHANDLE_UDP) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type socket", msg);
    }
}

MVMObject * MVM_socket_connect(MVMThreadContext *tc, MVMObject *type_object, MVMString *hostname, MVMint64 port, MVMint64 protocol, MVMint64 encoding_flag) {
    MVMOSHandle *result;
    MVMOSHandleBody *body;
    uv_tcp_t *socket;
    uv_connect_t connect;
    struct sockaddr_in dest;
    char *hostname_cstring;
    int r;

    ENCODING_VALID(encoding_flag);

    if (!(protocol == MVM_OSHANDLE_TCP || protocol == MVM_OSHANDLE_UDP)) {
        MVM_exception_throw_adhoc(tc, "Bind socket got an invalid protocol number (needs 4, 5; got %d)", protocol);
    }

    if (port < 1 || port > 65535) {
        MVM_exception_throw_adhoc(tc, "Open socket got an invalid port (need between 1 and 65535; got %d)", port);
    }

    hostname_cstring = MVM_string_utf8_encode_C_string(tc, hostname);
    if (strlen(hostname_cstring) == 0) {
        free(hostname_cstring);
        MVM_exception_throw_adhoc(tc, "Open socket needs a hostname or IP address");
    }

    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        free(hostname_cstring);
        MVM_exception_throw_adhoc(tc, "Open socket needs a type object with MVMOSHandle REPR");
    }

    uv_ip4_addr(hostname_cstring, port, &dest);

    free(hostname_cstring);

    socket = malloc(sizeof(uv_tcp_t));
    uv_tcp_init(tc->loop, socket);

    if ((r = uv_tcp_connect(&connect, socket, &dest, NULL)) < 0) {
        MVM_exception_throw_adhoc(tc, "Open socket failed to connect: %s", uv_strerror(r));
    }

    /* initialize the object */
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));

    body = &result->body;
    body->handle = (uv_handle_t *)socket;
    body->handle->data = body;   /* this is needed in tcp_stream_on_read function. */
    body->type = MVM_OSHANDLE_TCP;
    body->encoding_type = encoding_flag;

    return (MVMObject *)result;
}

void MVM_socket_close(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;

    verify_socket_type(tc, oshandle, &handle, "close socket");

    uv_close(handle->body.handle, NULL);
}

MVMObject * MVM_socket_bind(MVMThreadContext *tc, MVMObject *type_object, MVMString *address, MVMint64 port, MVMint64 protocol, MVMint64 encoding_flag) {
    MVMOSHandle *result;
    char *address_cstring;
    struct sockaddr_in bind_addr;

    ENCODING_VALID(encoding_flag);

    if (!(protocol == MVM_OSHANDLE_TCP || protocol == MVM_OSHANDLE_UDP)) {
        MVM_exception_throw_adhoc(tc, "Bind socket got an invalid protocol number (needs 4, 5; got %d)", protocol);
    }

    if (port < 1 || port > 65535) {
        MVM_exception_throw_adhoc(tc, "Bind socket got an invalid port (need between 1 and 65535; got %d)", port);
    }

    address_cstring = MVM_string_utf8_encode_C_string(tc, address);
    if (strlen(address_cstring) == 0) {
        free(address_cstring);
        MVM_exception_throw_adhoc(tc, "Bind socket needs an IP address or 0.0.0.0");
    }

    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        free(address_cstring);
        MVM_exception_throw_adhoc(tc, "Bind socket needs a type object with MVMOSHandle REPR");
    }

    /* initialize the object */
    result    = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    uv_ip4_addr(address_cstring, port, &bind_addr);

    free(address_cstring);

    switch (protocol) {
        case MVM_OSHANDLE_TCP: {
            MVMOSHandleBody * const body = &result->body;
            uv_tcp_t *server = malloc(sizeof(uv_tcp_t));
            uv_tcp_init(tc->loop, server);
            uv_tcp_bind(server, &bind_addr);
            body->handle = (uv_handle_t *)server;
            body->handle->data = body;   /* this is needed in tcp_stream_on_read function. */
            body->type = MVM_OSHANDLE_TCP;
            body->encoding_type = encoding_flag;
            break;
        }
        case MVM_OSHANDLE_UDP: {
            MVMOSHandleBody * const body = &result->body;
            uv_udp_t *server = malloc(sizeof(uv_udp_t));
            uv_udp_init(tc->loop, server);
            uv_udp_bind(server, &bind_addr, 0);
            body->handle = (uv_handle_t *)server;
            body->handle->data = body;    /* this is needed in udp_stream_on_read function. */
            body->type = MVM_OSHANDLE_UDP;
            body->encoding_type = encoding_flag;
            break;
        }
    }

    return (MVMObject *)result;
}

void MVM_socket_listen(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 backlog_size) {
    MVMOSHandle *handle;
    int r;

    verify_socket_type(tc, oshandle, &handle, "listen socket");

    if ((r = uv_listen((uv_stream_t *)handle->body.handle, (int)backlog_size, NULL)) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to listen to the socket: %s", uv_strerror(r));
    }
}

MVMObject * MVM_socket_accept(MVMThreadContext *tc, MVMObject *oshandle/*, MVMint64 timeout*/) {
    MVMOSHandle *handle;
    MVMOSHandle *result;
    MVMOSHandleBody *body;
    uv_tcp_t *client;
    int r;

    verify_socket_type(tc, oshandle, &handle, "socket accept");


    client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));

    uv_tcp_init(tc->loop, client);

    /* XXX TODO: set the timeout if one is provided */
    if ((r = uv_accept((uv_stream_t *)handle->body.handle, (uv_stream_t*) client)) != 0) {
        free(client);
        MVM_exception_throw_adhoc(tc, "Socket accept failed to get connection: %s", uv_strerror(r));
    }

    /* inherit the type object of the originating socket */
    result = (MVMOSHandle *)REPR(STABLE(oshandle)->WHAT)->allocate(tc, STABLE(STABLE(oshandle)->WHAT));
    body = &handle->body;
    body->handle = (uv_handle_t *)client;
    body->handle->data = body;      /* this is needed in tcp_stream_on_read function. */
    body->type   = MVM_OSHANDLE_TCP;
    body->encoding_type = handle->body.encoding_type;

    return (MVMObject *)result;
}

MVMint64 MVM_socket_send_string(MVMThreadContext *tc, MVMObject *oshandle, MVMString *tosend, MVMint64 start, MVMint64 length) {
    MVMOSHandle *handle;
    MVMint64 output_size;
    MVMuint8 *output;
    uv_write_t req;
    uv_buf_t buf;
    int r;

    verify_socket_type(tc, oshandle, &handle, "send string to socket");

    output = MVM_string_encode(tc, tosend, start, length, &output_size, handle->body.encoding_type);

    buf.base = output;
    buf.len  = output_size;

    if ((r = uv_write(&req, (uv_stream_t *)handle->body.handle, &buf, 1, NULL)) < 0) {
        free(output);
        MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s", uv_strerror(r));
    }

    free(output);

    return (MVMint64)output_size;
}

static void stream_on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    const MVMint64 length = ((MVMOSHandleBody *)(handle->data))->length;

    buf->base = malloc(length);
    buf->len = length;
}

static void tcp_stream_on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    MVMOSHandleBody * const body = (MVMOSHandleBody *)(handle->data);

    body->data = buf->base;
    body->length = buf->len;
}

static void udp_stream_on_read(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf,
    const struct sockaddr* addr, unsigned flags) {
    MVMOSHandleBody * const body = (MVMOSHandleBody *)(handle->data);

    body->data = buf->base;
    body->length = buf->len;
}

/* reads a string from a filehandle. */
MVMString * MVM_socket_receive_string(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 length) {
    MVMString *result;
    MVMOSHandle *handle;
    char *buf;
    MVMint64 bytes_read;

    /* XXX TODO handle length == -1 to mean read to EOF */

    verify_socket_type(tc, oshandle, &handle, "receive string from socket");

    if (length < 1 || length > 99999999) {
        MVM_exception_throw_adhoc(tc, "receive string from socket length out of bounds");
    }

    switch (handle->body.type) {
        case MVM_OSHANDLE_TCP: {
            MVMOSHandleBody * const body = &handle->body;
            body->length = length;
            uv_read_start((uv_stream_t *)body->handle, stream_on_alloc, tcp_stream_on_read);
            buf = body->data;
            bytes_read = body->length;
            break;
        }
        case MVM_OSHANDLE_UDP: {
            MVMOSHandleBody * const body = &handle->body;
            body->length = length;
            uv_udp_recv_start((uv_udp_t *)body->handle, stream_on_alloc, udp_stream_on_read);
            buf = body->data;
            bytes_read = body->length;
            break;
        }
        default:
            break;
    }

    result = MVM_string_decode(tc, tc->instance->VMString, buf, bytes_read, handle->body.encoding_type);

    free(buf);

    return result;
}
