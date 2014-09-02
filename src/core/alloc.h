MVM_STATIC_INLINE void * MVM_malloc(size_t len) {
    void *p;
    p = malloc(len);
    if (!p)
        MVM_panic_allocation_failed(len);

    return p;
}

MVM_STATIC_INLINE void * MVM_realloc(void *p, size_t len) {
    void *n;
    n = realloc(p, len);

    if (!n && len > 0)
        MVM_panic_allocation_failed(len);

    return n;
}

MVM_STATIC_INLINE void MVM_free(void *p) {
    free(p);
}
