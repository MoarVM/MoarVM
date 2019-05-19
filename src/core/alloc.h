#include <sys/sdt.h>

MVM_STATIC_INLINE void * MVM_malloc(size_t size) {
    void *ptr = malloc(size);

    if (!ptr)
        MVM_panic_allocation_failed(size);

    return ptr;
}

MVM_STATIC_INLINE void * MVM_calloc(size_t num, size_t size) {
    void *ptr = calloc(num, size);

    if (!ptr)
        MVM_panic_allocation_failed(num * size);

    return ptr;
}

#define MVM_MALLOCOBJ(count, type_name) (MVM_malloc_named((count) * sizeof(type_name), #type_name, (count)))
#define MVM_CALLOCOBJ(count, type_name) (MVM_calloc_named((count), sizeof(type_name), #type_name))

MVM_STATIC_INLINE void * MVM_malloc_named(size_t size, const char *type_name, size_t count) {
    DTRACE_PROBE3(moarvm, mallocobj, type_name, count, size);
    return MVM_malloc(size);
}
MVM_STATIC_INLINE void * MVM_calloc_named(size_t num, size_t size, const char *type_name) {
    DTRACE_PROBE3(moarvm, callocobj, type_name, num, size);
    return MVM_calloc(num, size);
}


MVM_STATIC_INLINE void * MVM_realloc(void *p, size_t size) {
    void *ptr = realloc(p, size);

    if (!ptr && size > 0)
        MVM_panic_allocation_failed(size);

    return ptr;
}

MVM_STATIC_INLINE void * MVM_recalloc(void *p, size_t old_size, size_t size) {
    void *ptr = realloc(p, size);

    if (!ptr && size > 0)
        MVM_panic_allocation_failed(size);

    memset((char *)ptr + old_size, 0, size - old_size);

    return ptr;
}

MVM_STATIC_INLINE void MVM_free(void *p) {
    free(p);
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
