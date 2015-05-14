#include "moar.h"

/* Creates a new serialization context with the specified handle. If any
 * compilation units are waiting for an SC with this handle, removes it from
 * their to-resolve list after installing itself in the appropriate slot. */
MVMObject * MVM_sc_create(MVMThreadContext *tc, MVMString *handle) {
    MVMSerializationContext     *sc;
    MVMSerializationContextBody *scb;

    /* Allocate. */
    MVMROOT(tc, handle, {
        sc = (MVMSerializationContext *)REPR(tc->instance->SCRef)->allocate(tc, STABLE(tc->instance->SCRef));
        MVMROOT(tc, sc, {
            /* Add to weak lookup hash. */
            uv_mutex_lock(&tc->instance->mutex_sc_weakhash);
            MVM_string_flatten(tc, handle);
            MVM_HASH_GET(tc, tc->instance->sc_weakhash, handle, scb);
            if (!scb) {
                sc->body = scb = MVM_calloc(1, sizeof(MVMSerializationContextBody));
                MVM_ASSIGN_REF(tc, &(sc->common.header), scb->handle, handle);
                MVM_HASH_BIND(tc, tc->instance->sc_weakhash, handle, scb);
                /* Calling repr_init will allocate, BUT if it does so, and we
                 * get unlucky, the GC will try to acquire mutex_sc_weakhash.
                 * This deadlocks. Thus, we force allocation in gen2, which
                 * can never trigger GC. Note that releasing the mutex early
                 * is not a good way to fix this, as it leaves a race to
                 * test/set scb->sc (between the line doing it in this block,
                 * and in the else clauses beneath it). */
                MVM_gc_allocate_gen2_default_set(tc);
                MVM_repr_init(tc, (MVMObject *)sc);
                MVM_gc_allocate_gen2_default_clear(tc);
                scb->sc = sc;
                MVM_sc_add_all_scs_entry(tc, scb);
            }
            else if (scb->sc) {
                /* we lost a race to create it! */
                sc = scb->sc;
            }
            else {
                scb->sc = sc;
                sc->body = scb;
                MVM_ASSIGN_REF(tc, &(sc->common.header), scb->handle, handle);
                MVM_repr_init(tc, (MVMObject *)sc);
            }
            uv_mutex_unlock(&tc->instance->mutex_sc_weakhash);
        });
    });

    return (MVMObject *)sc;
}

/* Makes an entry in all SCs list, the index of which is used to refer to
 * SCs in object headers. */
void MVM_sc_add_all_scs_entry(MVMThreadContext *tc, MVMSerializationContextBody *scb) {
    if (tc->instance->all_scs_next_idx == tc->instance->all_scs_alloc) {
        tc->instance->all_scs_alloc += 32;
        if (tc->instance->all_scs_next_idx == 0) {
            /* First time; allocate, and NULL first slot as it is
             * the "no SC" sentinel value. */
            tc->instance->all_scs    = MVM_malloc(tc->instance->all_scs_alloc * sizeof(MVMSerializationContextBody *));
            tc->instance->all_scs[0] = NULL;
            tc->instance->all_scs_next_idx++;
        }
        else {
            tc->instance->all_scs = MVM_realloc(tc->instance->all_scs,
                tc->instance->all_scs_alloc * sizeof(MVMSerializationContextBody *));
        }
    }
    scb->sc_idx = tc->instance->all_scs_next_idx;
    tc->instance->all_scs[tc->instance->all_scs_next_idx] = scb;
    tc->instance->all_scs_next_idx++;
}

/* Given an SC, returns its unique handle. */
MVMString * MVM_sc_get_handle(MVMThreadContext *tc, MVMSerializationContext *sc) {
    return sc->body->handle;
}

/* Given an SC, returns its description. */
MVMString * MVM_sc_get_description(MVMThreadContext *tc, MVMSerializationContext *sc) {
    return sc->body->description;
}

/* Given an SC, sets its description. */
void MVM_sc_set_description(MVMThreadContext *tc, MVMSerializationContext *sc, MVMString *desc) {
    MVM_ASSIGN_REF(tc, &(sc->common.header), sc->body->description, desc);
}

