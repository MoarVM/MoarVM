MVMObject * MVM_io_get_signals(MVMThreadContext *tc);
MVMObject * MVM_io_signal_handle(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMint64 signal, MVMObject *async_type);
