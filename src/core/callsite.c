#include "moar.h"

/* Some callsites used in the VM are interned at startup from static memory. */

static MVMCallsite   zero_arity_callsite = { NULL, 0, 0, 0, 0, 0, 0, 0 };

static MVMCallsiteEntry obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     obj_callsite = { obj_arg_flags, 1, 1, 1, 0, 0, 0, 0 };

static MVMCallsiteEntry obj_obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                                MVM_CALLSITE_ARG_OBJ };
static MVMCallsite    obj_obj_callsite = { obj_obj_arg_flags, 2, 2, 2, 0, 0, NULL, NULL };

static MVMCallsiteEntry obj_int_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                            MVM_CALLSITE_ARG_INT };
static MVMCallsite    obj_int_callsite = { obj_int_flags, 2, 2, 2, 0, 0, NULL, NULL };

static MVMCallsiteEntry obj_num_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                            MVM_CALLSITE_ARG_NUM };
static MVMCallsite    obj_num_callsite = { obj_num_flags, 2, 2, 2, 0, 0, NULL, NULL };

static MVMCallsiteEntry obj_str_arg_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                         MVM_CALLSITE_ARG_STR };
static MVMCallsite     obj_str_callsite = { obj_str_arg_flags, 2, 2, 2, 0, 0, NULL, NULL };

static MVMCallsiteEntry int_int_arg_flags[] = { MVM_CALLSITE_ARG_INT, MVM_CALLSITE_ARG_INT };
static MVMCallsite     int_int_callsite = { int_int_arg_flags, 2, 2, 2, 0, 0, 0, 0 };

static MVMCallsiteEntry obj_obj_str_arg_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_STR };
static MVMCallsite     obj_obj_str_callsite = { obj_obj_str_arg_flags, 3, 3, 3, 0, 0, NULL, NULL };

static MVMCallsiteEntry obj_obj_obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     obj_obj_obj_callsite = { obj_obj_obj_arg_flags, 3, 3, 3, 0, 0, NULL, NULL };

/* Intern common callsites at startup. */
void MVM_callsite_initialize_common(MVMThreadContext *tc) {
    MVMCallsite *ptr;
    ptr = &zero_arity_callsite;
    MVM_callsite_intern(tc, &ptr, 0, 1);
    ptr = &obj_callsite;
    MVM_callsite_intern(tc, &ptr, 0, 1);
    ptr = &obj_obj_callsite;
    MVM_callsite_intern(tc, &ptr, 0, 1);
    ptr = &obj_int_callsite;
    MVM_callsite_intern(tc, &ptr, 0, 1);
    ptr = &obj_num_callsite;
    MVM_callsite_intern(tc, &ptr, 0, 1);
    ptr = &obj_str_callsite;
    MVM_callsite_intern(tc, &ptr, 0, 1);
    ptr = &int_int_callsite;
    MVM_callsite_intern(tc, &ptr, 0, 1);
    ptr = &obj_obj_str_callsite;
    MVM_callsite_intern(tc, &ptr, 0, 1);
    ptr = &obj_obj_obj_callsite;
    MVM_callsite_intern(tc, &ptr, 0, 1);
}

/* Obtain one of the common callsites. */
MVM_PUBLIC MVMCallsite * MVM_callsite_get_common(MVMThreadContext *tc, MVMCommonCallsiteID id) {
    switch (id) {
        case MVM_CALLSITE_ID_ZERO_ARITY:
            return &zero_arity_callsite;
        case MVM_CALLSITE_ID_OBJ:
            return &obj_callsite;
        case MVM_CALLSITE_ID_OBJ_OBJ:
            return &obj_obj_callsite;
        case MVM_CALLSITE_ID_OBJ_INT:
            return &obj_int_callsite;
        case MVM_CALLSITE_ID_OBJ_NUM:
            return &obj_num_callsite;
        case MVM_CALLSITE_ID_OBJ_STR:
            return &obj_str_callsite;
        case MVM_CALLSITE_ID_INT_INT:
            return &int_int_callsite;
        case MVM_CALLSITE_ID_OBJ_OBJ_STR:
            return &obj_obj_str_callsite;
        case MVM_CALLSITE_ID_OBJ_OBJ_OBJ:
            return &obj_obj_obj_callsite;
        default:
            MVM_exception_throw_adhoc(tc, "get_common_callsite: id %d unknown", id);
    }
}

/* Checks if two callsites are equal. */
static MVMint32 callsites_equal(MVMThreadContext *tc, MVMCallsite *cs1, MVMCallsite *cs2,
                                MVMint32 num_flags, MVMint32 num_nameds) {
    if (num_flags && memcmp(cs1->arg_flags, cs2->arg_flags, num_flags))
        return 0;

    MVMint32 i;
    for (i = 0; i < num_nameds; i++)
        if (!MVM_string_equal(tc, cs1->arg_names[i], cs2->arg_names[i]))
            return 0;

    return 1;
}