/* Given an SC, looks up the index of an object that is in its root set. */
MVMint64 MVM_sc_find_object_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj) {
    MVMObject **roots;
    MVMint64    i, count;
    MVMuint32   cached = MVM_get_idx_in_sc(&obj->header);
    if (cached != ~0)
        return cached;
    roots = sc->body->root_objects;
    count = sc->body->num_objects;
    for (i = 0; i < count; i++)
        if (roots[i] == obj)
            return i;
    MVM_exception_throw_adhoc(tc,
        "Object does not exist in serialization context");
}

/* Given an SC, looks up the index of an STable that is in its root set. */
MVMint64 MVM_sc_find_stable_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMSTable *st) {
    MVMuint64 i;
    MVMuint32 cached = MVM_get_idx_in_sc(&st->header);
    if (cached != ~0)
        return cached;
    for (i = 0; i < sc->body->num_stables; i++)
        if (sc->body->root_stables[i] == st)
            return i;
    MVM_exception_throw_adhoc(tc,
        "STable does not exist in serialization context");
}

/* Given an SC, looks up the index of a code ref that is in its root set. */
MVMint64 MVM_sc_find_code_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj) {
    MVMObject *roots;
    MVMint64   i, count;
    roots = sc->body->root_codes;
    count = MVM_repr_elems(tc, roots);
    for (i = 0; i < count; i++) {
        MVMObject *test = MVM_repr_at_pos_o(tc, roots, i);
        if (test == obj)
            return i;
    }
    MVM_exception_throw_adhoc(tc,
        "Code ref '%s' does not exist in serialization context",
        REPR(obj)->ID == MVM_REPR_ID_MVMCode
            ? MVM_string_utf8_encode_C_string(tc, ((MVMCode *)obj)->body.name)
            : "<NOT A CODE OBJECT>");
}

/* Given a compilation unit and dependency index, returns that SC. */
MVMSerializationContext * MVM_sc_get_sc(MVMThreadContext *tc, MVMCompUnit *cu, MVMint16 dep) {
    MVMSerializationContext *sc = cu->body.scs[dep];
    if (sc == NULL) {
        MVMSerializationContextBody *scb = cu->body.scs_to_resolve[dep];
        if (!scb)
            MVM_exception_throw_adhoc(tc,
                "SC resolution: internal error");
        sc = scb->sc;
        if (sc == NULL)
            return NULL;
        MVM_ASSIGN_REF(tc, &(cu->common.header), cu->body.scs[dep], sc);
    }
    return sc;
}

/* Given an SC and an index, fetch the object stored there. */
MVMObject * MVM_sc_get_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    MVMObject **roots = sc->body->root_objects;
    MVMint64    count = sc->body->num_objects;
    if (idx >= 0 && idx < count)
        return roots[idx] ? roots[idx] : MVM_serialization_demand_object(tc, sc, idx);
    else
        MVM_exception_throw_adhoc(tc,
            "Probable version skew in pre-compiled '%s' (cause: no object at index %"PRId64")",
            MVM_string_utf8_encode_C_string(tc, sc->body->description), idx);
}

MVMObject * MVM_sc_get_sc_object(MVMThreadContext *tc, MVMCompUnit *cu,
                                 MVMint16 dep, MVMint64 idx) {
    if (dep >= 0 && dep < cu->body.num_scs) {
        MVMSerializationContext *sc = MVM_sc_get_sc(tc, cu, dep);
        if (sc == NULL)
            MVM_exception_throw_adhoc(tc, "SC not yet resolved; lookup failed");
        return MVM_sc_get_object(tc, sc, idx);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Invalid SC index in bytecode stream");
    }
}

/* Given an SC and an index, fetch the object stored there, or return NULL if
 * there is none. Does not cause lazy deserialization. */
MVMObject * MVM_sc_try_get_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    MVMObject **roots = sc->body->root_objects;
    MVMint64    count = sc->body->num_objects;
    if (idx > 0 && idx < count)
        return roots[idx];
    else
        return NULL;
}

