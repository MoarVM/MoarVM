#include "moar.h"

/* Checks if two callsiates are equal. */
static MVMint32 callsites_equal(MVMThreadContext *tc, MVMCallsite *cs1, MVMCallsite *cs2,
                                MVMint32 num_flags, MVMint32 num_nameds) {
    MVMint32 i;

    if (memcmp(cs1->arg_flags, cs2->arg_flags, num_flags))
        return 0;

    for (i = 0; i < num_nameds; i++)
        if (!MVM_string_equal(tc, cs1->arg_names[i], cs2->arg_names[i]))
            return 0;

    return 1;
}

static MVMCallsite   null_args_callsite = { NULL, 0, 0, 0, 0, 0, 0 };

static MVMCallsiteEntry obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     inv_arg_callsite = { obj_arg_flags, 1, 1, 0, 0, 0, 0 };

static MVMCallsiteEntry two_obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                                MVM_CALLSITE_ARG_OBJ };
static MVMCallsite    two_args_callsite = { two_obj_arg_flags, 2, 2, 0, 0, 0 };

static MVMCallsiteEntry mnfe_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                         MVM_CALLSITE_ARG_STR };
static MVMCallsite     methnotfound_callsite = { mnfe_flags, 2, 2, 0 };

static MVMCallsiteEntry fm_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_STR };
static MVMCallsite     findmeth_callsite = { fm_flags, 3, 3, 0 };

static MVMCallsiteEntry tc_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     typecheck_callsite = { tc_flags, 3, 3, 0 };

MVM_PUBLIC MVMCallsite *MVM_callsite_get_common(MVMThreadContext *tc, MVMCommonCallsiteID id) {
    switch (id) {
        case MVM_CALLSITE_ID_NULL_ARGS:
            return &null_args_callsite;
        case MVM_CALLSITE_ID_INV_ARG:
            return &inv_arg_callsite;
        case MVM_CALLSITE_ID_TWO_OBJ:
            return &two_args_callsite;
        case MVM_CALLSITE_ID_METH_NOT_FOUND:
            return &methnotfound_callsite;
        case MVM_CALLSITE_ID_FIND_METHOD:
            return &findmeth_callsite;
        case MVM_CALLSITE_ID_TYPECHECK:
            return &typecheck_callsite;
        default:
            MVM_exception_throw_adhoc(tc, "get_common_callsite: id %d unknown", id);
            return NULL;
    }
}

void MVM_callsite_initialize_common(MVMThreadContext *tc) {
    MVMCallsite *ptr;

    ptr = &inv_arg_callsite;
    MVM_callsite_try_intern(tc, &ptr);
    ptr = &null_args_callsite;
    MVM_callsite_try_intern(tc, &ptr);
    ptr = &methnotfound_callsite;
    MVM_callsite_try_intern(tc, &ptr);
    ptr = &two_args_callsite;
    MVM_callsite_try_intern(tc, &ptr);
    ptr = &findmeth_callsite;
    MVM_callsite_try_intern(tc, &ptr);
    ptr = &typecheck_callsite;
    MVM_callsite_try_intern(tc, &ptr);
}

/* Tries to intern the callsite, freeing and updating the one passed in and
 * replacing it with an already interned one if we find it. */
MVM_PUBLIC void MVM_callsite_try_intern(MVMThreadContext *tc, MVMCallsite **cs_ptr) {
    MVMCallsiteInterns *interns    = tc->instance->callsite_interns;
    MVMCallsite        *cs         = *cs_ptr;
    MVMint32            num_nameds = (cs->arg_count - cs->num_pos) / 2;
    MVMint32            num_flags  = cs->num_pos + num_nameds;
    MVMint32 i, found;

    /* Can't intern anything with flattening. */
    if (cs->has_flattening)
        return;

    /* Can intern things with nameds, provided we know the names. */
    if (num_nameds > 0 && !cs->arg_names)
        return;

    /* Also can't intern past the max arity. */
    if (num_flags >= MVM_INTERN_ARITY_LIMIT)
        return;

    /* Obtain mutex protecting interns store. */
    uv_mutex_lock(&tc->instance->mutex_callsite_interns);

    /* Search for a match. */
    found = 0;
    for (i = 0; i < interns->num_by_arity[num_flags]; i++) {
        if (callsites_equal(tc, interns->by_arity[num_flags][i], cs, num_flags, num_nameds)) {
            /* Got a match! Free the one we were passed and replace it with
             * the interned one. */
            if (num_flags)
                MVM_free(cs->arg_flags);
            MVM_free(cs);
            *cs_ptr = interns->by_arity[num_flags][i];
            found = 1;
            break;
        }
    }

    /* If it wasn't found, store it for the future. */
    if (!found) {
        if (interns->num_by_arity[num_flags] % 8 == 0) {
            if (interns->num_by_arity[num_flags])
                interns->by_arity[num_flags] = MVM_realloc(
                    interns->by_arity[num_flags],
                    sizeof(MVMCallsite *) * (interns->num_by_arity[num_flags] + 8));
            else
                interns->by_arity[num_flags] = MVM_malloc(sizeof(MVMCallsite *) * 8);
        }
        interns->by_arity[num_flags][interns->num_by_arity[num_flags]++] = cs;
        cs->is_interned = 1;
    }

    /* Finally, release mutex. */
    uv_mutex_unlock(&tc->instance->mutex_callsite_interns);
}
