#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMMultiCache_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMMultiCache_this_repr, HOW);

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
    size_t i;
    for (i = 0; i < mc->num_results; i++)
        MVM_gc_worklist_add(tc, worklist, &(mc->results[i]));
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMMultiCache *mc = (MVMMultiCache *)obj;
    if (mc->body.node_hash_head)
        MVM_fixed_size_free(tc, tc->instance->fsa, mc->body.cache_memory_size,
            mc->body.node_hash_head);
    if (mc->body.results)
        MVM_fixed_size_free(tc, tc->instance->fsa,
            mc->body.num_results * sizeof(MVMObject *),
            mc->body.results);
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

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMMultiCacheBody *body = (MVMMultiCacheBody *)data;
    return body->num_results * sizeof(MVMObject *) + body->cache_memory_size;
}

/* Initializes the representation. */
const MVMREPROps * MVMMultiCache_initialize(MVMThreadContext *tc) {
    return &MVMMultiCache_this_repr;
}

static const MVMREPROps MVMMultiCache_this_repr = {
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
    unmanaged_size, /* unmanaged_size */
    NULL, /* describe_refs */
};

/* Filters for various parts of action.arg_match. */
#define MVM_MULTICACHE_ARG_IDX_FILTER  (2 * MVM_INTERN_ARITY_LIMIT - 1)
#define MVM_MULTICACHE_ARG_CONC_FILTER (2 * MVM_INTERN_ARITY_LIMIT)
#define MVM_MULTICACHE_ARG_RW_FILTER   (4 * MVM_INTERN_ARITY_LIMIT)
#define MVM_MULTICACHE_TYPE_ID_FILTER  (0xFFFFFFFFFFFFFFFFULL ^ (MVM_TYPE_CACHE_ID_INCR - 1))

/* Debug support dumps the tree after each addition. */
#define MVM_MULTICACHE_DEBUG 0
#if MVM_MULTICACHE_DEBUG
static void dump_cache(MVMThreadContext *tc, MVMMultiCacheBody *cache) {
    MVMint32 num_nodes = cache->cache_memory_size / sizeof(MVMMultiCacheNode);
    MVMint32 i;
    printf("Multi cache at %p (%d nodes, %zd results)\n",
        cache, num_nodes, cache->num_results);
    for (i = 0; i < num_nodes; i++)
        printf(" - %p -> (Y: %d, N: %d)\n",
            cache->node_hash_head[i].action.cs,
            cache->node_hash_head[i].match,
            cache->node_hash_head[i].no_match);
    printf("\n");
}
#endif

/* Big cache profiling. */
#define MVM_MULTICACHE_BIG_PROFILE 0
#if MVM_MULTICACHE_BIG_PROFILE
static MVMint32 is_power_of_2(MVMint32 value) {
    return ((value != 0) && !(value & (value - 1)));
}
#endif

/* Takes a pointer to a callsite and turns it into an index into the multi cache
 * keyed by callsite. We don't do anything too clever here: just shift away the
 * bits of the pointer we know will be zero, and the take the least significant
 * few bits of it. Hopefully the distribution of memory addresses over time will
 * be sufficient. */
MVM_STATIC_INLINE size_t hash_callsite(MVMThreadContext *tc, MVMCallsite *cs) {
    return ((size_t)cs >> 3) & MVM_MULTICACHE_HASH_FILTER;
}

