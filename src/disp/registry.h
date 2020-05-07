/* The definition of a dispatcher, stored in the registry. */
struct MVMDispDefinition {
    /* The ID of the dispatcher, used to look it up. */
    MVMString *id;

    /* The dispatch callback, for when no entry is found in the cache. */
    MVMObject *dispatch;

    /* The resume callback, for resumable dispatchers, when no resume entry
     * is found in the cache. */
    MVMObject *resume;
};

/* The dispatcher registry. The dispatcher definitions live in their own
 * pieces of memory, so we can reference them and know they won't move. We
 * then store an array of pointers to those. The position in the table is
 * determined by hashing the ID. If the table needs to grow, we safepoint
 * reallocate it. Thus we only need a lock for the purpose of guarding new
 * dispatcher registrations, and lookups are lock-free. We don't need to
 * worry over hash randomization and so forth here; the dispatchers are
 * registered by a program's runtime support, not on data from the outside
 * world. */
struct MVMDispRegistry {
    /* The current table, held at a level of indirection for safe updates. */
    MVMDispRegistryTable *table;

    /* Lock for serializing updates to the set of dispatchers. */
    uv_mutex_t mutex_update;
};
struct MVMDispRegistryTable {
    /* The table of dispatcher definitions, FSA allocated and safepoint
     * reallocated (thus why not using MVM_VECTOR). */
    MVMDispDefinition **dispatchers;

    /* The number of allocated dispatcher entries and the number of
     * registered dispatchers. */
    MVMuint32 alloc_dispatchers;
    MVMuint32 num_dispatchers;
};

void MVM_disp_registry_init(MVMThreadContext *tc);
void MVM_disp_registry_register(MVMThreadContext *tc, MVMString *id, MVMObject *dispatch,
        MVMObject *resume);
MVMDispDefinition * MVM_disp_registry_find(MVMThreadContext *tc, MVMString *id);
void MVM_disp_registry_mark(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_disp_registry_destroy(MVMThreadContext *tc);
