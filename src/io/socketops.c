#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool


static void verify_socket_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {

    /* work on only MVMOSHandle of type MVM_OSHANDLE_FILE */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle");
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.handle_type != MVM_OSHANDLE_SOCKET) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type socket");
    }
}

MVMObject * MVM_socket_connect(MVMThreadContext *tc, MVMObject *type_object, MVMString *hostname, MVMint64 port, MVMint64 protocol) {
    MVMOSHandle *result;
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    apr_socket_t *socket;
    apr_sockaddr_t *sa;
    int family = APR_INET; /* TODO: detect family from ip address format or the ip address resolving from the hostname */
    int type = SOCK_STREAM;
    int protocol_int = (int)protocol;
    char *hostname_cstring;
    
    if (!(protocol_int == APR_PROTO_TCP || protocol_int == APR_PROTO_UDP || protocol_int == APR_PROTO_SCTP)) {
        MVM_exception_throw_adhoc(tc, "Open socket got an invalid protocol number (needs 6, 17, or 132; got %d)", protocol_int);
    }
    
    if (port < 1 || port > 65535) {
        MVM_exception_throw_adhoc(tc, "Open socket got an invalid port (need between 1 and 65535; got %d)", port);
    }
    
    hostname_cstring = MVM_string_utf8_encode_C_string(tc, hostname);
    if (strlen(hostname_cstring) == 0) {
        MVM_exception_throw_adhoc(tc, "Open socket needs a hostname or IP address");
    }
    
    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        MVM_exception_throw_adhoc(tc, "Open socket needs a type object with MVMOSHandle REPR");
    }
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Open socket failed to create pool: ");
    }
    
    if ((rv = apr_socket_create(&socket, family, type, protocol_int, tmp_pool)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Open socket failed to create socket: ");
    }
    
    if ((rv = apr_sockaddr_info_get(&sa, (const char *)hostname_cstring, APR_UNSPEC, (apr_port_t)port, APR_IPV4_ADDR_OK, tmp_pool)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Open socket failed to study address/port: ");
    }
    
    if ((rv = apr_socket_connect(socket, sa)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Open socket failed to connect: ");
    }
    
    /* initialize the object */
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    
    result->body.socket = socket;
    result->body.handle_type = MVM_OSHANDLE_SOCKET;
    result->body.mem_pool = tmp_pool;
    
    free(hostname_cstring);
    
    return (MVMObject *)result;
}

void MVM_socket_close(MVMThreadContext *tc, MVMObject *oshandle) {
    apr_status_t rv;
    MVMOSHandle *handle;
    
    verify_socket_type(tc, oshandle, &handle, "close socket");
    
    if ((rv = apr_socket_close(handle->body.socket)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to close the socket: ");
    }
}

MVMObject * MVM_socket_bind(MVMThreadContext *tc, MVMObject *type_object, MVMString *address, MVMint64 port, MVMint64 protocol) {
    MVMOSHandle *result;
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    apr_socket_t *socket;
    apr_sockaddr_t *sa;
    int family = APR_INET; /* TODO: detect family from ip address format */
    int type = SOCK_STREAM;
    int protocol_int = (int)protocol;
    char *address_cstring;
    
    if (!(protocol_int == APR_PROTO_TCP || protocol_int == APR_PROTO_UDP || protocol_int == APR_PROTO_SCTP)) {
        MVM_exception_throw_adhoc(tc, "Bind socket got an invalid protocol number (needs 6, 17, or 132; got %d)", protocol_int);
    }
    
    if (port < 1 || port > 65535) {
        MVM_exception_throw_adhoc(tc, "Bind socket got an invalid port (need between 1 and 65535; got %d)", port);
    }
    
    address_cstring = MVM_string_utf8_encode_C_string(tc, address);
    if (strlen(address_cstring) == 0) {
        MVM_exception_throw_adhoc(tc, "Bind socket needs an IP address or 0.0.0.0");
    }
    
    if (REPR(type_object)->ID != MVM_REPR_ID_MVMOSHandle || IS_CONCRETE(type_object)) {
        MVM_exception_throw_adhoc(tc, "Bind socket needs a type object with MVMOSHandle REPR");
    }
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Bind socket failed to create pool: ");
    }
    
    if ((rv = apr_socket_create(&socket, family, type, protocol_int, tmp_pool)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Bind socket failed to create socket: ");
    }
    
    if ((rv = apr_sockaddr_info_get(&sa, (const char *)address_cstring, APR_UNSPEC, (apr_port_t)port, APR_IPV4_ADDR_OK, tmp_pool)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Bind socket failed to study address/port: ");
    }
    
    if ((rv = apr_socket_bind(socket, sa)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to bind socket: ");
    }
    
    /* initialize the object */
    result = (MVMOSHandle *)REPR(type_object)->allocate(tc, STABLE(type_object));
    
    result->body.socket = socket;
    result->body.handle_type = MVM_OSHANDLE_SOCKET;
    result->body.mem_pool = tmp_pool;
    
    free(address_cstring);
    
    return (MVMObject *)result;
}

void MVM_socket_listen(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 backlog_size) {
    apr_status_t rv;
    MVMOSHandle *handle;
    
    verify_socket_type(tc, oshandle, &handle, "listen socket");
    
    /* blocks until a connection is received, I think; can't really test it in the test suite */
    if ((rv = apr_socket_listen(handle->body.socket, (apr_int32_t)backlog_size)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to listen to the socket: ");
    }
}
