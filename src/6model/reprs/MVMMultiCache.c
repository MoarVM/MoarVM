#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMMultiCache);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation MultiCache");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMMultiCacheBody *mc = (MVMMultiCacheBody *)data;
    MVMint64 i, j;

    MVM_gc_worklist_add(tc, worklist, &mc->zero_arity);

    for (i = 0; i < MVM_MULTICACHE_MAX_ARITY; i++)
        for (j = 0; j < mc->arity_caches[i].num_entries; j++)
            MVM_gc_worklist_add(tc, worklist, &mc->arity_caches[i].results[j]);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMMultiCache *mc = (MVMMultiCache *)obj;
    MVMint64 i;
    for (i = 0; i < MVM_MULTICACHE_MAX_ARITY; i++) {
        MVM_checked_free_null(mc->body.arity_caches[i].type_ids);
        MVM_checked_free_null(mc->body.arity_caches[i].named_ok);
        MVM_checked_free_null(mc->body.arity_caches[i].results);
    }
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
const MVMREPROps * MVMMultiCache_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "MVMMultiCache", /* name */
    MVM_REPR_ID_MVMMultiCache,
    0, /* refs_frames */
};

MVMObject * MVM_multi_cache_add(MVMThreadContext *tc, MVMObject *cache_obj, MVMObject *capture, MVMObject *result) {
    MVMMultiCacheBody *cache;
    MVMCallsite       *cs;
    MVMArgProcContext *apc;
    MVMuint16          num_args, i, entries, ins_type;
    MVMuint8           has_nameds;
    MVMuint64          arg_tup[MVM_MULTICACHE_MAX_ARITY];

    /* Allocate a cache if needed. */
    if (MVM_is_null(tc, cache_obj) || !IS_CONCRETE(cache_obj) || REPR(cache_obj)->ID != MVM_REPR_ID_MVMMultiCache) {
        MVMROOT(tc, capture, {
        MVMROOT(tc, result, {
            cache_obj = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTMultiCache);
        });
        });
    }
    cache = &((MVMMultiCache *)cache_obj)->body;

    /* Ensure we got a capture in to cache on; bail if unflattened (should
     * never happen). */
    if (REPR(capture)->ID == MVM_REPR_ID_MVMCallCapture) {
        cs         = ((MVMCallCapture *)capture)->body.effective_callsite;
        apc        = ((MVMCallCapture *)capture)->body.apc;
        num_args   = apc->num_pos;
        has_nameds = apc->arg_count != apc->num_pos;
        if (cs->has_flattening)
            return cache_obj;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Multi cache addition requires an MVMCallCapture");
    }

    /* If we're in a multi-threaded context, obtain the cache additions
     * lock, and then do another lookup to ensure nobody beat us to
     * making this entry. */
    if (MVM_instance_have_user_threads(tc)) {
        uv_mutex_lock(&(tc->instance->mutex_multi_cache_add));
        if (MVM_multi_cache_find(tc, cache_obj, capture))
            goto DONE;
    }

    /* If it's zero arity, just stick it in that slot. */
    if (num_args == 0) {
        /* Can only be added if there are no named args */
        if (!has_nameds)
            MVM_ASSIGN_REF(tc, &(cache_obj->header), cache->zero_arity, result);
        goto DONE;
    }

    /* If there's more args than the maximum, we can't cache it. */
    if (num_args > MVM_MULTICACHE_MAX_ARITY)
        goto DONE;

    /* If the cache is saturated, don't do anything (we could instead do a random
     * replacement). */
    entries = cache->arity_caches[num_args - 1].num_entries;
    if (entries == MVM_MULTICACHE_MAX_ENTRIES)
        goto DONE;

    /* Create arg tuple. */
    for (i = 0; i < num_args; i++) {
        MVMuint8 arg_type = cs->arg_flags[i] & MVM_CALLSITE_ARG_MASK;
        if (arg_type == MVM_CALLSITE_ARG_OBJ) {
            MVMObject *arg = MVM_args_get_pos_obj(tc, apc, i, 1).arg.o;
            if (arg) {
                MVMContainerSpec const *contspec = STABLE(arg)->container_spec;
                if (contspec && IS_CONCRETE(arg)) {
                    if (contspec->fetch_never_invokes) {
                        if (REPR(arg)->ID != MVM_REPR_ID_NativeRef) {
                            MVMRegister r;
                            contspec->fetch(tc, arg, &r);
                            arg = r.o;
                        }
                    }
                    else {
                        goto DONE;
                    }
                }
                arg_tup[i] = STABLE(arg)->type_cache_id | (IS_CONCRETE(arg) ? 1 : 0);
            }
            else {
                goto DONE;
            }
        }
        else {
            arg_tup[i] = (arg_type << 1) | 1;
        }
    }

    /* If there's no entries yet, need to do some allocation. */
    if (entries == 0) {
        cache->arity_caches[num_args - 1].type_ids = MVM_malloc(num_args * sizeof(MVMuint64) * MVM_MULTICACHE_MAX_ENTRIES);
        cache->arity_caches[num_args - 1].named_ok = MVM_malloc(sizeof(MVMuint8) * MVM_MULTICACHE_MAX_ENTRIES);
        cache->arity_caches[num_args - 1].results  = MVM_malloc(sizeof(MVMObject *) * MVM_MULTICACHE_MAX_ENTRIES);
    }

    /* Add entry. */
    ins_type = entries * num_args;
    for (i = 0; i < num_args; i++)
        cache->arity_caches[num_args - 1].type_ids[ins_type + i] = arg_tup[i];
    MVM_ASSIGN_REF(tc, &(cache_obj->header), cache->arity_caches[num_args - 1].results[entries], result);
    cache->arity_caches[num_args - 1].named_ok[entries] = has_nameds;

    /* Other threads can read concurrently, so do a memory barrier before we
     * bump the entry count to ensure all the above writes are done. */
    MVM_barrier();
    cache->arity_caches[num_args - 1].num_entries = entries + 1;

    /* Release lock if needed. */
  DONE:
    if (MVM_instance_have_user_threads(tc))
        uv_mutex_unlock(&(tc->instance->mutex_multi_cache_add));

    /* Hand back the created/updated cache. */
    return cache_obj;
}

