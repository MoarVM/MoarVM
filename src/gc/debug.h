/* Include this file after moar.h in a source file to get some help with GC
 * debugging. */

#define MVM_ASSERT_NOT_FROMSPACE(tc, c) do { \
    if (tc->nursery_fromspace && \
            (char *)(c) >= (char *)tc->nursery_fromspace && \
            (char *)(c) < (char *)tc->nursery_fromspace + MVM_NURSERY_SIZE) \
        MVM_panic(1, "Collectable %p in fromspace accessed", c); \
} while (0)
