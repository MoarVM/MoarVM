typedef enum {
    SOCKET_FAMILY_UNSPEC,
    SOCKET_FAMILY_INET,
    SOCKET_FAMILY_INET6,
    SOCKET_FAMILY_UNIX
} MVMSocketFamily;

typedef enum {
    SOCKET_TYPE_STREAM = 1,
    SOCKET_TYPE_DGRAM,
    SOCKET_TYPE_RAW,
    SOCKET_TYPE_RDM,
    SOCKET_TYPE_SEQPACKET
} MVMSocketType;

typedef enum {
    SOCKET_PROTOCOL_TCP = 1,
    SOCKET_PROTOCOL_UDP,
    SOCKET_PROTOCOL_RAW
} MVMSocketProtocol;

MVMObject * MVM_io_socket_create(MVMThreadContext *tc, MVMint64 listen);
struct addrinfo * MVM_io_resolve_host_name(
    MVMThreadContext *tc,
    MVMString        *host,
    MVMint64          port,
    MVMuint16         family,
    MVMint64          type,
    MVMint64          protocol,
    MVMint64          passive
);
MVMString * MVM_io_get_hostname(MVMThreadContext *tc);