/* Does a lookup in a multi-dispatch cache. */
MVMObject * MVM_multi_cache_find(MVMThreadContext *tc, MVMObject *cache_obj, MVMObject *capture) {
    MVMMultiCacheBody *cache;
    MVMCallsite       *cs;
    MVMArgProcContext *apc;
    MVMuint16          num_args, i, j, entries, t_pos;
    MVMuint8           has_nameds;
    MVMuint64          arg_tup[MVM_MULTICACHE_MAX_ARITY];

    /* If no cache, no result. */
    if (MVM_is_null(tc, cache_obj) || !IS_CONCRETE(cache_obj) || REPR(cache_obj)->ID != MVM_REPR_ID_MVMMultiCache)
        return NULL;
    cache = &((MVMMultiCache *)cache_obj)->body;

    /* Ensure we got a capture in to look up; bail if unflattened (should
     * never happen). */
    if (REPR(capture)->ID == MVM_REPR_ID_MVMCallCapture) {
        cs         = ((MVMCallCapture *)capture)->body.effective_callsite;
        apc        = ((MVMCallCapture *)capture)->body.apc;
        num_args   = apc->num_pos;
        has_nameds = apc->arg_count != apc->num_pos;
        if (cs->has_flattening)
            return NULL;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Multi cache lookup requires an MVMCallCapture");
    }

    /* If it's zero-arity, return result right off. */
    if (num_args == 0)
        return has_nameds ? NULL : cache->zero_arity;

    /* If there's more args than the maximum, won't be in the cache. */
    if (num_args > MVM_MULTICACHE_MAX_ARITY)
        return NULL;

    /* Create arg tuple. */
    for (i = 0; i < num_args; i++) {
        MVMuint8 arg_type = cs->arg_flags[i] & MVM_CALLSITE_ARG_MASK;
        if (arg_type == MVM_CALLSITE_ARG_OBJ) {
            MVMObject *arg = MVM_args_get_pos_obj(tc, apc, i, 1).arg.o;
            if (arg) {
                MVMContainerSpec const *contspec = STABLE(arg)->container_spec;
                if (contspec && IS_CONCRETE(arg)) {
                    if (contspec->fetch_never_invokes) {
                        if (REPR(arg)->ID != MVM_REPR_ID_NativeRef) {
                            MVMRegister r;
                            contspec->fetch(tc, arg, &r);
                            arg = r.o;
                        }
                    }
                    else {
                        return NULL;
                    }
                }
                arg_tup[i] = STABLE(arg)->type_cache_id | (IS_CONCRETE(arg) ? 1 : 0);
            }
            else {
                return NULL;
            }
        }
        else {
            arg_tup[i] = (arg_type << 1) | 1;
        }
    }

    /* Look through entries. */
    entries = cache->arity_caches[num_args - 1].num_entries;
    t_pos = 0;
    for (i = 0; i < entries; i++) {
        MVMint64 match = 1;
        for (j = 0; j < num_args; j++) {
            if (cache->arity_caches[num_args - 1].type_ids[t_pos + j] != arg_tup[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            MVMuint8 match_nameds = cache->arity_caches[num_args - 1].named_ok[i];
            if (has_nameds == match_nameds)
                return cache->arity_caches[num_args - 1].results[i];
        }
        t_pos += num_args;
    }

    return NULL;
}

/* Does a lookup in the multi-dispatch cache using a callsite and args. Some
 * code dupe with above; may be nice to factor it out some day. */
MVMObject * MVM_multi_cache_find_callsite_args(MVMThreadContext *tc, MVMObject *cache_obj,
    MVMCallsite *cs, MVMRegister *args) {
    MVMMultiCacheBody *cache;
    MVMuint16          num_args, i, j, entries, t_pos;
    MVMuint8           has_nameds;
    MVMuint64          arg_tup[MVM_MULTICACHE_MAX_ARITY];

    /* If no cache, no result. */
    if (MVM_is_null(tc, cache_obj) || !IS_CONCRETE(cache_obj) || REPR(cache_obj)->ID != MVM_REPR_ID_MVMMultiCache)
        return NULL;
    cache = &((MVMMultiCache *)cache_obj)->body;

    /* Ensure we got a capture in to look up; bail if unflattened. */
    if (cs->has_flattening)
        return NULL;
    num_args   = cs->num_pos;
    has_nameds = cs->arg_count != cs->num_pos;

    /* If it's zero-arity, return result right off. */
    if (num_args == 0)
        return has_nameds ? NULL : cache->zero_arity;

    /* If there's more args than the maximum, won't be in the cache. */
    if (num_args > MVM_MULTICACHE_MAX_ARITY)
        return NULL;

    /* Create arg tuple. */
    for (i = 0; i < num_args; i++) {
        MVMuint8 arg_type = cs->arg_flags[i] & MVM_CALLSITE_ARG_MASK;
        if (arg_type == MVM_CALLSITE_ARG_OBJ) {
            MVMObject *arg = args[i].o;
            if (arg) {
                MVMContainerSpec const *contspec = STABLE(arg)->container_spec;
                if (contspec && IS_CONCRETE(arg)) {
                    if (contspec->fetch_never_invokes) {
                        if (REPR(arg)->ID != MVM_REPR_ID_NativeRef) {
                            MVMRegister r;
                            contspec->fetch(tc, arg, &r);
                            arg = r.o;
                        }
                    }
                    else {
                        return NULL;
                    }
                }
                arg_tup[i] = STABLE(arg)->type_cache_id | (IS_CONCRETE(arg) ? 1 : 0);
            }
            else {
                return NULL;
            }
        }
        else {
            arg_tup[i] = (arg_type << 1) | 1;
        }
    }

    /* Look through entries. */
    entries = cache->arity_caches[num_args - 1].num_entries;
    t_pos = 0;
    for (i = 0; i < entries; i++) {
        MVMint64 match = 1;
        for (j = 0; j < num_args; j++) {
            if (cache->arity_caches[num_args - 1].type_ids[t_pos + j] != arg_tup[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            MVMuint8 match_nameds = cache->arity_caches[num_args - 1].named_ok[i];
            if (has_nameds == match_nameds)
                return cache->arity_caches[num_args - 1].results[i];
        }
        t_pos += num_args;
    }

    return NULL;
}

/* Do a multi cache lookup based upon spesh arg facts. */
MVMObject * MVM_multi_cache_find_spesh(MVMThreadContext *tc, MVMObject *cache_obj, MVMSpeshCallInfo *arg_info) {
    MVMMultiCacheBody *cache;
    MVMuint16          num_args, i, j, entries, t_pos;
    MVMuint8           has_nameds;
    MVMuint64          arg_tup[MVM_MULTICACHE_MAX_ARITY];

    /* If no cache, no result. */
    if (MVM_is_null(tc, cache_obj) || !IS_CONCRETE(cache_obj) || REPR(cache_obj)->ID != MVM_REPR_ID_MVMMultiCache)
        return NULL;
    cache = &((MVMMultiCache *)cache_obj)->body;

    /* Ensure we got a capture in to look up; bail if unflattened. */
    if (arg_info->cs->has_flattening)
        return NULL;
    num_args   = arg_info->cs->num_pos;
    has_nameds = arg_info->cs->arg_count != arg_info->cs->num_pos;

    /* If it's zero-arity, return result right off. */
    if (num_args == 0)
        return has_nameds ? NULL : cache->zero_arity;

    /* If there's more args than the maximum, won't be in the cache. Also
     * check against maximum size of spesh call site. */
    if (num_args > MVM_MULTICACHE_MAX_ARITY || num_args > MAX_ARGS_FOR_OPT)
        return NULL;

    /* Create arg tuple. */
    for (i = 0; i < num_args; i++) {
        MVMuint8 arg_type = arg_info->cs->arg_flags[i] & MVM_CALLSITE_ARG_MASK;
        if (arg_type == MVM_CALLSITE_ARG_OBJ) {
            MVMSpeshFacts *facts = arg_info->arg_facts[i];
            if (facts) {
                /* Must know type. */
                if (!(facts->flags & MVM_SPESH_FACT_KNOWN_TYPE))
                    return NULL;

                /* Must know if it's concrete or not. */
                if (!(facts->flags & (MVM_SPESH_FACT_CONCRETE | MVM_SPESH_FACT_TYPEOBJ)))
                    return NULL;

                /* If it's a container, must know what's inside it. Otherwise,
                 * we're already good on type info. */
                if ((facts->flags & MVM_SPESH_FACT_CONCRETE)&& STABLE(facts->type)->container_spec) {
                    /* Again, need to know type and concreteness. */
                    if (!(facts->flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE))
                        return NULL;
                    if (!(facts->flags & (MVM_SPESH_FACT_DECONT_CONCRETE | MVM_SPESH_FACT_DECONT_TYPEOBJ)))
                        return NULL;
                    arg_tup[i] = STABLE(facts->decont_type)->type_cache_id |
                        ((facts->flags & MVM_SPESH_FACT_DECONT_CONCRETE) ? 1 : 0);
                }
                else {
                    arg_tup[i] = STABLE(facts->type)->type_cache_id |
                        ((facts->flags & MVM_SPESH_FACT_CONCRETE) ? 1 : 0);
                }
            }
            else {
                return NULL;
            }
        }
        else {
            arg_tup[i] = (arg_type << 1) | 1;
        }
    }

    /* Look through entries. */
    entries = cache->arity_caches[num_args - 1].num_entries;
    t_pos = 0;
    for (i = 0; i < entries; i++) {
        MVMint64 match = 1;
        for (j = 0; j < num_args; j++) {
            if (cache->arity_caches[num_args - 1].type_ids[t_pos + j] != arg_tup[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            MVMuint8 match_nameds = cache->arity_caches[num_args - 1].named_ok[i];
            if (has_nameds == match_nameds)
                return cache->arity_caches[num_args - 1].results[i];
        }
        t_pos += num_args;
    }

    return NULL;
}
