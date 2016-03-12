/* A collection of heap snapshots, with common type and static frame names.
 * Note that we take care to never refer to heap objects themselves in here,
 * including for types and frames, since to do so would extend their lifetime
 * for the whole program, which would render the results pretty bogus. */
struct MVMHeapSnapshotCollection {
    /* List of taken snapshots. */
    MVMHeapSnapshot *snapshots;
    MVMuint32 num_snapshots;
    MVMuint32 alloc_snapshots;

    /* Known types/REPRs. Just a list for now, but we might like to look at a
     * hash or trie if this ends up making taking a snapshot wicked slow. */
    MVMHeapSnapshotType *types;
    MVMuint32 num_types;
    MVMuint32 alloc_types;

    /* Known static frames. Same applies to searching this as to the above. */
    MVMHeapSnapshotStaticFrame *static_frames;
    MVMuint32 num_static_frames;
    MVMuint32 alloc_static_frames;

    /* Strings, referenced by index from various places. */
    char **strings;
    MVMuint32 num_strings;
    MVMuint32 alloc_strings;
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
};

/* An object/type object/STable type in the snapshot. */
struct MVMHeapSnapshotType {
    /* Name of the representation. Do not free; this is a reference to the
     * REPR's name string itself. */
    char *repr_name;

    /* The type's debug name. We assume these are sufficiently unique we don't
     * reference them in the string heap. */
    char *type_name;
};

/* A static frame in the snapshot. */
struct MVMHeapSnapshotStaticFrame {
    /* The static frame name; index into the snapshot collection string heap. */
    MVMuint32 name;

    /* The line number where it's declared. */
    MVMuint32 line_number;

    /* And the filename; also an index into snapshot collection string heap. */
    MVMuint32 file;
};

/* Kinds of collectable. */
#define MVM_SNAPSHOT_COL_KIND_OBJECT        1
#define MVM_SNAPSHOT_COL_KIND_TYPE_OBJECT   2
#define MVM_SNAPSHOT_COL_KIND_STABLE        3
#define MVM_SNAPSHOT_COL_KIND_FRAME         4

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
    MVMuint64 kind;

    /* The index of the collectable referenced. */
    MVMuint64 collectable_index;
};

MVMint32 MVM_profile_heap_profiling(MVMThreadContext *tc);
void MVM_profile_heap_start(MVMThreadContext *tc, MVMObject *config);
void MVM_profile_heap_take_snapshot(MVMThreadContext *tc);
MVMObject * MVM_profile_heap_end(MVMThreadContext *tc);
