MVMObject * MVM_io_socket_create(MVMThreadContext *tc, MVMint64 listen);
struct sockaddr * MVM_io_resolve_host_name(MVMThreadContext *tc, MVMString *host, MVMint64 port);
MVMString * MVM_io_get_hostname(MVMThreadContext *tc);
