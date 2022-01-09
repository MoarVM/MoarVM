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

MVM_STATIC_INLINE void MVM_free_at_safepoint(MVMThreadContext *tc, void *ptr) {
    MVM_VECTOR_PUSH(tc->instance->free_at_safepoint, ptr);
}

MVM_STATIC_INLINE void MVM_alloc_safepoint(MVMThreadContext *tc) {
    /* No need to acquire mutex since we're in the GC when calling this. */
    while (MVM_VECTOR_ELEMS(tc->instance->free_at_safepoint))
        MVM_free(MVM_VECTOR_POP(tc->instance->free_at_safepoint));
}