/* Adds an entry to the multi-dispatch cache. */
MVMObject * MVM_multi_cache_add(MVMThreadContext *tc, MVMObject *cache_obj, MVMObject *capture, MVMObject *result) {
    MVMMultiCacheBody *cache = NULL;
    MVMCallsite       *cs    = NULL;
    MVMArgProcContext *apc   = NULL;
    MVMuint64          match_flags[2 * MVM_INTERN_ARITY_LIMIT];
    size_t             match_arg_idx[MVM_INTERN_ARITY_LIMIT];
    MVMuint32          flag, i, num_obj_args, have_head, have_tree,
                       have_callsite, matched_args, unmatched_arg,
                       tweak_node, insert_node;
    size_t             new_size;
    MVMMultiCacheNode *new_head    = NULL;
    MVMObject        **new_results = NULL;

    /* Allocate a cache if needed. */
    if (MVM_is_null(tc, cache_obj) || !IS_CONCRETE(cache_obj) || REPR(cache_obj)->ID != MVM_REPR_ID_MVMMultiCache) {
        MVMROOT2(tc, capture, result, {
            cache_obj = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTMultiCache);
        });
    }
    cache = &((MVMMultiCache *)cache_obj)->body;

    /* Ensure we got a capture in to cache on; bail if not interned. */
    if (REPR(capture)->ID == MVM_REPR_ID_MVMCallCapture) {
        apc        = ((MVMCallCapture *)capture)->body.apc;
        cs         = apc->callsite;
        if (!cs->is_interned)
            return cache_obj;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Multi cache addition requires an MVMCallCapture");
    }

    /* Calculate matcher flags for all the object arguments. */
    num_obj_args = 0;
    for (i = 0, flag = 0; flag < cs->flag_count; i++, flag++) {
        if (cs->arg_flags[flag] & MVM_CALLSITE_ARG_NAMED)
            i++;
        if ((cs->arg_flags[flag] & MVM_CALLSITE_ARG_TYPE_MASK) == MVM_CALLSITE_ARG_OBJ) {
            MVMRegister  arg   = apc->args[i];
            MVMSTable   *st    = STABLE(arg.o);
            MVMuint32    is_rw = 0;
            if (st->container_spec && IS_CONCRETE(arg.o)) {
                MVMContainerSpec const *contspec = st->container_spec;
                if (!contspec->fetch_never_invokes)
                    return cache_obj; /* Impossible to cache. */
                if (REPR(arg.o)->ID != MVM_REPR_ID_NativeRef) {
                    is_rw = contspec->can_store(tc, arg.o);
                    contspec->fetch(tc, arg.o, &arg);
                }
                else {
                    is_rw = 1;
                }
            }
            match_flags[i] = STABLE(arg.o)->type_cache_id |
                (is_rw ? MVM_MULTICACHE_ARG_RW_FILTER : 0) |
                (IS_CONCRETE(arg.o) ? MVM_MULTICACHE_ARG_CONC_FILTER : 0);
            match_arg_idx[num_obj_args] = i;
            num_obj_args++;
        }
    }

    /* Obtain the cache addition lock. */
    uv_mutex_lock(&(tc->instance->mutex_multi_cache_add));

    /* We're now under the insertion lock and know nobody else can tweak the
     * cache. First, see if there's even a current version and search tree. */
    have_head = 0;
    have_tree = 0;
    have_callsite = 0;
    matched_args = 0;
    unmatched_arg = 0;
    tweak_node = hash_callsite(tc, cs);
    if (cache->node_hash_head) {
        MVMMultiCacheNode *tree = cache->node_hash_head;
        MVMint32 cur_node = tweak_node;
        have_head = 1;
        if (tree[cur_node].action.cs)
            have_tree = 1;

        /* Now see if we already have this callsite. */
        do {
            if (tree[cur_node].action.cs == cs) {
                have_callsite = 1;
                cur_node = tree[cur_node].match;
                break;
            }
            tweak_node = cur_node;
            cur_node = tree[cur_node].no_match;
        } while (cur_node > 0);

        /* Chase until we reach an arg we don't match. */
        while (cur_node > 0) {
            MVMuint64    arg_match = tree[cur_node].action.arg_match;
            MVMuint64    arg_idx   = arg_match & MVM_MULTICACHE_ARG_IDX_FILTER;
            MVMuint64    type_id   = arg_match & MVM_MULTICACHE_TYPE_ID_FILTER;
            MVMRegister  arg       = apc->args[arg_idx];
            MVMSTable   *st        = STABLE(arg.o);
            MVMuint64    is_rw     = 0;
            tweak_node = cur_node;
            if (st->container_spec && IS_CONCRETE(arg.o)) {
                MVMContainerSpec const *contspec = st->container_spec;
                if (!contspec->fetch_never_invokes)
                    goto DONE;
                if (REPR(arg.o)->ID != MVM_REPR_ID_NativeRef) {
                    is_rw = contspec->can_store(tc, arg.o);
                    contspec->fetch(tc, arg.o, &arg);
                }
                else {
                    is_rw = 1;
                }
            }
            if (STABLE(arg.o)->type_cache_id == type_id) {
                MVMuint32 need_concrete = (arg_match & MVM_MULTICACHE_ARG_CONC_FILTER) ? 1 : 0;
                if (IS_CONCRETE(arg.o) == need_concrete) {
                    MVMuint32 need_rw = (arg_match & MVM_MULTICACHE_ARG_RW_FILTER) ? 1 : 0;
                    if (need_rw == is_rw) {
                        matched_args++;
                        unmatched_arg = 0;
                        cur_node = tree[cur_node].match;
                        continue;
                    }
                }
            }
            unmatched_arg = 1;
            cur_node = tree[cur_node].no_match;
        }

        /* If we found a candidate, someone else beat us to adding the candidate
           before we obtained the lock or the arguments got changed behind our
           back so that they now match */
        if (cur_node != 0)
            goto DONE;
    }

    /* Now calculate the new size we'll need to allocate. */
    new_size = cache->cache_memory_size;
    if (!have_head)
        new_size += MVM_MULTICACHE_HASH_SIZE * sizeof(MVMMultiCacheNode);
    else if (!have_callsite)
        new_size += sizeof(MVMMultiCacheNode);
    new_size += (num_obj_args - matched_args) * sizeof(MVMMultiCacheNode);

    /* Allocate and copy existing cache. */
    new_head = MVM_fixed_size_alloc(tc, tc->instance->fsa, new_size);
    memcpy(new_head, cache->node_hash_head, cache->cache_memory_size);

    /* If we had no head, set it up. */
    if (!have_head)
        memset(new_head, 0, MVM_MULTICACHE_HASH_SIZE * sizeof(MVMMultiCacheNode));

    /* Calculate storage location of new nodes. */
    insert_node = have_head
        ? cache->cache_memory_size / sizeof(MVMMultiCacheNode)
        : MVM_MULTICACHE_HASH_SIZE;

    /* If we had no callsite, add a node for it. */
    if (!have_callsite) {
        if (!have_tree) {
            /* We'll put it in the tree root. */
            new_head[tweak_node].action.cs = cs;
        }
        else {
            /* We'll insert a new node and chain it from the tweak node. */
            new_head[insert_node].action.cs = cs;
            new_head[insert_node].no_match = 0;
            new_head[tweak_node].no_match = insert_node;
            tweak_node = insert_node;
            insert_node++;
        }
    }

    /* Now insert any needed arg matchers. */
    for (i = matched_args; i < num_obj_args; i++) {
        MVMuint32 arg_idx = match_arg_idx[i];
        new_head[insert_node].action.arg_match = match_flags[arg_idx] | arg_idx;
        new_head[insert_node].no_match = 0;
        if (unmatched_arg) {
            new_head[tweak_node].no_match = insert_node;
            unmatched_arg = 0;
        }
        else {
            new_head[tweak_node].match = insert_node;
        }
        tweak_node = insert_node;
        insert_node++;
    }

    /* Make a copy of the results, or allocate new (first result is NULL
     * always) and insert the new result. Schedule old results for freeing. */
    if (cache->num_results) {
        new_results = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            (cache->num_results + 1) * sizeof(MVMObject *));
        memcpy(new_results, cache->results, cache->num_results * sizeof(MVMObject *));
        MVM_ASSIGN_REF(tc, &(cache_obj->header), new_results[cache->num_results], result);
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
            cache->num_results * sizeof(MVMObject *), cache->results);
        cache->results = new_results;
        cache->num_results++;
    }
    else {
        new_results = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            2 * sizeof(MVMObject *));
        new_results[0] = NULL; /* Sentinel */
        MVM_ASSIGN_REF(tc, &(cache_obj->header), new_results[1], result);
        cache->results = new_results;
        cache->num_results = 2;
    }
    MVM_barrier();

    /* Associate final node with result index. */
    new_head[tweak_node].match = -(cache->num_results - 1);

    /* Update the rest. */
    if (cache->node_hash_head)
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
            cache->cache_memory_size, cache->node_hash_head);
    cache->node_hash_head = new_head;
    cache->cache_memory_size = new_size;

