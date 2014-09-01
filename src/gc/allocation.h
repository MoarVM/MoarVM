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

#define MVM_free free

void * MVM_gc_allocate_nursery(MVMThreadContext *tc, size_t size);
void * MVM_gc_allocate_zeroed(MVMThreadContext *tc, size_t size);
MVMSTable * MVM_gc_allocate_stable(MVMThreadContext *tc, const MVMREPROps *repr, MVMObject *how);
MVMObject * MVM_gc_allocate_type_object(MVMThreadContext *tc, MVMSTable *st);
MVMObject * MVM_gc_allocate_object(MVMThreadContext *tc, MVMSTable *st);
void MVM_gc_allocate_gen2_default_set(MVMThreadContext *tc);
void MVM_gc_allocate_gen2_default_clear(MVMThreadContext *tc);

#define MVM_gc_allocate(tc, size) (tc->allocate_in_gen2 \
    ? MVM_gc_gen2_allocate_zeroed(tc->gen2, size) \
    : MVM_gc_allocate_nursery(tc, size))
