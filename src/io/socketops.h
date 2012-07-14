MVMObject * MVM_socket_connect(MVMThreadContext *tc, MVMObject *type_object, MVMString *hostname, MVMint64 port, MVMint64 protocol, MVMint64 encoding_flag);
void MVM_socket_close(MVMThreadContext *tc, MVMObject *oshandle);
MVMObject * MVM_socket_bind(MVMThreadContext *tc, MVMObject *type_object, MVMString *address, MVMint64 port, MVMint64 protocol, MVMint64 encoding_flag);
void MVM_socket_listen(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 backlog_size);
MVMObject * MVM_socket_accept(MVMThreadContext *tc, MVMObject *oshandle/*, MVMint64 timeout*/);
void MVM_socket_send_string(MVMThreadContext *tc, MVMObject *oshandle, MVMString *tosend);
MVMString * MVM_socket_receive_string(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 length);
MVMString * MVM_socket_hostname(MVMThreadContext *tc);
