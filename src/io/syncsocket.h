typedef enum {
    SOCKET_FAMILY_UNSPEC,
    SOCKET_FAMILY_INET,
    SOCKET_FAMILY_INET6,
    SOCKET_FAMILY_UNIX
} MVMSocketFamily;

typedef void (*MVMIOGetUsableAddressCB)(
    MVMThreadContext  *tc,
    char              *host_cstr,
    int                port,
    unsigned short     family,
    struct addrinfo  **result,
    void              *misc_data
);

MVMObject * MVM_io_socket_create(MVMThreadContext *tc, MVMint64 listen);
void MVM_io_get_usable_address(
    MVMThreadContext         *tc,
    char                     *host_cstr,
    int                       port,
    unsigned short            family,
    struct addrinfo         **result,
    void                     *misc_data,
    MVMIOGetUsableAddressCB   on_error
);
struct addrinfo * MVM_io_resolve_host_name(
    MVMThreadContext *tc,
    MVMString        *host,
    MVMint64          port,
    MVMuint16         family,
    MVMint32          type
);
MVMString * MVM_io_get_hostname(MVMThreadContext *tc);