/* Destroy a callsite, freeing the memory associated with it. */
void MVM_callsite_destroy(MVMCallsite *cs) {
    if (cs->flag_count) {
        MVM_free(cs->arg_flags);
    }

    if (cs->arg_names) {
        MVM_free(cs->arg_names);
    }

    if (cs->with_invocant) {
        MVM_callsite_destroy(cs->with_invocant);
    }

    MVM_free(cs);
}

/* Copies the named args of one callsite into another. */
void copy_nameds(MVMCallsite *to, const MVMCallsite *from) {
    if (from->arg_names) {
        MVMuint32 num_names = from->flag_count - from->num_pos;
        size_t memory_area = num_names * sizeof(MVMString *);
        to->arg_names = MVM_malloc(memory_area);
        memcpy(to->arg_names, from->arg_names, memory_area);
    }
    else {
        to->arg_names = NULL;
    }
}

/* Copy a callsite. */
MVMCallsite * MVM_callsite_copy(MVMThreadContext *tc, const MVMCallsite *cs) {
    MVMCallsite *copy = MVM_malloc(sizeof(MVMCallsite));

    if (cs->flag_count) {
        copy->arg_flags =  MVM_malloc(cs->flag_count);
        memcpy(copy->arg_flags, cs->arg_flags, cs->flag_count);
    }

    copy_nameds(copy, cs);

    copy->with_invocant = cs->with_invocant
        ? MVM_callsite_copy(tc, cs->with_invocant)
        : NULL;

    copy->flag_count = cs->flag_count;
    copy->arg_count = cs->arg_count;
    copy->num_pos = cs->num_pos;
    copy->has_flattening = cs->has_flattening;
    copy->is_interned = cs->is_interned;

    return copy;
}

/* Interns a callsite, provided it is possible to do so. The force option
 * means we should intern it no matter what, even if it's beyond our
 * preferred size. The steal option means that we should take over the
 * management of the memory of the callsite that is passed; if we replace
 * it with an existing intern, then we should free it, otherwise we can
 * just use it as the interned one. If steal is set to false, and we want
 * to intern the callsite, then we should make a copy of it and intern
 * that. */
MVM_PUBLIC void MVM_callsite_intern(MVMThreadContext *tc, MVMCallsite **cs_ptr,
        MVMuint32 force, MVMuint32 steal) {
    MVMCallsiteInterns *interns    = tc->instance->callsite_interns;
    MVMCallsite        *cs         = *cs_ptr;
    MVMint32            num_flags  = cs->flag_count;
    MVMint32            num_nameds = MVM_callsite_num_nameds(tc, cs);
    MVMint32 i, found;

    /* Can't intern anything with flattening. */
    if (cs->has_flattening) {
        if (force)
            MVM_exception_throw_adhoc(tc,
                "Should not force interning of a flattening callsite");
        return;
    }

    /* Cannot intern things when we don't have the callsite names (we always
     * should nowadays, so TODO remove this after adding a check in bytecode.c
     * that we always have them.) */
    if (num_nameds > 0 && !cs->arg_names) {
        if (force)
            MVM_exception_throw_adhoc(tc, "Force interning of a callsite without named arg names");
        return;
    }

    /* We don't like to intern beyond a given arity limit, but will if force
     * mode is on. */
    if (num_flags >= MVM_INTERN_ARITY_LIMIT) {
        // TODO
        if (force)
            MVM_exception_throw_adhoc(tc, "Force interning of large callsites NYI");
        return;
    }

    /* Obtain mutex protecting interns store. */
    uv_mutex_lock(&tc->instance->mutex_callsite_interns);

    /* Search for a match. */
    found = 0;
    for (i = 0; i < interns->num_by_arity[num_flags]; i++) {
        if (callsites_equal(tc, interns->by_arity[num_flags][i], cs, num_flags, num_nameds)) {
            /* Got a match! If we were asked to steal the callsite we were passed,
             * then we should free it. */
            if (steal) {
                if (num_flags)
                    MVM_free(cs->arg_flags);
                MVM_free(cs->arg_names);
                MVM_free(cs);
            }
            *cs_ptr = interns->by_arity[num_flags][i];
            found = 1;
            break;
        }
    }

    /* If it wasn't found, store it for the future. */
    if (!found) {
        if (interns->num_by_arity[num_flags] % MVM_INTERN_ARITY_LIMIT == 0) {
            if (interns->num_by_arity[num_flags])
                interns->by_arity[num_flags] = MVM_realloc(
                    interns->by_arity[num_flags],
                    sizeof(MVMCallsite *) * (interns->num_by_arity[num_flags] + MVM_INTERN_ARITY_LIMIT));
            else
                interns->by_arity[num_flags] = MVM_malloc(sizeof(MVMCallsite *) * MVM_INTERN_ARITY_LIMIT);
        }
        if (steal) {
            cs->is_interned = 1;
            interns->by_arity[num_flags][interns->num_by_arity[num_flags]++] = cs;
        }
        else {
            MVMCallsite *copy = MVM_callsite_copy(tc, cs);
            copy->is_interned = 1;
            interns->by_arity[num_flags][interns->num_by_arity[num_flags]++] = cs;
            *cs_ptr = copy;
        }
    }

    /* Finally, release mutex. */
    uv_mutex_unlock(&tc->instance->mutex_callsite_interns);
}

