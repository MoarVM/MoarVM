#include "moar.h"

/* Takes a type and sets it up as a parametric type, provided it's OK to do so. */
void MVM_6model_parametric_setup(MVMThreadContext *tc, MVMObject *type, MVMObject *parameterizer) {
    MVMSTable *st = STABLE(type);

    /* Ensure that the type is not already parametric or parameterized. */
    if (st->mode_flags & MVM_PARAMETRIC_TYPE)
        MVM_exception_throw_adhoc(tc, "This type is already parametric");
    if (st->mode_flags & MVM_PARAMETERIZED_TYPE)
        MVM_exception_throw_adhoc(tc, "Cannot make a parameterized type also be parametric");

    /* For now, we use a simple pairwise array, with parameters and the type
     * that is based on those parameters interleaved. It does make resolution
     * O(n), so we might like to do some hash in the future. */
    MVMROOT2(tc, st, parameterizer, {
        MVMObject *lookup = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        MVM_ASSIGN_REF(tc, &(st->header), st->paramet.ric.lookup, lookup);
    });

     /* Store the parameterizer. (Note, we do this after the allocation
      * above, since the array allocation may cause GC, but we didn't mark
      * it as a parametric type yet so it wouldn't mark the parameterizer.) */
    MVM_ASSIGN_REF(tc, &(st->header), st->paramet.ric.parameterizer, parameterizer);

    /* Mark the type as parameterized. */
    st->mode_flags |= MVM_PARAMETRIC_TYPE;
}

/* Parameterize a type. Re-use an existing parameterization if there is one that
 * matches. Otherwise, run the parameterization creator. */
typedef struct {
    MVMObject   *parametric_type;
    MVMObject   *parameters;
    MVMRegister *result;
} ParameterizeReturnData;
static void finish_parameterizing(MVMThreadContext *tc, void *sr_data) {
    ParameterizeReturnData *prd = (ParameterizeReturnData *)sr_data;
    MVMObject *found;
    MVMObject *parameters = prd->parameters;
    MVMObject *parametric_type = prd->parametric_type;

    /* Mark parametric and stash required data. */
    MVMSTable *new_stable = STABLE(prd->result->o);
    MVM_ASSIGN_REF(tc, &(new_stable->header), new_stable->paramet.erized.parametric_type,
        prd->parametric_type);
    MVM_ASSIGN_REF(tc, &(new_stable->header), new_stable->paramet.erized.parameters,
        prd->parameters);
    new_stable->mode_flags |= MVM_PARAMETERIZED_TYPE;

    /* Add to lookup table. Multiple threads may race to do this, so after
     * taking the lock to serialize additions we re-check for a match. If we
     * don't find one, do a defensive copy here so that existing readers of
     * the table won't be bitten.
     * We may trigger garbage collection while holding the lock, so we need
     * to mark the thread as blocked for GC while waiting for the lock. */
    MVMROOT2(tc, parameters, parametric_type, {
        MVM_gc_mark_thread_blocked(tc);
        uv_mutex_lock(&tc->instance->mutex_parameterization_add);
        MVM_gc_mark_thread_unblocked(tc);

        found = MVM_6model_parametric_try_find_parameterization(tc,
            parametric_type->st, parameters);
        if (found) {
            prd->result->o = found;
        }
        else {
            MVMObject *copy = MVM_repr_clone(tc, parametric_type->st->paramet.ric.lookup);
            MVMROOT(tc, copy, {
                MVM_repr_push_o(tc, copy, parameters);
                MVM_repr_push_o(tc, copy, prd->result->o);
            });
            MVM_ASSIGN_REF(tc, &(parametric_type->st->header),
                parametric_type->st->paramet.ric.lookup, copy);
        }
        uv_mutex_unlock(&tc->instance->mutex_parameterization_add);
    });

    /* Clean up parametric return data, now we're finished with it. */
    MVM_free(prd);
}
static void mark_parameterize_sr_data(MVMThreadContext *tc, MVMFrame *frame, MVMGCWorklist *worklist) {
    ParameterizeReturnData *prd = (ParameterizeReturnData *)frame->extra->special_return_data;
    MVM_gc_worklist_add(tc, worklist, &(prd->parametric_type));
    MVM_gc_worklist_add(tc, worklist, &(prd->parameters));
}
static void free_parameterize_sr_data(MVMThreadContext *tc, void *sr_data) {
    MVM_free(sr_data);
}
void MVM_6model_parametric_parameterize(MVMThreadContext *tc, MVMObject *type, MVMObject *params,
                                        MVMRegister *result) {
    ParameterizeReturnData *prd;
    MVMObject *code, *found;

    /* Ensure we have a parametric type. */
    MVMSTable *st = STABLE(type);
    if (!(st->mode_flags & MVM_PARAMETRIC_TYPE))
        MVM_exception_throw_adhoc(tc, "This type is not parametric");

    /* Use an existing parameterization if we have it. */
    found = MVM_6model_parametric_try_find_parameterization(tc, st, params);
    if (found) {
        result->o = found;
        return;
    }

    /* It wasn't found; run parameterizer. */
    code = MVM_frame_find_invokee(tc, st->paramet.ric.parameterizer, NULL);
    prd  = MVM_malloc(sizeof(ParameterizeReturnData));
    prd->parametric_type                    = type;
    prd->parameters                         = params;
    prd->result                             = result;
    MVM_frame_special_return(tc, tc->cur_frame, finish_parameterizing, free_parameterize_sr_data,
        prd, mark_parameterize_sr_data);
    MVM_args_setup_thunk(tc, result, MVM_RETURN_OBJ, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ));
    tc->cur_frame->args[0].o = st->WHAT;
    tc->cur_frame->args[1].o = params;
    STABLE(code)->invoke(tc, code, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ), tc->cur_frame->args);
}

