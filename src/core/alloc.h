MVM_STATIC_INLINE void * MVM_malloc(size_t size) {
#ifdef MVM_USE_MIMALLOC
    void *ptr = mi_malloc(size);
#else
    void *ptr = malloc(size);
#endif

    if (!ptr)
        MVM_panic_allocation_failed(size);

    return ptr;
}

MVM_STATIC_INLINE void * MVM_calloc(size_t num, size_t size) {
#ifdef MVM_USE_MIMALLOC
    void *ptr = mi_calloc(num, size);
#else
    void *ptr = calloc(num, size);
#endif

    if (!ptr)
        MVM_panic_allocation_failed(num * size);

    return ptr;
}

MVM_STATIC_INLINE void * MVM_realloc(void *p, size_t size) {
#ifdef MVM_USE_MIMALLOC
    void *ptr = mi_realloc(p, size);
#else
    void *ptr = realloc(p, size);
#endif

    if (!ptr && size > 0)
        MVM_panic_allocation_failed(size);

    return ptr;
}

MVM_STATIC_INLINE void * MVM_recalloc(void *p, size_t old_size, size_t size) {
#ifdef MVM_USE_MIMALLOC
    void *ptr = mi_realloc(p, size);
#else
    void *ptr = realloc(p, size);
#endif

    if (size > 0) {
        if (!ptr)
            MVM_panic_allocation_failed(size);

        if (size > old_size)
            memset((char *)ptr + old_size, 0, size - old_size);
    }

    return ptr;
}

MVM_STATIC_INLINE void MVM_free(void *p) {
#ifdef MVM_USE_MIMALLOC
    mi_free(p);
#else
    free(p);
#endif
}

#define MVM_free_null(addr) do { \
    MVM_free((addr)); \
    (addr) = NULL; \
} while (0)

/* Entry in the "free at safe point" linked list. */
struct MVMAllocSafepointFreeListEntry {
    void                           *to_free;
    MVMAllocSafepointFreeListEntry *next;
};

/* Race to add a piece of memory to be freed at the next global safe point.
 * A global safe point is defined as one in which all threads, since we
 * requested the freeing of the memory, have reached a safe point. */
MVM_STATIC_INLINE void MVM_free_at_safepoint(MVMThreadContext *tc, void *to_free) {
    MVMAllocSafepointFreeListEntry *orig;
    MVMAllocSafepointFreeListEntry *to_add = MVM_malloc(sizeof(MVMAllocSafepointFreeListEntry));
    to_add->to_free = to_free;
    do {
        orig = tc->instance->free_at_safepoint;
        to_add->next = orig;
    } while (!MVM_trycas(&(tc->instance->free_at_safepoint), orig, to_add));
}

MVM_STATIC_INLINE void * MVM_realloc_at_safepoint(MVMThreadContext *tc, void *p, size_t old_bytes, size_t new_bytes) {
    void *allocd;
#ifdef MVM_USE_MIMALLOC
    /* mi_expand() is guaranteed to expand in-place or returns NULL if it can't,
     * and therefore is safe to do right away (and not at a safepoint). */
    allocd = mi_expand(p, new_bytes);
    if (!allocd) {
#endif
        allocd = MVM_malloc(new_bytes);
        memcpy(allocd, p, new_bytes > old_bytes ? old_bytes : new_bytes);
        MVM_free_at_safepoint(tc, p);
#ifdef MVM_USE_MIMALLOC
    }
#endif
    return allocd;
}

/* Called when we're at a safepoint, to free everything queued up to be freed
 * at the next safepoint. Assumes that it is only called on one thread at a
 * time, while the world is stopped. */
MVM_STATIC_INLINE void MVM_alloc_safepoint(MVMThreadContext *tc) {
    MVMAllocSafepointFreeListEntry *cur, *next;
    cur = tc->instance->free_at_safepoint;
    while (cur) {
        next = cur->next;
        MVM_free(cur->to_free);
        MVM_free(cur);
        cur = next;
    }
    tc->instance->free_at_safepoint = NULL;
}
