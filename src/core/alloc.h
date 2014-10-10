MVM_STATIC_INLINE void * MVM_malloc(size_t size) {
    void *ptr = malloc(size);

    if (!ptr)
        MVM_panic_allocation_failed(size);

    return ptr;
}

MVM_STATIC_INLINE void * MVM_calloc(size_t num, size_t size) {
    void *ptr = calloc(num, size);

    if (!ptr)
        MVM_panic_allocation_failed(size);

    return ptr;
}

MVM_STATIC_INLINE void * MVM_realloc(void *p, size_t size) {
    void *ptr = realloc(p, size);

    if (!ptr && size > 0)
        MVM_panic_allocation_failed(size);

    return ptr;
}

MVM_STATIC_INLINE void MVM_free(void *p) {
    free(p);
}
