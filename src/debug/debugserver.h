struct MVMDebugServerHandleTableEntry {
    MVMuint64 id;
    MVMObject *target;
};

struct MVMDebugServerHandleTable {
    MVMuint32 allocated;
    MVMuint32 used;

    MVMuint64 next_id;

    MVMDebugServerHandleTableEntry *entries;
};

struct MVMDebugServerBreakpointInfo {
    MVMuint64 breakpoint_id;
    MVMuint32 line_no;

    MVMuint8 shall_suspend;
    MVMuint8 send_backtrace;
};

struct MVMDebugServerBreakpointFileTable {
    char *filename;
    MVMuint32 filename_length;
    MVMuint32 lines_active_alloc;

    MVMuint8 *lines_active;

    MVMDebugServerBreakpointInfo *breakpoints;
    MVMuint32 breakpoints_alloc;
    MVMuint32 breakpoints_used;
};

struct MVMDebugServerBreakpointTable {
    MVMDebugServerBreakpointFileTable *files;
    MVMuint32 files_used;
    MVMuint32 files_alloc;
};

struct MVMDebugServerData {
    /* Debug Server thread */
    uv_thread_t thread;

    /* Protect the debugserver-related condvars */
    uv_mutex_t mutex_cond;

    /* Protect sending data on the network */
    uv_mutex_t mutex_network_send;

    /* Protect the open requests list */
    uv_mutex_t mutex_request_list;

    /* Condition variable to tell threads to check their state for changes
     * like "i should suspend" */
    uv_cond_t tell_threads;

    /* Condition variable to tell the worker to check thread states
     * for changes like "i just suspended" */
    uv_cond_t tell_worker;

    MVMuint32 port;
    MVMuint32 thread_id;

    MVMuint64 event_id;

    MVMDebugServerHandleTable *handle_table;

    MVMDebugServerBreakpointTable *breakpoints;
    MVMuint32 any_breakpoints_at_all;
    MVMuint32 breakpoints_alloc;
    MVMuint32 breakpoints_used;
    uv_mutex_t mutex_breakpoints;

    void *messagepack_data;

    MVMuint8 debugspam_network;
    MVMuint8 debugspam_protocol;
};

MVM_PUBLIC void MVM_debugserver_init(MVMThreadContext *tc, MVMuint32 port);
MVM_PUBLIC void MVM_debugserver_mark_handles(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot);

MVM_PUBLIC void MVM_debugserver_notify_thread_creation(MVMThreadContext *tc);
MVM_PUBLIC void MVM_debugserver_notify_thread_destruction(MVMThreadContext *tc);

MVM_PUBLIC void MVM_debugserver_notify_unhandled_exception(MVMThreadContext *tc, MVMException *ex);

MVM_PUBLIC void MVM_debugserver_register_line(MVMThreadContext *tc, char *filename, MVMuint32 filename_len, MVMuint32 line_no,  MVMuint32 *file_idx);
MVM_PUBLIC void MVM_debugserver_breakpoint_check(MVMThreadContext *tc, MVMuint32 file_idx, MVMuint32 line_no);
