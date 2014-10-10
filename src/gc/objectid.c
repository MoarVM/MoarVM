#include "moar.h"

/* Gets a stable identifier for an object, which will not change even if the
 * GC moves the object. */
MVMuint64 MVM_gc_object_id(MVMThreadContext *tc, MVMObject *obj) {
    MVMuint64 id;

    /* If it's already in the old generation, just use memory address, as
     * gen2 objects never move. */
    if (obj->header.flags & MVM_CF_SECOND_GEN) {
        id = (MVMuint64)obj;
    }

    /* Otherwise, see if we already have a persistent object ID. */
    else {
        MVMObjectId *entry;
        uv_mutex_lock(&tc->instance->mutex_object_ids);
        if (obj->header.flags & MVM_CF_HAS_OBJECT_ID) {
            /* Has one, so just look up by address in the hash ID hash. */
            HASH_FIND(hash_handle, tc->instance->object_ids, (void *)&obj,
                sizeof(MVMObject *), entry);
        }
        else {
            /* Hasn't got one; allocate it a place in gen2 and make an entry
             * in the persistent object ID hash. */
            entry            = MVM_calloc(1, sizeof(MVMObjectId));
            entry->current   = obj;
            entry->gen2_addr = MVM_gc_gen2_allocate_zeroed(tc->gen2, obj->header.size);
            HASH_ADD_KEYPTR(hash_handle, tc->instance->object_ids, &(entry->current),
                sizeof(MVMObject *), entry);
            obj->header.flags |= MVM_CF_HAS_OBJECT_ID;
        }
        id = (MVMuint64)entry->gen2_addr;
        uv_mutex_unlock(&tc->instance->mutex_object_ids);
    }

    return id;
}

/* If an object with an entry here lives long enough to be promoted to gen2,
 * this removes the hash entry for it and returns the pre-allocated gen2
 * address. */
void * MVM_gc_object_id_use_allocation(MVMThreadContext *tc, MVMCollectable *item) {
    MVMObjectId *entry;
    void        *addr;
    uv_mutex_lock(&tc->instance->mutex_object_ids);
    HASH_FIND(hash_handle, tc->instance->object_ids, (void *)&item, sizeof(MVMObject *), entry);
    addr = entry->gen2_addr;
    HASH_DELETE(hash_handle, tc->instance->object_ids, entry);
    MVM_free(entry);
    item->flags ^= MVM_CF_HAS_OBJECT_ID;
    uv_mutex_unlock(&tc->instance->mutex_object_ids);
    return addr;
}

/* Clears hash entry for a persistent object ID when an object dies in the
 * nursery. */
void MVM_gc_object_id_clear(MVMThreadContext *tc, MVMCollectable *item) {
    MVMObjectId *entry;
    uv_mutex_lock(&tc->instance->mutex_object_ids);
    HASH_FIND(hash_handle, tc->instance->object_ids, (void *)&item, sizeof(MVMObject *), entry);
    HASH_DELETE(hash_handle, tc->instance->object_ids, entry);
    MVM_free(entry);
    uv_mutex_unlock(&tc->instance->mutex_object_ids);
}