/* Given an SC, an index, and an object, store the object at that index. */
void MVM_sc_set_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *obj) {
    if (idx < 0)
        MVM_exception_throw_adhoc(tc, "Invalid (negative) object root index %"PRId64"", idx);
    if (idx < sc->body->num_objects) {
        /* Just updating an existing one. */
        MVM_ASSIGN_REF(tc, &(sc->common.header), sc->body->root_objects[idx], obj);
    }
    else {
        if (idx >= sc->body->alloc_objects) {
            MVMint64 orig_size = sc->body->alloc_objects;
            sc->body->alloc_objects *= 2;
            if (sc->body->alloc_objects < idx + 1)
                sc->body->alloc_objects = idx + 1;
            sc->body->root_objects = MVM_realloc(sc->body->root_objects,
                sc->body->alloc_objects * sizeof(MVMObject *));
            memset(sc->body->root_objects + orig_size, 0,
                (sc->body->alloc_objects - orig_size) * sizeof(MVMObject *));
        }
        MVM_ASSIGN_REF(tc, &(sc->common.header), sc->body->root_objects[idx], obj);
        sc->body->num_objects = idx + 1;
    }
}

/* Given an SC and an index, fetch the STable stored there. */
MVMSTable * MVM_sc_get_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    if (idx >= 0 && idx < sc->body->num_stables) {
        MVMSTable *got = sc->body->root_stables[idx];
        return got ? got : MVM_serialization_demand_stable(tc, sc, idx);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Probable version skew in pre-compiled '%s' (cause: no STable at index %"PRId64")",
            MVM_string_utf8_encode_C_string(tc, sc->body->description), idx);
    }
}

/* Given an SC and an index, fetch the STable stored there, or return NULL if there
 * is none. Does not cause lazy deserialization. */
MVMSTable * MVM_sc_try_get_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    if (idx >= 0 && idx < sc->body->num_stables)
        return sc->body->root_stables[idx];
    else
        return NULL;
}

/* Given an SC, an index, and an STable, store the STable at the index. */
void MVM_sc_set_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMSTable *st) {
    if (idx < 0)
        MVM_exception_throw_adhoc(tc,
            "Invalid (negative) STable index %"PRId64, idx);
    if (idx < sc->body->num_stables) {
        /* Just updating an existing one. */
        MVM_ASSIGN_REF(tc, &(sc->common.header), sc->body->root_stables[idx], st);
    }
    else {
        if (idx >= sc->body->alloc_stables) {
            MVMint64 orig_size = sc->body->alloc_stables;
            sc->body->alloc_stables += 32;
            if (sc->body->alloc_stables < idx + 1)
                sc->body->alloc_stables = idx + 1;
            sc->body->root_stables = MVM_realloc(sc->body->root_stables,
                sc->body->alloc_stables * sizeof(MVMSTable *));
            memset(sc->body->root_stables + orig_size, 0,
                (sc->body->alloc_stables - orig_size) * sizeof(MVMSTable *));
        }
        MVM_ASSIGN_REF(tc, &(sc->common.header), sc->body->root_stables[idx], st);
        sc->body->num_stables = idx + 1;
    }
}


/* Given an SC and an STable, pushes the STable to the end of the root list. */
void MVM_sc_push_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMSTable *st) {
    MVMint64 idx = sc->body->num_stables;
    if (idx == sc->body->alloc_stables) {
        sc->body->alloc_stables += 16;
        sc->body->root_stables = MVM_realloc(sc->body->root_stables,
            sc->body->alloc_stables * sizeof(MVMSTable *));
    }
    MVM_ASSIGN_REF(tc, &(sc->common.header), sc->body->root_stables[idx], st);
    sc->body->num_stables++;
}

/* Given an SC and an index, fetch the code ref stored there. */
MVMObject * MVM_sc_get_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    MVMObject *roots = sc->body->root_codes;
    MVMuint64   count = MVM_repr_elems(tc, roots);
    if (idx < count) {
        MVMObject *found = MVM_repr_at_pos_o(tc, roots, idx);
        return MVM_is_null(tc, found)
            ? MVM_serialization_demand_code(tc, sc, idx)
            : found;
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Probable version skew in pre-compiled '%s' (cause: no code ref at index %"PRId64")",
            MVM_string_utf8_encode_C_string(tc, sc->body->description), idx);
    }
}

