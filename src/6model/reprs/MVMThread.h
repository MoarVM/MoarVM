/* Various stages a thread can be in. */
typedef enum {
    MVM_thread_stage_unstarted = 0,
    MVM_thread_stage_starting = 1,
    MVM_thread_stage_waiting = 2,
    MVM_thread_stage_started = 3,
    MVM_thread_stage_exited = 4,
    MVM_thread_stage_clearing_nursery = 5,
    MVM_thread_stage_destroyed = 6
} MVMThreadStages;

/* Representation used for VM thread handles. */
struct MVMThreadBody {
    /* The code object we will invoke to start the thread.. */
    MVMObject *invokee;

    /* The underlying OS thread handle. */
    uv_thread_t thread;

    /* The thread context for the thread. */
    MVMThreadContext *tc;

    /* Next in tc's threads list. */
    MVMThread *next;

    /* The current stage the thread is in (one of MVMThreadStages). */
    AO_t stage;

    /* Thread's OS-level thread ID. */
    MVMint64 native_thread_id;

    /* Copy of the VM-level thread ID (also stored in the ThreadContext while
     * the thread is alive). */
    MVMuint32 thread_id;

    /* Non-zero if the thread should not block shutdown of the VM (those with
     * zero in here will be joined when the main thread ends). */
    MVMint32 app_lifetime;
};
struct MVMThread {
    MVMObject common;
    MVMThreadBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMThread_initialize(MVMThreadContext *tc);
