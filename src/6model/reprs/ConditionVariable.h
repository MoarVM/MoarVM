/* Representation used for condition variables. */
struct MVMConditionVariableBody {
    /* The ReentrantMutex this condition variable is associated with. */
    MVMObject *mutex;

    /* The condition variable itself, held at a level of indirection to keep
     * OSes that wouldn't like it moving around happy. */
    uv_cond_t *condvar;
};
struct MVMConditionVariable {
    MVMObject common;
    MVMConditionVariableBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMConditionVariable_initialize(MVMThreadContext *tc);

/* Operations on a condition variable. */
MVMObject * MVM_conditionvariable_from_lock(MVMThreadContext *tc, MVMReentrantMutex *lock, MVMObject *type);
void MVM_conditionvariable_wait(MVMThreadContext *tc, MVMConditionVariable *cv);
void MVM_conditionvariable_signal_one(MVMThreadContext *tc, MVMConditionVariable *cv);
void MVM_conditionvariable_signal_all(MVMThreadContext *tc, MVMConditionVariable *cv);
