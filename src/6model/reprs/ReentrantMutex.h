/* Representation used for VM thread handles. */
struct MVMReentrantMutexBody {
    /* The (non-reentrant) mutex supplied by libuv. Sadly, we have to hold it
     * at a level of indirection - at least on Windows - because if the object
     * is moved it causes confusion. */
    uv_mutex_t *mutex;

    /* Who currently holds the mutex, if anyone. */
    AO_t holder_id;

    /* How many times we've taken the lock. */
    AO_t lock_count;
};
struct MVMReentrantMutex {
    MVMObject common;
    MVMReentrantMutexBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMReentrantMutex_initialize(MVMThreadContext *tc);

/* Lock and unlock functions. */
void MVM_reentrantmutex_lock_checked(MVMThreadContext *tc, MVMObject *lock);
void MVM_reentrantmutex_lock(MVMThreadContext *tc, MVMReentrantMutex *rm);
void MVM_reentrantmutex_unlock_checked(MVMThreadContext *tc, MVMObject *lock);
void MVM_reentrantmutex_unlock(MVMThreadContext *tc, MVMReentrantMutex *rm);
