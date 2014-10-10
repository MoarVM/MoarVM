MVM_STATIC_INLINE void * MVM_malloc(size_t len) {
    void *ptr = malloc(len);

    if (!ptr)
        MVM_panic_allocation_failed(len);

    return ptr;
}

MVM_STATIC_INLINE void * MVM_calloc(size_t len) {
    void *ptr = calloc(len);

    if (!ptr)
        MVM_panic_allocation_failed(len);

    return ptr;
}

MVM_STATIC_INLINE void * MVM_realloc(void *p, size_t len) {
    void *ptr = realloc(p, len);

    if (!ptr && len > 0)
        MVM_panic_allocation_failed(len);

    return ptr;
}

MVM_STATIC_INLINE void MVM_free(void *p) {
    free(p);
}
