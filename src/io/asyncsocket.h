MVMObject * MVM_io_socket_create_async(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMObject *async_type);
MVMObject * MVM_io_socket_connect_async(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMObject *socket, MVMString *host, MVMint64 port, MVMObject *async_type);
MVMObject * MVM_io_socket_listen_async(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMObject *socket, MVMString *host, MVMint64 port, MVMint32 backlog, MVMObject *async_type);
