struct MVMHeapDumpIndexSnapshotEntry {
    MVMuint64 collectables_size;
    MVMuint64 full_refs_size;
    MVMuint64 refs_middlepoint;
    MVMuint64 incremental_data;
};

/* Used in version 2 of the heap snapshot format */
struct MVMHeapDumpIndex {
    MVMuint64 stringheap_size;
    MVMuint64 types_size;
    MVMuint64 staticframes_size;

    MVMuint64 snapshot_size_entries;
    MVMHeapDumpIndexSnapshotEntry *snapshot_sizes;

    MVMuint64 snapshot_sizes_alloced;
};

/* Used in version 3 of the heap snapshot format.
 * There is one top-level TOC for the whole file,
 * but every snapshot in the file has a TOC of its
 * own that's got an entry in the top-level TOC.
 */
struct MVMHeapDumpTableOfContents {
    MVMuint32 toc_entry_alloc;
    MVMuint32 toc_entry_used;

    char **toc_words;
    MVMuint64 *toc_positions;
};

struct MVMHeapSnapshotStats {
    MVMuint64 type_stats_alloc;

    MVMuint32 *type_counts;
    MVMuint64 *type_size_sum;

    MVMuint64 sf_stats_alloc;

    MVMuint32 *sf_counts;
    MVMuint64 *sf_size_sum;
};

/* A collection of heap snapshots, with common type and static frame names.
 * Note that we take care to never refer to heap objects themselves in here,
 * including for types and frames, since to do so would extend their lifetime
 * for the whole program, which would render the results pretty bogus. */
struct MVMHeapSnapshotCollection {
    /* Snapshot we are currently taking and its index */
    MVMHeapSnapshot *snapshot;
    MVMuint64 snapshot_idx;

    /* Known types/REPRs. Just a list for now, but we might like to look at a
     * hash or trie if this ends up making taking a snapshot wicked slow. */
    MVMHeapSnapshotType *types;
    MVMuint64 num_types;
    MVMuint64 alloc_types;

    /* Known static frames. Same applies to searching this as to the above. */
    MVMHeapSnapshotStaticFrame *static_frames;
    MVMuint64 num_static_frames;
    MVMuint64 alloc_static_frames;

    /* Strings, referenced by index from various places. Also a "should we
     * free it" flag for each one. */
    char **strings;
    MVMuint64 num_strings;
    MVMuint64 alloc_strings;
    char *strings_free;
    MVMuint64 num_strings_free;
    MVMuint64 alloc_strings_free;

    MVMuint64 types_written;
    MVMuint64 static_frames_written;
    MVMuint64 strings_written;

    /* For heap snapshot format 2, an index */
    MVMHeapDumpIndex *index;

    /* For heap snapshot format 3, table of contents */
    MVMHeapDumpTableOfContents *toplevel_toc;
    MVMHeapDumpTableOfContents *second_level_toc;

    /* When the heap snapshot recording was started */
    MVMuint64 start_time;

    /* Counts for the current recording to make an overview */
    MVMuint64 total_heap_size;
    MVMuint64 total_objects;
    MVMuint64 total_typeobjects;
    MVMuint64 total_stables;
    MVMuint64 total_frames;

    /* The file handle we are outputting to */
    FILE *fh;
};

/* An individual heap snapshot. */
struct MVMHeapSnapshot {
    /* Array of data about collectables on the heap. */
    MVMHeapSnapshotCollectable *collectables;
    MVMuint64 num_collectables;
    MVMuint64 alloc_collectables;

    /* References.  */
    MVMHeapSnapshotReference *references;
    MVMuint64 num_references;
    MVMuint64 alloc_references;

    MVMHeapSnapshotStats *stats;

    /* When the snapshot was recorded */
    MVMuint64 record_time;
};

/* An object/type object/STable type in the snapshot. */
struct MVMHeapSnapshotType {
    /* String heap index of the REPR name. */
    MVMuint32 repr_name;

    /* String heap index of the type's debug name. */
    MVMuint32 type_name;
};

/* A static frame in the snapshot. */
struct MVMHeapSnapshotStaticFrame {
    /* The static frame name; index into the snapshot collection string heap. */
    MVMuint32 name;

    /* The static frame compilation unit ID, for added uniqueness checking.
     * Also an index into the string heap. */
    MVMuint32 cuid;

    /* The line number where it's declared. */
    MVMuint32 line;

    /* And the filename; also an index into snapshot collection string heap. */
    MVMuint32 file;
};

/* Kinds of collectable, plus a few "virtual" kinds to cover the various places
 * we find roots. MVM_SNAPSHOT_COL_KIND_ROOT is the ultimate root of the heap
 * snapshot and everything hangs off it. */
#define MVM_SNAPSHOT_COL_KIND_OBJECT            1
#define MVM_SNAPSHOT_COL_KIND_TYPE_OBJECT       2
#define MVM_SNAPSHOT_COL_KIND_STABLE            3
#define MVM_SNAPSHOT_COL_KIND_FRAME             4
#define MVM_SNAPSHOT_COL_KIND_PERM_ROOTS        5
#define MVM_SNAPSHOT_COL_KIND_INSTANCE_ROOTS    6
#define MVM_SNAPSHOT_COL_KIND_CSTACK_ROOTS      7
#define MVM_SNAPSHOT_COL_KIND_THREAD_ROOTS      8
#define MVM_SNAPSHOT_COL_KIND_ROOT              9
#define MVM_SNAPSHOT_COL_KIND_INTERGEN_ROOTS    10
#define MVM_SNAPSHOT_COL_KIND_CALLSTACK_ROOTS   11