/* Free the memory associated with interned callsites. */
static int is_common(MVMCallsite *cs) {
    return cs == &zero_arity_callsite   ||
           cs == &obj_callsite          ||
           cs == &obj_obj_callsite      ||
           cs == &obj_str_callsite      ||
           cs == &obj_int_callsite      ||
           cs == &obj_num_callsite      ||
           cs == &int_int_callsite      ||
           cs == &obj_obj_str_callsite  ||
           cs == &obj_obj_obj_callsite;
}
void MVM_callsite_cleanup_interns(MVMInstance *instance) {
    MVMuint32 i;
    for (i = 0; i < MVM_INTERN_ARITY_LIMIT; i++) {
        MVMuint32 callsite_count = instance->callsite_interns->num_by_arity[i];
        if (callsite_count) {
            MVMCallsite **callsites = instance->callsite_interns->by_arity[i];
            MVMuint32 j;
            for (j = 0; j < callsite_count; j++) {
                MVMCallsite *callsite = callsites[j];
                if (!is_common(callsite))
                    MVM_callsite_destroy(callsite);
            }
            MVM_free(callsites);
        }
    }
    MVM_free(instance->callsite_interns);
}

/* Produce a new callsite consisting of the current one with a positional
 * argument dropped. It will be interned if possible. */
MVMCallsite * MVM_callsite_drop_positional(MVMThreadContext *tc, MVMCallsite *cs, MVMuint32 idx) {
    /* Can only do this with positional arguments and non-flattening callsite. */
    if (idx >= cs->num_pos)
        MVM_exception_throw_adhoc(tc, "Cannot drop positional in callsite: index out of range");
    if (cs->has_flattening)
        MVM_exception_throw_adhoc(tc, "Cannot transform a callsite with flattening args");

    /* Allocate a new callsite and set it up. */
    MVMCallsite *new_callsite = MVM_calloc(1, sizeof(MVMCallsite));
    new_callsite->num_pos = cs->num_pos - 1;
    new_callsite->flag_count = cs->flag_count - 1;
    new_callsite->arg_count = cs->arg_count - 1;
    new_callsite->arg_flags = MVM_malloc(new_callsite->flag_count);
    MVMuint32 from, to = 0;
    for (from = 0; from < cs->flag_count; from++) {
        if (from != idx) {
            new_callsite->arg_flags[to] = cs->arg_flags[from];
            to++;
        }
    }
    copy_nameds(new_callsite, cs);

    /* Try to intern it, and return the result (which may be the interned
     * version that already existed, or may newly intern this). */
    MVM_callsite_intern(tc, &new_callsite, 0, 1);
    return new_callsite;
}

/* Produce a new callsite consisting of the current one with a positional
 * argument inserted. It will be interned if possible. */
MVMCallsite * MVM_callsite_insert_positional(MVMThreadContext *tc, MVMCallsite *cs, MVMuint32 idx,
        MVMCallsiteFlags flag) {
    /* Can only do this with positional arguments and non-flattening callsite. */
    if (idx > cs->num_pos)
        MVM_exception_throw_adhoc(tc, "Cannot drop positional in callsite: index out of range");
    if (cs->has_flattening)
        MVM_exception_throw_adhoc(tc, "Cannot transform a callsite with flattening args");

    /* Allocate a new callsite and set it up. */
    MVMCallsite *new_callsite = MVM_calloc(1, sizeof(MVMCallsite));
    new_callsite->num_pos = cs->num_pos + 1;
    new_callsite->flag_count = cs->flag_count + 1;
    new_callsite->arg_count = cs->arg_count + 1;
    new_callsite->arg_flags = MVM_malloc(new_callsite->flag_count);
    MVMuint32 from, to = 0;
    for (from = 0; from < cs->flag_count; from++) {
        if (from == idx) {
            new_callsite->arg_flags[to] = flag;
            to++;
        }
        new_callsite->arg_flags[to] = cs->arg_flags[from];
        to++;
    }
    if (from == idx)
        new_callsite->arg_flags[to] = flag;
    copy_nameds(new_callsite, cs);

    /* Try to intern it, and return the result (which may be the interned
     * version that already existed, or may newly intern this). */
    MVM_callsite_intern(tc, &new_callsite, 0, 1);
    return new_callsite;
}
