#include "moar.h"

MVMObject * MVM_io_socket_connect_async(MVMThreadContext *tc, MVMObject *queue,
                                        MVMObject *schedulee, MVMString *host,
                                        MVMint64 port, MVMObject *async_type) {
    MVM_exception_throw_adhoc(tc, "async connect NYI");
}

MVMObject * MVM_io_socket_listen_async(MVMThreadContext *tc, MVMObject *queue,
                                       MVMObject *schedulee, MVMString *host,
                                       MVMint64 port, MVMObject *async_type) {
    MVM_exception_throw_adhoc(tc, "async listen NYI");
}
