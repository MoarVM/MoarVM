typedef enum {
    SOCKET_FAMILY_UNSPEC,
    SOCKET_FAMILY_INET,
    SOCKET_FAMILY_INET6,
    SOCKET_FAMILY_UNIX
} MVMSocketFamily;

MVMObject * MVM_io_socket_create(MVMThreadContext *tc, MVMint64 listen);
struct sockaddr * MVM_io_resolve_host_name(MVMThreadContext *tc, MVMString *host, MVMint64 port, MVMuint16 family);
MVMString * MVM_io_get_hostname(MVMThreadContext *tc);
