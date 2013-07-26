/* Functions related to roots. */
void MVM_gc_root_add_permanent(MVMThreadContext *tc, MVMCollectable **obj_ref);
void MVM_gc_root_add_permanents_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_gc_root_add_instance_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_gc_root_add_tc_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_gc_root_temp_push(MVMThreadContext *tc, MVMCollectable **obj_ref);
void MVM_gc_root_temp_pop(MVMThreadContext *tc);
void MVM_gc_root_temp_pop_n(MVMThreadContext *tc, MVMuint32 n);
void MVM_gc_root_add_temps_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_gc_root_gen2_add(MVMThreadContext *tc, MVMCollectable *c);
void MVM_gc_root_add_gen2s_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_gc_root_gen2_cleanup(MVMThreadContext *tc);
void MVM_gc_root_add_frame_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *start_frame);

/* Macros related to rooting objects into the temporaries list, and
 * unrooting them afterwards. */
#define MVMROOT(tc, obj_ref, block) do {\
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref)); \
    block \
    MVM_gc_root_temp_pop(tc); \
 } while(0)
