#if !defined(MVM_CAN_UNALIGNED_INT64) || !defined(MVM_CAN_UNALIGNED_NUM64)
#define MVM_ALIGN_SIZE(size) MVM_ALIGN_SECTION(size)
#else
#define MVM_ALIGN_SIZE(size) (size)
#endif
void * MVM_gc_allocate_nursery(MVMThreadContext *tc, size_t size);
void * MVM_gc_allocate_zeroed(MVMThreadContext *tc, size_t size);
MVMSTable * MVM_gc_allocate_stable(MVMThreadContext *tc, const MVMREPROps *repr, MVMObject *how);
MVMObject * MVM_gc_allocate_type_object(MVMThreadContext *tc, MVMSTable *st);
MVMObject * MVM_gc_allocate_object(MVMThreadContext *tc, MVMSTable *st);
MVMFrame * MVM_gc_allocate_frame(MVMThreadContext *tc);
void MVM_gc_allocate_gen2_default_set(MVMThreadContext *tc);
void MVM_gc_allocate_gen2_default_clear(MVMThreadContext *tc);

MVM_STATIC_INLINE void * MVM_gc_allocate(MVMThreadContext *tc, size_t size) {
    return tc->allocate_in_gen2
        ? MVM_gc_gen2_allocate_zeroed(tc->gen2, size)
        : MVM_gc_allocate_nursery(tc, size);
}
