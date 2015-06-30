/* Data that we keep for a pipe-based handle. */
struct MVMIOSyncPipeData {
    /* Start with same fields as a sync stream, since we will re-use most
     * of its logic. */
    MVMIOSyncStreamData ss;

    /* Also need to keep hold of the process */
    uv_process_t *process;
};

MVMObject * MVM_io_syncpipe(MVMThreadContext *tc);
