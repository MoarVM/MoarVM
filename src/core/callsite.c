#include "moar.h"

/* Tries to intern the callsite, freeing and updating the one passed in and
 * replacing it with an already interned one if we find it. */
void MVM_callsite_try_intern(MVMThreadContext *tc, MVMCallsite **cs_ptr) {
    MVMCallsiteInterns *interns = tc->instance->callsite_interns;
    MVMCallsite        *cs      = *cs_ptr;
    MVMint32 i, found;

    /* Can't intern anything with named or flattening, for now. */
    if (cs->arg_count != cs->num_pos)
        return;
    if (cs->has_flattening)
        return;

    /* Also can't intern past the max arity. */
    if (cs->num_pos >= MVM_INTERN_ARITY_LIMIT)
        return;

    /* Obtain mutex protecting interns store. */
    uv_mutex_lock(&tc->instance->mutex_callsite_interns);

    /* Search for a match. */
    found = 0;
    for (i = 0; i < interns->num_by_arity[cs->num_pos]; i++) {
        if (memcmp(interns->by_arity[cs->num_pos][i]->arg_flags, cs->arg_flags, cs->num_pos) == 0) {
            /* Got a match! Free the one we were passed and replace it with
             * the interned one. */
            if (cs->num_pos)
                free(cs->arg_flags);
            free(cs);
            *cs_ptr = interns->by_arity[cs->num_pos][i];
            found = 1;
            break;
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
