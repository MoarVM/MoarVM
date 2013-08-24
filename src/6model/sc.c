#include "moarvm.h"

/* Creates a new serialization context with the specified handle. If any
 * compilation units are waiting for an SC with this handle, removes it from
 * their to-resolve list after installing itself in the appropriate slot. */
MVMObject * MVM_sc_create(MVMThreadContext *tc, MVMString *handle) {
    MVMSerializationContext     *sc;
    MVMCompUnit                 *cur_cu;
    MVMSerializationContextBody *scb;

    /* Allocate. */
    MVMROOT(tc, handle, {
        sc = (MVMSerializationContext *)REPR(tc->instance->SCRef)->allocate(tc, STABLE(tc->instance->SCRef));
        MVMROOT(tc, sc, {
            /* Add to weak lookup hash. */
            if (apr_thread_mutex_lock(tc->instance->mutex_sc_weakhash) != APR_SUCCESS)
                MVM_exception_throw_adhoc(tc, "Unable to lock SC weakhash");
            MVM_string_flatten(tc, handle);
            MVM_HASH_GET(tc, tc->instance->sc_weakhash, handle, scb);
            if (!scb) {
                sc->body = scb = calloc(1, sizeof(MVMSerializationContextBody));
                MVM_ASSIGN_REF(tc, (MVMObject *)sc, scb->handle, handle);
                MVM_HASH_BIND(tc, tc->instance->sc_weakhash, handle, scb);
                MVM_repr_init(tc, (MVMObject *)sc);
                scb->sc = sc;
            }
            else if (scb->sc) {
                /* we lost a race to create it! */
                sc = scb->sc;
            }
            else {
                scb->sc = sc;
                sc->body = scb;
                MVM_ASSIGN_REF(tc, sc, scb->handle, handle);
                MVM_repr_init(tc, (MVMObject *)sc);
            }
            if (apr_thread_mutex_unlock(tc->instance->mutex_sc_weakhash) != APR_SUCCESS)
                MVM_exception_throw_adhoc(tc, "Unable to unlock SC weakhash");
        });
    });

    return (MVMObject *)sc;
}

/* Given an SC, returns its unique handle. */
MVMString * MVM_sc_get_handle(MVMThreadContext *tc, MVMSerializationContext *sc) {
    return sc->body->handle;
}

/* Given an SC, returns its description. */
MVMString * MVM_sc_get_description(MVMThreadContext *tc, MVMSerializationContext *sc) {
    return sc->body->description;
}

/* Given an SC, looks up the index of an object that is in its root set. */
MVMint64 MVM_sc_find_object_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj) {
    MVMObject *roots;
    MVMint64   i, count;
    roots = sc->body->root_objects;
    count = MVM_repr_elems(tc, roots);
    for (i = 0; i < count; i++) {
        MVMObject *test = MVM_repr_at_pos_o(tc, roots, i);
        if (test == obj)
            return i;
    }
    MVM_exception_throw_adhoc(tc,
        "Object does not exist in serialization context");
}

/* Given an SC, looks up the index of an STable that is in its root set. */
MVMint64 MVM_sc_find_stable_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMSTable *st) {
    MVMuint64 i;
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
        "Code ref does not exist in serialization context");
}

/* Given a compilation unit and dependency index, returns that SC. */
MVMSerializationContext * MVM_sc_get_sc(MVMThreadContext *tc, MVMCompUnit *cu, MVMint16 dep) {
    MVMSerializationContext *sc = cu->body.scs[dep];
    if (sc == NULL) {
        MVMSerializationContextBody *scb = cu->body.scs_to_resolve[dep];
        if (!scb)
            MVM_exception_throw_adhoc(tc,
                "SC resolution; internal error");
        sc = scb->sc;
        if (sc == NULL)
            return NULL;
        cu->body.scs[dep] = sc;
    }
    return sc;
}

/* Given an SC and an index, fetch the object stored there. */
MVMObject * MVM_sc_get_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    MVMObject *roots = sc->body->root_objects;
    MVMint64   count = REPR(roots)->elems(tc, STABLE(roots), roots, OBJECT_BODY(roots));
    if (idx < count)
        return MVM_repr_at_pos_o(tc, roots, idx);
    else
        MVM_exception_throw_adhoc(tc,
            "No object at index %d", idx);
}

