typedef struct {
    MVMuint64 id;
    MVMObject *target;
} MVMDebugServerHandleTableEntry;

typedef struct MVMDebugServerHandleTable {
    MVMuint32 allocated;
    MVMuint32 used;

    MVMuint64 next_id;

    MVMDebugServerHandleTableEntry *entries;
} MVMDebugServerHandleTable;

void MVM_debugserver_init(MVMThreadContext *tc, MVMuint32 port);
void MVM_debugserver_mark_handles(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot);

void MVM_debugserver_notify_thread_creation(MVMThreadContext *tc);
void MVM_debugserver_notify_thread_destruction(MVMThreadContext *tc);
