#include "moar.h"

/* Tries to intern the callsite, freeing and updating the one passed in and
 * replacing it with an already interned one if we find it. */
void MVM_callsite_try_intern(MVMThreadContext *tc, MVMCallsite **cs_ptr) {
    MVMCallsiteInterns *interns = tc->instance->callsite_interns;
    MVMCallsite        *cs      = *cs_ptr;
    MVMint32            num_pos = cs->num_pos;
    MVMint32          num_flags = num_pos + (cs->arg_count - num_pos) / 2;
    MVMint32 i, j, found;

    /* Can't intern anything with flattening, for now. */
    if (cs->has_flattening)
        return;

    /* Also can't intern past the max arity. */
    if (num_flags >= MVM_INTERN_ARITY_LIMIT)
        return;

    if (num_flags > num_pos && !cs->arg_name)
        return;

    for (j = 0; j < num_flags - num_pos; j++) {
        if (!cs->arg_name[j])
            return;
    }

    /* Obtain mutex protecting interns store. */
    uv_mutex_lock(&tc->instance->mutex_callsite_interns);

    /* Search for a match. */
    found = 0;
    for (i = 0; i < interns->num_by_arity[num_pos]; i++) {
        if (cs->arg_count != interns->by_arity[num_pos][i]->arg_count)
            continue;
        if (memcmp(interns->by_arity[num_pos][i]->arg_flags, cs->arg_flags, num_flags) == 0) {
            /* Now let's have a look at the named arguments. */
            for (j = 0; j < num_flags - num_pos; j++) {
                /* if any of the nameds are not known at this time, we skip this */
                if (   !interns->by_arity[num_pos][i]->arg_name || !interns->by_arity[num_pos][i]->arg_name[j]
                    || !MVM_string_equal(tc, cs->arg_name[j], interns->by_arity[num_pos][i]->arg_name[j])) {
                    goto cancel;
                }
            }
            /* Got a match! Free the one we were passed and replace it with
             * the interned one. */
            if (num_pos)
                free(cs->arg_flags);
            if (num_flags > num_pos)
                free(cs->arg_name);
            free(cs);
            *cs_ptr = interns->by_arity[num_pos][i];
            if (num_flags > num_pos)
                printf("interned a callsite with %d nameds\n", num_flags - num_pos);
            found = 1;
cancel:     break;
        }
    }

    /* If it wasn't found, store it for the future. */
    if (!found) {
        if (interns->num_by_arity[cs->num_pos] % 8 == 0) {
            if (interns->num_by_arity[cs->num_pos])
                interns->by_arity[cs->num_pos] = realloc(
                    interns->by_arity[cs->num_pos],
                    sizeof(MVMCallsite *) * (interns->num_by_arity[cs->num_pos] + 8));
            else
                interns->by_arity[cs->num_pos] = malloc(sizeof(MVMCallsite *) * 8);
        }
        interns->by_arity[cs->num_pos][interns->num_by_arity[cs->num_pos]++] = cs;
        cs->is_interned = 1;
    }

    /* Finally, release mutex. */
    uv_mutex_unlock(&tc->instance->mutex_callsite_interns);
}
