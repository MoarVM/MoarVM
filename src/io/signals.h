/* MoarVM platform-independent signal values. */
#define MVM_SIG_INT     1
#define MVM_SIG_BREAK   2
#define MVM_SIG_HUP     3
#define MVM_SIG_WINCH   4

MVMObject * MVM_io_signal_handle(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMint64 signal, MVMObject *async_type);
