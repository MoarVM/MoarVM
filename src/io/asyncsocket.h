MVMObject * MVM_io_socket_connect_async(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMString *host, MVMint64 port, MVMObject *async_type);
MVMObject * MVM_io_socket_listen_async(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMString *host, MVMint64 port, MVMint32 backlog, MVMObject *async_type);

MVMObject * MVM_io_socket_connect_unix_async(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMString *path, MVMObject *async_type);
MVMObject * MVM_io_socket_listen_unix_async(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMString *path, MVMint32 backlog, MVMObject *async_type);