/* Data about an individual collectable in the heap snapshot. Ordered to avoid
 * holes. */
struct MVMHeapSnapshotCollectable {
    /* What kind of collectable is it? */
    MVMuint16 kind;

    /* Self-size (from the collectable header). */
    MVMuint16 collectable_size;

    /* Index into the snapshot collection type name or frame info array,
     * depending on kind. */
    MVMuint32 type_or_frame_index;

    /* The number of other collectables this one references. */
    MVMuint32 num_refs;

    /* Index into the references info list. */
    MVMuint64 refs_start;

    /* Unmanaged size (memory held but not under the GC's contorl). */
    MVMuint64 unmanaged_size;
};

/* Reference identifier kinds. */
#define MVM_SNAPSHOT_REF_KIND_UNKNOWN   0
#define MVM_SNAPSHOT_REF_KIND_INDEX     1
#define MVM_SNAPSHOT_REF_KIND_STRING    2

/* Number of bits needed for ref kind. */
#define MVM_SNAPSHOT_REF_KIND_BITS      2

/* A reference from one collectable to another. */
struct MVMHeapSnapshotReference {
    /* The lower MVM_SNAPSHOT_REF_KIND_BITS bits indicate the type of reference.
     * After shifting those away, we either have a numeric index (e.g. for
     * array indexes) or an index into the string heap (for lexicals in frames
     * and attributes in objects). If kind is MVM_SNAPSHOT_REF_KIND_UNKNOWN the
     * rest of the bits will be zero; we know nothing of the relationship. */
    MVMuint64 description;

    /* The index of the collectable referenced. */
    MVMuint64 collectable_index;
};

/* Current state object whlie taking a heap snapshot. */
struct MVMHeapSnapshotState {
    /* The heap snapshot collection and current working snapshot. */
    MVMHeapSnapshotCollection *col;
    MVMHeapSnapshot *hs;

    /* Our current collectable worklist. */
    MVMHeapSnapshotWorkItem *workitems;
    MVMuint64 num_workitems;
    MVMuint64 alloc_workitems;

    /* The collectable we're currently adding references for. */
    MVMuint64 ref_from;

    /* The seen hash of collectables (including frames). */
    MVMHeapSnapshotSeen *seen;

    /* We sometimes use GC mark functions to find references. Keep a worklist
     * around for those times (much cheaper than allocating it whenever we
     * need it). */
    MVMGCWorklist *gcwl;

    /* Many reprs have only one type (BOOTCode) or two
     * types (P6int, BOOTInt), so caching string index
     * per repr id is worth a lot. */
    MVMuint64 repr_str_idx_cache[MVM_REPR_MAX_COUNT];
    MVMuint64 type_str_idx_cache[MVM_REPR_MAX_COUNT];
    MVMuint64 anon_repr_type_str_idx_cache[MVM_REPR_MAX_COUNT];

    MVMuint32 type_of_type_idx_cache[8];
    MVMuint32 repr_of_type_idx_cache[8];
    MVMuint32 type_idx_cache[8];

    MVMuint8 type_idx_rotating_insert_slot;
};

/* Work item used while taking a heap snapshot. */
struct MVMHeapSnapshotWorkItem {
    /* The kind of collectable. */
    MVMuint16 kind;

    /* Index in the collectables (assigned upon adding to the worklist). */
    MVMuint64 col_idx;

    /* Target collectable, if any. */
    void *target;
};

/* Heap seen hash entry used while taking a heap snapshot. */
struct MVMHeapSnapshotSeen {
    /* The seen address. */
    void *address;

    /* The collectables index it has. */
    MVMuint64 idx;

    /* Hash handle. */
    UT_hash_handle hash_handle;
};

MVMint32 MVM_profile_heap_profiling(MVMThreadContext *tc);
void MVM_profile_heap_start(MVMThreadContext *tc, MVMObject *config);
void MVM_profile_heap_take_snapshot(MVMThreadContext *tc);
MVMObject * MVM_profile_heap_end(MVMThreadContext *tc);

/* API for things that want to contribute more detailed data to the heap
 * profile. */
MVM_PUBLIC void MVM_profile_heap_add_collectable_rel_const_cstr(MVMThreadContext *tc,
    MVMHeapSnapshotState *ss, MVMCollectable *collectable, char *desc);
MVM_PUBLIC void MVM_profile_heap_add_collectable_rel_const_cstr_cached(MVMThreadContext *tc,
    MVMHeapSnapshotState *ss, MVMCollectable *collectable, char *desc, MVMuint64 *cache);
MVM_PUBLIC void MVM_profile_heap_add_collectable_rel_vm_str(MVMThreadContext *tc,
    MVMHeapSnapshotState *ss, MVMCollectable *collectable, MVMString *desc);
MVM_PUBLIC void MVM_profile_heap_add_collectable_rel_idx(MVMThreadContext *tc,
    MVMHeapSnapshotState *ss, MVMCollectable *collectable, MVMuint64 idx);
