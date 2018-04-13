void MVM_cross_thread_write_instrument(MVMThreadContext *tc, MVMStaticFrame *static_frame);
void MVM_cross_thread_write_check(MVMThreadContext *tc, MVMObject *written, MVMint16 guilty);

/* Mutating operations one thread may do on an object it didn't create. */
#define MVM_CTW_BIND_ATTR       1
#define MVM_CTW_BIND_POS        2
#define MVM_CTW_PUSH            3
#define MVM_CTW_POP             4
#define MVM_CTW_SHIFT           5
#define MVM_CTW_UNSHIFT         6
#define MVM_CTW_SLICE           7
#define MVM_CTW_SPLICE          8
#define MVM_CTW_BIND_KEY        9
#define MVM_CTW_DELETE_KEY      10
#define MVM_CTW_ASSIGN          11
#define MVM_CTW_REBLESS         12
