/* Mutating operations one thread may do on an object it didn't create. */
typedef enum {
    MVM_CTW_BIND_ATTR = 1,
    MVM_CTW_BIND_POS,
    MVM_CTW_PUSH,
    MVM_CTW_POP,
    MVM_CTW_SHIFT,
    MVM_CTW_UNSHIFT,
    MVM_CTW_SPLICE,
    MVM_CTW_BIND_KEY,
    MVM_CTW_DELETE_KEY,
    MVM_CTW_ASSIGN,
    MVM_CTW_REBLESS
} MVMCtw;

void MVM_cross_thread_write_instrument(MVMThreadContext *tc, MVMStaticFrame *static_frame);
void MVM_cross_thread_write_check(MVMThreadContext *tc, const MVMObject *written, MVMCtw guilty);