#if MVM_MULTICACHE_DEBUG
    printf("Made new entry for callsite with %d object arguments\n", num_obj_args);
    dump_cache(tc, cache);
#endif
#if MVM_MULTICACHE_BIG_PROFILE
    if (cache->num_results >= 32 && is_power_of_2(cache->num_results)) {
        MVMCode *code = (MVMCode *)MVM_frame_find_invokee(tc, result, NULL);
        char *name = MVM_string_utf8_encode_C_string(tc, code->body.sf->body.name);
        printf("Multi cache for %s reached %d entries\n", name, cache->num_results);
        MVM_free(name);
    }
#endif

    /* Release lock. */
  DONE:
    uv_mutex_unlock(&(tc->instance->mutex_multi_cache_add));

    /* Hand back the created/updated cache. */
    return cache_obj;
}

/* Does a lookup in a multi-dispatch cache using a capture. */
MVMObject * MVM_multi_cache_find(MVMThreadContext *tc, MVMObject *cache_obj, MVMObject *capture) {
    if (REPR(capture)->ID == MVM_REPR_ID_MVMCallCapture) {
        MVMArgProcContext *apc = ((MVMCallCapture *)capture)->body.apc;
        MVMCallsite       *cs  = apc->callsite;
        return MVM_multi_cache_find_callsite_args(tc, cache_obj, cs, apc->args);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Multi cache lookup requires an MVMCallCapture");
    }
}

