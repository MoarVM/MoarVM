void * MVM_gc_allocate(MVMThreadContext *tc, size_t size);
void * MVM_gc_allocate_zeroed(MVMThreadContext *tc, size_t size);
MVMSTable * MVM_gc_allocate_stable(MVMThreadContext *tc, MVMREPROps *repr, MVMObject *how);
MVMObject * MVM_gc_allocate_type_object(MVMThreadContext *tc, MVMSTable *st);
MVMObject * MVM_gc_allocate_object(MVMThreadContext *tc, MVMSTable *st, size_t size);