/* Resolves an SC handle using the SC weakhash. */
MVMSerializationContext * MVM_sc_find_by_handle(MVMThreadContext *tc, MVMString *handle) {
    MVMSerializationContextBody *scb;
    MVM_string_flatten(tc, handle);
    uv_mutex_lock(&tc->instance->mutex_sc_weakhash);
    MVM_HASH_GET(tc, tc->instance->sc_weakhash, handle, scb);
    uv_mutex_unlock(&tc->instance->mutex_sc_weakhash);
    return scb && scb->sc ? scb->sc : NULL;
}

/* Called when an object triggers the SC repossession write barrier. */
void MVM_sc_wb_hit_obj(MVMThreadContext *tc, MVMObject *obj) {
    MVMSerializationContext *comp_sc;

    /* If the WB is disabled or we're not compiling, can exit quickly. */
    if (tc->sc_wb_disable_depth)
        return;
    if (!tc->compiling_scs || !MVM_repr_elems(tc, tc->compiling_scs))
        return;

    /* Same if the object is flagged as one to never repossess. */
    if (obj->header.flags & MVM_CF_NEVER_REPOSSESS)
        return;

    /* Otherwise, check that the object's SC is different from the SC
     * of the compilation we're currently in. Repossess if so. */
    comp_sc = (MVMSerializationContext *)MVM_repr_at_pos_o(tc, tc->compiling_scs, 0);
    if (MVM_sc_get_obj_sc(tc, obj) != comp_sc) {
        /* Get new slot ID. */
        MVMint64 new_slot = comp_sc->body->num_objects;

        /* See if the object is actually owned by another, and it's the
         * owner we need to repossess. */
        if (obj->st->WHAT == tc->instance->boot_types.BOOTArray ||
            obj->st->WHAT == tc->instance->boot_types.BOOTHash) {
            MVMObject *owned_objects = MVM_sc_get_obj_sc(tc, obj)->body->owned_objects;
            MVMint64 n = MVM_repr_elems(tc, owned_objects);
            MVMint64 found = 0;
            MVMint64 i;
            for (i = 0; i < n; i += 2) {
                if (MVM_repr_at_pos_o(tc, owned_objects, i) == obj) {
                    obj = MVM_repr_at_pos_o(tc, owned_objects, i + 1);
                    if (MVM_sc_get_obj_sc(tc, obj) == comp_sc)
                        return;
                    found = 1;
                    break;
                }
            }
            if (!found)
                return;
        }

        /* Add to root set. */
        MVM_sc_set_object(tc, comp_sc, new_slot, obj);

        /* Add repossession entry. */
        MVM_repr_push_i(tc, comp_sc->body->rep_indexes, new_slot << 1);
        MVM_repr_push_o(tc, comp_sc->body->rep_scs, (MVMObject *)MVM_sc_get_obj_sc(tc, obj));

        /* Update SC of the object, claiming it. */
        MVM_sc_set_obj_sc(tc, obj, comp_sc);
    }
}

/* Called when an STable triggers the SC repossession write barrier. */
void MVM_sc_wb_hit_st(MVMThreadContext *tc, MVMSTable *st) {
    MVMSerializationContext *comp_sc;

    /* If the WB is disabled or we're not compiling, can exit quickly. */
    if (tc->sc_wb_disable_depth)
        return;
    if (!tc->compiling_scs || !MVM_repr_elems(tc, tc->compiling_scs))
        return;

    /* Otherwise, check that the STable's SC is different from the SC
     * of the compilation we're currently in. Repossess if so. */
    comp_sc = (MVMSerializationContext *)MVM_repr_at_pos_o(tc, tc->compiling_scs, 0);
    if (MVM_sc_get_stable_sc(tc, st) != comp_sc) {
        /* Add to root set. */
        MVMint64 new_slot = comp_sc->body->num_stables;
        MVM_sc_push_stable(tc, comp_sc, st);

        /* Add repossession entry. */
        MVM_repr_push_i(tc, comp_sc->body->rep_indexes, (new_slot << 1) | 1);
        MVM_repr_push_o(tc, comp_sc->body->rep_scs, (MVMObject *)MVM_sc_get_stable_sc(tc, st));

        /* Update SC of the STable, claiming it. */
        MVM_sc_set_stable_sc(tc, st, comp_sc);
    }
}
