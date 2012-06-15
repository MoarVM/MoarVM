MVMObject * MVM_socket_connect(MVMThreadContext *tc, MVMObject *type_object, MVMString *hostname, MVMint64 port, MVMint64 protocol);
void MVM_socket_close(MVMThreadContext *tc, MVMObject *oshandle);