/* Does a lookup in the multi-dispatch cache using a callsite and args. */
MVMObject * MVM_multi_cache_find_callsite_args(MVMThreadContext *tc, MVMObject *cache_obj,
    MVMCallsite *cs, MVMRegister *args) {
    MVMMultiCacheBody *cache = NULL;
    MVMMultiCacheNode *tree  = NULL;
    MVMint32 cur_node;

    /* Bail if callsite not interned. */
    if (!cs->is_interned)
        return NULL;

    /* If no cache, no result. */
    if (MVM_is_null(tc, cache_obj) || !IS_CONCRETE(cache_obj) || REPR(cache_obj)->ID != MVM_REPR_ID_MVMMultiCache)
        return NULL;
    cache = &((MVMMultiCache *)cache_obj)->body;
    if (!cache->node_hash_head)
        return NULL;

    /* Use hashed callsite to find the node to start with. */
    cur_node = hash_callsite(tc, cs);

    /* Walk tree until we match callsite. */
    tree = cache->node_hash_head;
    do {
        if (tree[cur_node].action.cs == cs) {
            cur_node = tree[cur_node].match;
            break;
        }
        cur_node = tree[cur_node].no_match;
    } while (cur_node > 0);

    /* Now walk until we match argument type/concreteness/rw. */
    while (cur_node > 0) {
        MVMuint64    arg_match = tree[cur_node].action.arg_match;
        MVMuint64    arg_idx   = arg_match & MVM_MULTICACHE_ARG_IDX_FILTER;
        MVMuint64    type_id   = arg_match & MVM_MULTICACHE_TYPE_ID_FILTER;
        MVMRegister  arg       = args[arg_idx];
        MVMSTable   *st        = STABLE(arg.o);
        MVMuint64    is_rw     = 0;
        if (st->container_spec && IS_CONCRETE(arg.o)) {
            MVMContainerSpec const *contspec = st->container_spec;
            if (!contspec->fetch_never_invokes)
                return NULL;
            if (REPR(arg.o)->ID != MVM_REPR_ID_NativeRef) {
                is_rw = contspec->can_store(tc, arg.o);
                contspec->fetch(tc, arg.o, &arg);
            }
            else {
                is_rw = 1;
            }
        }
        if (STABLE(arg.o)->type_cache_id == type_id) {
            MVMuint32 need_concrete = (arg_match & MVM_MULTICACHE_ARG_CONC_FILTER) ? 1 : 0;
            if (IS_CONCRETE(arg.o) == need_concrete) {
                MVMuint32 need_rw = (arg_match & MVM_MULTICACHE_ARG_RW_FILTER) ? 1 : 0;
                if (need_rw == is_rw) {
                    cur_node = tree[cur_node].match;
                    continue;
                }
            }
        }
        cur_node = tree[cur_node].no_match;
    }

    /* Negate result and index into results (the first result is always NULL
     * to save flow control around "no match"). */
    return cache->results[-cur_node];
}