/* Try to find an existing parameterization of the specified type and
 * parameters. If none is found, returns NULL. */
MVMObject * MVM_6model_parametric_try_find_parameterization(MVMThreadContext *tc, MVMSTable *st, MVMObject *params) {
    MVMint64 i, j, num_lookups, params_elems;
    num_lookups  = MVM_repr_elems(tc, st->paramet.ric.lookup);
    params_elems = MVM_repr_elems(tc, params);
    for (i = 0; i < num_lookups; i += 2) {
        MVMObject *compare       = MVM_repr_at_pos_o(tc, st->paramet.ric.lookup, i);
        MVMint64   compare_elems = MVM_repr_elems(tc, compare);
        if (params_elems == compare_elems) {
            MVMint64 match = 1;
            for (j = 0; j < params_elems; j++) {
                MVMObject *want = MVM_repr_at_pos_o(tc, params, j);
                MVMObject *got  = MVM_repr_at_pos_o(tc, compare, j);
                if (want != got) {
                    match = 0;
                    break;
                }
            }
            if (match)
                return MVM_repr_at_pos_o(tc, st->paramet.ric.lookup, i + 1);
        }
    }
    return NULL;
}

/* If the passed type is a parameterized type, then returns the parametric
 * type it is based on. Otherwise, returns null. */
MVMObject * MVM_6model_parametric_type_parameterized(MVMThreadContext *tc, MVMObject *type) {
    MVMSTable *st = STABLE(type);
    if (st->mode_flags & MVM_PARAMETERIZED_TYPE)
        return st->paramet.erized.parametric_type;
    else
        return tc->instance->VMNull;
}

/* Provided this is a parameterized type, returns the array of type parameters. */
MVMObject * MVM_6model_parametric_type_parameters(MVMThreadContext *tc, MVMObject *type) {
    MVMSTable *st = STABLE(type);
    if (!(st->mode_flags & MVM_PARAMETERIZED_TYPE))
        MVM_exception_throw_adhoc(tc, "This type is not parameterized");
    return st->paramet.erized.parameters;
}

/* Provided this is a parameterized type, returns the type parameter at the specified index. */
MVMObject * MVM_6model_parametric_type_parameter_at(MVMThreadContext *tc, MVMObject *type, MVMint64 idx) {
    MVMSTable *st = STABLE(type);
    if (!(st->mode_flags & MVM_PARAMETERIZED_TYPE))
        MVM_exception_throw_adhoc(tc, "This type is not parameterized");
    return MVM_repr_at_pos_o(tc, st->paramet.erized.parameters, idx);
}
