void MVM_gc_root_add_permanent(MVMThreadContext *tc, MVMCollectable **obj_ref);
void MVM_gc_root_add_parmanents_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_gc_root_temp_push(MVMThreadContext *tc, MVMCollectable **obj_ref);
void MVM_gc_root_temp_pop(MVMThreadContext *tc);
void MVM_gc_root_temp_pop_n(MVMThreadContext *tc, MVMuint32 n);
void MVM_gc_root_add_temps_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_gc_root_add_frame_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *start_frame);
