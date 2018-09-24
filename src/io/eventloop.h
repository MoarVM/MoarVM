/* Operations table for a certain type of asynchronous task that can be run on
 * the event loop. */
struct MVMAsyncTaskOps {
    /* How to set work up on the event loop. */
    void (*setup) (MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data);

    /* How to grant emit permits, if possible. */
    void (*permit) (MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data,
            MVMint64 channel, MVMint64 permits);

    /* How to cancel, if possible. */
    void (*cancel) (MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data);

    /* How to mark the task's data, if needed. */
    void (*gc_mark) (MVMThreadContext *tc, void *data, MVMGCWorklist *worklist);

    /* How to free the task's data, if needed. */
    void (*gc_free) (MVMThreadContext *tc, MVMObject *t, void *data);
};

void MVM_io_eventloop_queue_work(MVMThreadContext *tc, MVMObject *work);
void MVM_io_eventloop_permit(MVMThreadContext *tc, MVMObject *task_obj,
    MVMint64 channel, MVMint64 permits);
void MVM_io_eventloop_cancel_work(MVMThreadContext *tc, MVMObject *task_obj,
    MVMObject *notify_queue, MVMObject *notify_schedulee);
void MVM_io_eventloop_send_cancellation_notification(MVMThreadContext *tc, MVMAsyncTask *task_obj);

int MVM_io_eventloop_add_active_work(MVMThreadContext *tc, MVMObject *async_task);
MVMAsyncTask * MVM_io_eventloop_get_active_work(MVMThreadContext *tc, int work_idx);
void MVM_io_eventloop_remove_active_work(MVMThreadContext *tc, int *work_idx_to_clear);

void MVM_io_eventloop_start(MVMThreadContext *tc);
void MVM_io_eventloop_stop(MVMThreadContext *tc);
void MVM_io_eventloop_join(MVMThreadContext *tc);
void MVM_io_eventloop_destroy(MVMThreadContext *tc);
