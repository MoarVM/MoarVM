/* This flag enables/disables various bits of GC debugging sanity checks:
 * 0 = No checking
 * 1 = Checks on reference assignments and other relatively cheap cases
 * 2 = Checks on every object register access (slow)
 */
#define MVM_GC_DEBUG 0

#if MVM_GC_DEBUG
#define MVM_ASSERT_NOT_FROMSPACE(tc, c) do { \
    MVMThread *cur_thread = tc->instance->threads; \
    while (cur_thread) { \
        MVMThreadContext *thread_tc = cur_thread->body.tc; \
        if (thread_tc && thread_tc->nursery_fromspace && \
                (char *)(c) >= (char *)thread_tc->nursery_fromspace && \
                (char *)(c) < (char *)thread_tc->nursery_fromspace + MVM_NURSERY_SIZE) \
            MVM_panic(1, "Collectable %p in fromspace accessed", c); \
        cur_thread = cur_thread->body.next; \
    } \
} while (0)
#else
#define MVM_ASSERT_NOT_FROMSPACE(tc, c)
#endif
