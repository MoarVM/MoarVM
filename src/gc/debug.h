/* Turn this flag on to enable various bits of GC debugging sanity checks. */
#define MVM_GC_DEBUG 1

#define MVM_ASSERT_NOT_FROMSPACE(tc, c) do { \
    if (tc->nursery_fromspace && \
            (char *)(c) >= (char *)tc->nursery_fromspace && \
            (char *)(c) < (char *)tc->nursery_fromspace + MVM_NURSERY_SIZE) \
        MVM_panic(1, "Collectable %p in fromspace accessed", c); \
} while (0)
