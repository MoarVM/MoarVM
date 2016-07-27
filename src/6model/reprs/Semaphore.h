/* Representation used for VM thread handles. */
struct MVMSemaphoreBody {
    uv_sem_t *sem;
};
struct MVMSemaphore {
    MVMObject common;
    MVMSemaphoreBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMSemaphore_initialize(MVMThreadContext *tc);

/* Acquire and release functions. */
MVMint64 MVM_semaphore_tryacquire(MVMThreadContext *tc, MVMSemaphore *sem);
void MVM_semaphore_acquire(MVMThreadContext *tc, MVMSemaphore *sem);
void MVM_semaphore_release(MVMThreadContext *tc, MVMSemaphore *sem);