/* Do a multi cache lookup based upon spesh arg facts. */
MVMObject * MVM_multi_cache_find_spesh(MVMThreadContext *tc, MVMObject *cache_obj,
                                       MVMSpeshCallInfo *arg_info,
                                       MVMSpeshStatsType *type_tuple) {
    MVMMultiCacheBody *cache = NULL;
    MVMMultiCacheNode *tree  = NULL;
    MVMint32 cur_node;

    /* Bail if callsite not interned. */
    if (!arg_info->cs->is_interned)
        return NULL;

    /* If no cache, no result. */
    if (MVM_is_null(tc, cache_obj) || !IS_CONCRETE(cache_obj) || REPR(cache_obj)->ID != MVM_REPR_ID_MVMMultiCache)
        return NULL;
    cache = &((MVMMultiCache *)cache_obj)->body;
    if (!cache->node_hash_head)
        return NULL;

    /* Use hashed callsite to find the node to start with. */
    cur_node = hash_callsite(tc, arg_info->cs);

    /* Walk tree until we match callsite. */
    tree = cache->node_hash_head;
    do {
        if (tree[cur_node].action.cs == arg_info->cs) {
            cur_node = tree[cur_node].match;
            break;
        }
        cur_node = tree[cur_node].no_match;
    } while (cur_node > 0);

    /* Now walk until we match argument type/concreteness/rw. */
    while (cur_node > 0) {
        MVMuint64      arg_match = tree[cur_node].action.arg_match;
        MVMuint64      arg_idx   = arg_match & MVM_MULTICACHE_ARG_IDX_FILTER;
        MVMuint64      type_id   = arg_match & MVM_MULTICACHE_TYPE_ID_FILTER;
        MVMSpeshFacts *facts     = arg_idx < MAX_ARGS_FOR_OPT
            ? arg_info->arg_facts[arg_idx]
            : NULL;
        if (type_tuple) {
            MVMuint16 num_pos = arg_info->cs->num_pos;
            MVMuint64 tt_offset = arg_idx >= num_pos
                ? ((arg_idx - 1) - num_pos) / 2 + num_pos
                : arg_idx;
            MVMuint32 is_rw = type_tuple[tt_offset].rw_cont;
            MVMSTable *known_type_st = NULL;
            MVMuint32 is_conc;
            if (type_tuple[tt_offset].decont_type) {
                known_type_st = type_tuple[tt_offset].decont_type->st;
                is_conc = type_tuple[tt_offset].decont_type_concrete;
            }
            else if (type_tuple[tt_offset].type) { /* FIXME: tuples with neither decont_type nor type shouldn't appear */
                known_type_st = type_tuple[tt_offset].type->st;
                is_conc = type_tuple[tt_offset].type_concrete;
            }

            /* Now check if what we have matches what we need. */
            if (known_type_st && known_type_st->type_cache_id == type_id) {
                MVMuint32 need_concrete = (arg_match & MVM_MULTICACHE_ARG_CONC_FILTER) ? 1 : 0;
                if (is_conc == need_concrete) {
                    MVMuint32 need_rw = (arg_match & MVM_MULTICACHE_ARG_RW_FILTER) ? 1 : 0;
                    if (need_rw == is_rw) {
                        cur_node = tree[cur_node].match;
                        continue;
                    }
                }
            }
            cur_node = tree[cur_node].no_match;
        }
        else if (facts) {
            /* Figure out type, concreteness, and rw-ness from facts. */
            MVMSTable *known_type_st = NULL;
            MVMuint32  is_conc;
            MVMuint32  is_rw;

            /* Must know type. */
            if (!(facts->flags & MVM_SPESH_FACT_KNOWN_TYPE))
                return NULL;

            /* Must know if it's concrete or not. */
            if (!(facts->flags & (MVM_SPESH_FACT_CONCRETE | MVM_SPESH_FACT_TYPEOBJ)))
                return NULL;

            /* If it's a container, must know what's inside it. Otherwise,
             * we're already good on type info. */
            if ((facts->flags & MVM_SPESH_FACT_CONCRETE) && STABLE(facts->type)->container_spec) {
                /* Again, need to know type and concreteness. */
                if (!(facts->flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE))
                    return NULL;
                if (!(facts->flags & (MVM_SPESH_FACT_DECONT_CONCRETE | MVM_SPESH_FACT_DECONT_TYPEOBJ)))
                    return NULL;
                known_type_st = STABLE(facts->decont_type);
                is_conc = (facts->flags & MVM_SPESH_FACT_DECONT_CONCRETE) ? 1 : 0;
                is_rw = (facts->flags & MVM_SPESH_FACT_RW_CONT) ? 1 : 0;
            }
            else {
                known_type_st = STABLE(facts->type);
                is_conc = (facts->flags & MVM_SPESH_FACT_CONCRETE) ? 1 : 0;
                is_rw = is_conc && REPR(facts->type)->ID == MVM_REPR_ID_NativeRef;
            }

            /* Now check if what we have matches what we need. */
            if (known_type_st->type_cache_id == type_id) {
                MVMuint32 need_concrete = (arg_match & MVM_MULTICACHE_ARG_CONC_FILTER) ? 1 : 0;
                if (is_conc == need_concrete) {
                    MVMuint32 need_rw = (arg_match & MVM_MULTICACHE_ARG_RW_FILTER) ? 1 : 0;
                    if (need_rw == is_rw) {
                        cur_node = tree[cur_node].match;
                        continue;
                    }
                }
            }
            cur_node = tree[cur_node].no_match;
        }
        else {
            /* No facts about this argument available from analysis, so
             * can't resolve the dispatch. */
            return NULL;
        }
    }

    /* Negate result and index into results (the first result is always NULL
     * to save flow control around "no match"). */
    return cache->results[-cur_node];
}