/* Given an SC, an index, and an object, store the object at that index. */
void MVM_sc_set_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *obj) {
    MVMObject *roots = sc->body->root_objects;
    MVMint64   count = MVM_repr_elems(tc, roots);
    MVM_repr_bind_pos_o(tc, roots, idx, obj);
}

/* Given an SC and an index, fetch the STable stored there. */
MVMSTable * MVM_sc_get_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    if (idx >= 0 && idx < sc->body->num_stables)
        return sc->body->root_stables[idx];
    else
        MVM_exception_throw_adhoc(tc,
            "No STable at index %d", idx);
}

/* Given an SC, an index, and an STable, store the STable at the index. */
void MVM_sc_set_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMSTable *st) {
    if (idx < 0)
        MVM_exception_throw_adhoc(tc,
            "Invalid (negative) STable index", idx);
    if (idx < sc->body->num_stables) {
        /* Just updating an existing one. */
        MVM_ASSIGN_REF(tc, (MVMObject *)sc, sc->body->root_stables[idx], st);
    }
    else if (idx == sc->body->num_stables) {
        /* Setting the next one. */
        if (idx == sc->body->alloc_stables) {
            sc->body->alloc_stables += 16;
            sc->body->root_stables = realloc(sc->body->root_stables,
                sc->body->alloc_stables * sizeof(MVMSTable *));
        }
        MVM_ASSIGN_REF(tc, (MVMObject *)sc, sc->body->root_stables[idx], st);
        sc->body->num_stables++;
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Gaps in STable root set not allowed");
    }
}


/* Given an SC and an STable, pushes the STable to the end of the root list. */
void MVM_sc_push_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMSTable *st) {
    MVMint64 idx = sc->body->num_stables;
    if (idx == sc->body->alloc_stables) {
        sc->body->alloc_stables += 16;
        sc->body->root_stables = realloc(sc->body->root_stables,
            sc->body->alloc_stables * sizeof(MVMSTable *));
    }
    MVM_ASSIGN_REF(tc, (MVMObject *)sc, sc->body->root_stables[idx], st);
    sc->body->num_stables++;
}


/* Given an SC and an index, fetch the code ref stored there. */
MVMObject * MVM_sc_get_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    MVMObject *roots = sc->body->root_codes;
    MVMint64   count = MVM_repr_elems(tc, roots);
    if (idx < count)
        return MVM_repr_at_pos_o(tc, roots, idx);
    else
        MVM_exception_throw_adhoc(tc,
            "No code ref at index %d", idx);
}

/* Given an SC, an index and a code ref, store it and the index. */
void MVM_sc_set_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx, MVMObject *code) {
    MVMObject *roots = sc->body->root_codes;
    MVMint64   count = MVM_repr_elems(tc, roots);
    MVM_repr_bind_pos_o(tc, roots, idx, code);
}

/* Sets the full list of code refs. */
void MVM_sc_set_code_list(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *code_list) {
    MVM_ASSIGN_REF(tc, sc, sc->body->root_codes, code_list);
}

/* Sets an object's SC. */
void MVM_sc_set_obj_sc(MVMThreadContext *tc, MVMObject *obj, MVMSerializationContext *sc) {
    MVM_ASSIGN_REF(tc, obj, obj->header.sc, sc);
}

/* Sets an STable's SC. */
void MVM_sc_set_stable_sc(MVMThreadContext *tc, MVMSTable *st, MVMSerializationContext *sc) {
    MVM_ASSIGN_REF(tc, st, st->header.sc, sc);
}

/* Resolves an SC handle using the SC weakhash. */
MVMSerializationContext * MVM_sc_find_by_handle(MVMThreadContext *tc, MVMString *handle) {
    MVMSerializationContextBody *scb;
    MVM_string_flatten(tc, handle);
    if (apr_thread_mutex_lock(tc->instance->mutex_sc_weakhash) != APR_SUCCESS)
        MVM_exception_throw_adhoc(tc, "Unable to lock SC weakhash");
    MVM_HASH_GET(tc, tc->instance->sc_weakhash, handle, scb);
    if (apr_thread_mutex_unlock(tc->instance->mutex_sc_weakhash) != APR_SUCCESS)
        MVM_exception_throw_adhoc(tc, "Unable to unlock SC weakhash");
    return scb && scb->sc ? scb->sc : NULL;
}
