/* Turn this flag on to enable various bits of GC debugging sanity checks. */
#define MVM_GC_DEBUG 0

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
