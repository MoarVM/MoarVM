#include "moarvm.h"

/* Given an SC, returns its unique handle. */
MVMString * MVM_sc_get_handle(MVMThreadContext *tc, MVMSerializationContext *sc) {
    return sc->handle;
}

/* Given an SC, returns its description. */
MVMString * MVM_sc_get_description(MVMThreadContext *tc, MVMSerializationContext *sc) {
    return sc->description;
}

/* Given an SC, looks up the index of an object that is in its root set. */
MVMint64 MVM_sc_find_object_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj) {
    MVMObject *roots;
    MVMint64   i, count;
    roots = sc->root_objects;
    count = REPR(roots)->elems(tc, STABLE(roots), roots, OBJECT_BODY(roots));
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
    for (i = 0; i < sc->num_stables; i++)
        if (sc->root_stables[i] == st)
            return i;
    MVM_exception_throw_adhoc(tc,
        "STable does not exist in serialization context");
}

/* Given an SC, looks up the index of a code ref that is in its root set. */
MVMint64 MVM_sc_find_code_idx(MVMThreadContext *tc, MVMSerializationContext *sc, MVMObject *obj) {
    MVMObject *roots;
    MVMint64   i, count;
    roots = sc->root_codes;
    count = REPR(roots)->elems(tc, STABLE(roots), roots, OBJECT_BODY(roots));
    for (i = 0; i < count; i++) {
        MVMObject *test = MVM_repr_at_pos_o(tc, roots, i);
        if (test == obj)
            return i;
    }
    MVM_exception_throw_adhoc(tc,
        "Code ref does not exist in serialization context");
}

/* Given an SC and an index, fetch the object stored there. */
MVMObject * MVM_sc_get_object(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    MVMObject *roots = sc->root_objects;
    MVMint64   count = REPR(roots)->elems(tc, STABLE(roots), roots, OBJECT_BODY(roots));
    if (idx < count)
        return MVM_repr_at_pos_o(tc, roots, idx);
    else
        MVM_exception_throw_adhoc(tc,
            "No object at index %d", idx);
}

/* Given an SC and an index, fetch the STable stored there. */
MVMSTable * MVM_sc_get_stable(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    if (idx > 0 && idx < sc->num_stables)
        return sc->root_stables[idx];
    else
        MVM_exception_throw_adhoc(tc,
            "No STable at index %d", idx);
}

/* Given an SC and an index, fetch the code ref stored there. */
MVMObject * MVM_sc_get_code(MVMThreadContext *tc, MVMSerializationContext *sc, MVMint64 idx) {
    MVMObject *roots = sc->root_codes;
    MVMint64   count = REPR(roots)->elems(tc, STABLE(roots), roots, OBJECT_BODY(roots));
    if (idx < count)
        return MVM_repr_at_pos_o(tc, roots, idx);
    else
        MVM_exception_throw_adhoc(tc,
            "No code ref at index %d", idx);
}
