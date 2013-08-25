#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->body.pool

static void verify_socket_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {

    /* work on only MVMOSHandle of type MVM_OSHANDLE_FILE */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle", msg);
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.type != MVM_OSHANDLE_SOCKET) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type socket", msg);
    }
}

MVMObject * MVM_socket_connect(MVMThreadContext *tc, MVMObject *type_object, MVMString *hostname, MVMint64 port, MVMint64 protocol, MVMint64 encoding_flag) {
    MVMOSHandle *result;
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    apr_socket_t *socket;
    apr_sockaddr_t *sa;
    int family = APR_INET; /* TODO: detect family from ip address format or the ip address resolving from the hostname */
    int type = SOCK_STREAM;
    int protocol_int = (int)protocol;
    char *hostname_cstring;

    ENCODING_VALID(encoding_flag);

    if (!(protocol_int == APR_PROTO_TCP || protocol_int == APR_PROTO_UDP || protocol_int == APR_PROTO_SCTP)) {
        MVM_exception_throw_adhoc(tc, "Open socket got an invalid protocol number (needs 6, 17, or 132; got %d)", protocol_int);
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

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(hostname_cstring);
        MVM_exception_throw_apr_error(tc, rv, "Open socket failed to create pool: ");
    }

    if ((rv = apr_socket_create(&socket, family, type, protocol_int, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        free(hostname_cstring);
        MVM_exception_throw_apr_error(tc, rv, "Open socket failed to create socket: ");
    }

    if ((rv = apr_sockaddr_info_get(&sa, (const char *)hostname_cstring, APR_UNSPEC, (apr_port_t)port, APR_IPV4_ADDR_OK, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        free(hostname_cstring);
        MVM_exception_throw_apr_error(tc, rv, "Open socket failed to study address/port: ");
    }

    free(hostname_cstring);

    if ((rv = apr_socket_connect(socket, sa)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Open socket failed to connect: ");
    }

    /* initialize the object */
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));

    result->body.socket = socket;
    result->body.type = MVM_OSHANDLE_SOCKET;
    result->body.mem_pool = tmp_pool;
    result->body.encoding_type = encoding_flag;

    return (MVMObject *)result;
}

void MVM_socket_close(MVMThreadContext *tc, MVMObject *oshandle) {
    MVMOSHandle *handle;

    verify_socket_type(tc, oshandle, &handle, "close socket");

    uv_close(handle->body.handle, NULL);
}

MVMObject * MVM_socket_bind(MVMThreadContext *tc, MVMObject *type_object, MVMString *address, MVMint64 port, MVMint64 protocol, MVMint64 encoding_flag) {
    MVMOSHandle *result;
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    apr_socket_t *socket;
    apr_sockaddr_t *sa;
    int family = APR_INET; /* TODO: detect family from ip address format */
    int type = SOCK_STREAM;
    int protocol_int = (int)protocol;
    char *address_cstring;

    ENCODING_VALID(encoding_flag);

    if (!(protocol_int == APR_PROTO_TCP || protocol_int == APR_PROTO_UDP || protocol_int == APR_PROTO_SCTP)) {
        MVM_exception_throw_adhoc(tc, "Bind socket got an invalid protocol number (needs 6, 17, or 132; got %d)", protocol_int);
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

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(address_cstring);
        MVM_exception_throw_apr_error(tc, rv, "Bind socket failed to create pool: ");
    }

    if ((rv = apr_socket_create(&socket, family, type, protocol_int, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        free(address_cstring);
        MVM_exception_throw_apr_error(tc, rv, "Bind socket failed to create socket: ");
    }

    if ((rv = apr_sockaddr_info_get(&sa, (const char *)address_cstring, APR_UNSPEC, (apr_port_t)port, APR_IPV4_ADDR_OK, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        free(address_cstring);
        MVM_exception_throw_apr_error(tc, rv, "Bind socket failed to study address/port: ");
    }

    free(address_cstring);

    if ((rv = apr_socket_bind(socket, sa)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to bind socket: ");
    }

    /* initialize the object */
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));

    result->body.socket = socket;
    result->body.type = MVM_OSHANDLE_SOCKET;
    result->body.mem_pool = tmp_pool;
    result->body.encoding_type = encoding_flag;

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
    apr_status_t rv;
    MVMOSHandle *handle;
    MVMOSHandle *result;
    apr_pool_t *tmp_pool;
    apr_socket_t *new_socket;

    verify_socket_type(tc, oshandle, &handle, "socket accept");

    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Socket accept failed to create pool: ");
    }

    /* XXX TODO: set the timeout if one is provided */
    if ((rv = apr_socket_accept(&new_socket, handle->body.socket, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Socket accept failed to get connection: ");
    }

    /* inherit the type object of the originating socket */
    result = (MVMOSHandle *)REPR(STABLE(oshandle)->WHAT)->allocate(tc, STABLE(STABLE(oshandle)->WHAT));

    result->body.socket = new_socket;
    result->body.type = MVM_OSHANDLE_SOCKET;
    result->body.mem_pool = tmp_pool;
    result->body.encoding_type = handle->body.encoding_type;

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

    output = MVM_encode_string_to_C_buffer(tc, tosend, start, length, &output_size, handle->body.encoding_type);

    buf.base = output;
    buf.len  = output_size;

    if ((r = uv_write(&req, (uv_stream_t *)handle->body.handle, &buf, 1, NULL)) < 0) {
        free(output);
        MVM_exception_throw_adhoc(tc, "Failed to write bytes to filehandle: %s", uv_strerror(r));
    }

    free(output);

    return (MVMint64)output_size;
}

static uv_buf_t stream_on_alloc(uv_handle_t *handle, size_t suggested_size) {
    const MVMint64 length = ((MVMOSHandleBody *)(handle->data))->length;
    uv_buf_t buf;

    buf.base = malloc(length);
    buf.len = length;

    return buf;
}

static void tcp_stream_on_read(uv_stream_t *handle, ssize_t nread, uv_buf_t buf) {
    MVMOSHandleBody * const body = (MVMOSHandleBody *)(handle->data);

    body->data = buf.base;
    body->length = buf.len;
}

static void udp_stream_on_read(uv_udp_t *handle, ssize_t nread, uv_buf_t buf,
    struct sockaddr* addr, unsigned flags) {
    MVMOSHandleBody * const body = (MVMOSHandleBody *)(handle->data);

    body->data = buf.base;
    body->length = buf.len;
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

    result = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, buf, bytes_read, handle->body.encoding_type);

    free(buf);

    return result;
}
