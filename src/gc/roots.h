/* Set this flag to debug temporary root pushes/pops. */
#define MVM_TEMP_ROOT_DEBUG 0

/* The number of temp roots we start out with per thread (and so can rely on
 * always having). */
#define MVM_TEMP_ROOT_BASE_ALLOC 16

/* Temp root push slow-path case. */
MVM_PUBLIC void MVM_gc_root_temp_push_slow(MVMThreadContext *tc, MVMCollectable **obj_ref);

/* Fast-path case of pushing a root onto the per-thread temporary roots. */
MVM_STATIC_INLINE void MVM_gc_root_temp_push(MVMThreadContext *tc, MVMCollectable **obj_ref) {
    /* If debugging, ensure the root is not null. */
#ifdef MVM_TEMP_ROOT_DEBUG
    if (obj_ref == NULL)
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to add null object address as a temporary root");
#endif

    /* If less than the number of always-allocated roots, just add. */
    if (tc->num_temproots < MVM_TEMP_ROOT_BASE_ALLOC) {
        tc->temproots[tc->num_temproots] = obj_ref;
        tc->num_temproots++;
    }

    /* Otherwise call the slow path. */
    else {
        MVM_gc_root_temp_push_slow(tc, obj_ref);
    }
}

/* Pop top root from the per-thread temporary roots stack. */
MVM_STATIC_INLINE void MVM_gc_root_temp_pop(MVMThreadContext *tc) {
#if MVM_TEMP_ROOT_DEBUG
    if (tc->num_temproots <= 0)
        MVM_panic(1, "Illegal attempt to pop empty temporary root stack");
#endif
    tc->num_temproots--;
}

/* Pop top n roots from the per-thread temporary roots stack. */
MVM_STATIC_INLINE void MVM_gc_root_temp_pop_n(MVMThreadContext *tc, MVMuint32 n) {
#if MVM_TEMP_ROOT_DEBUG
    if (tc->num_temproots < n)
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to pop insufficiently large temporary root stack");
#endif
    tc->num_temproots -= n;
}

/* Other functions related to roots. */
MVM_PUBLIC void MVM_gc_root_add_permanent(MVMThreadContext *tc, MVMCollectable **obj_ref);
MVM_PUBLIC void MVM_gc_root_add_permanent_desc(MVMThreadContext *tc, MVMCollectable **obj_ref, char *description);
void MVM_gc_root_add_permanents_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot);
void MVM_gc_root_add_instance_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot);
void MVM_gc_root_add_tc_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot);
MVMuint32 MVM_gc_root_temp_mark(MVMThreadContext *tc);
void MVM_gc_root_temp_mark_reset(MVMThreadContext *tc, MVMuint32 mark);
void MVM_gc_root_temp_pop_all(MVMThreadContext *tc);
void MVM_gc_root_add_temps_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot);
void MVM_gc_root_gen2_add(MVMThreadContext *tc, MVMCollectable *c);
void MVM_gc_root_add_gen2s_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_gc_root_add_gen2s_to_snapshot(MVMThreadContext *tc, MVMHeapSnapshotState *snapshot);
void MVM_gc_root_gen2_cleanup(MVMThreadContext *tc);
void MVM_gc_root_add_frame_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *start_frame);
void MVM_gc_root_add_frame_registers_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame);

/* Macros related to rooting objects into the temporaries list, and
 * unrooting them afterwards. */
#define MVMROOT(tc, obj_ref, block) do {\
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref)); \
    block \
    MVM_gc_root_temp_pop(tc); \
 } while (0)
#define MVMROOT2(tc, obj_ref1, obj_ref2, block) do {\
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref1)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref2)); \
    block \
    MVM_gc_root_temp_pop_n(tc, 2); \
 } while (0)
#define MVMROOT3(tc, obj_ref1, obj_ref2, obj_ref3, block) do {\
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref1)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref2)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref3)); \
    block \
    MVM_gc_root_temp_pop_n(tc, 3); \
 } while (0)
#define MVMROOT4(tc, obj_ref1, obj_ref2, obj_ref3, obj_ref4, block) do {\
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref1)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref2)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref3)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref4)); \
    block \
    MVM_gc_root_temp_pop_n(tc, 4); \
 } while (0)
#define MVMROOT5(tc, obj_ref1, obj_ref2, obj_ref3, obj_ref4, obj_ref5, block) do {\
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref1)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref2)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref3)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref4)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref5)); \
    block \
    MVM_gc_root_temp_pop_n(tc, 5); \
 } while (0)
#define MVMROOT6(tc, obj_ref1, obj_ref2, obj_ref3, obj_ref4, obj_ref5, obj_ref6, block) do {\
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref1)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref2)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref3)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref4)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref5)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(obj_ref6)); \
    block \
    MVM_gc_root_temp_pop_n(tc, 6); \
 } while (0)
