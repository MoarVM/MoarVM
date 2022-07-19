/* Representation used for VM thread handles. */
struct MVMSemaphoreBody {
    volatile AO_t count;
    volatile AO_t waits;
};
struct MVMSemaphore {
    MVMObject common;
    MVMSemaphoreBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMSemaphore_initialize(MVMThreadContext *tc);

/* Acquire and release functions. */
MVMuint64 MVM_semaphore_tryacquire(MVMThreadContext *tc, MVMSemaphore *sem);
void MVM_semaphore_acquire(MVMThreadContext *tc, MVMSemaphore *sem);
void MVM_semaphore_release(MVMThreadContext *tc, MVMSemaphore *sem);
