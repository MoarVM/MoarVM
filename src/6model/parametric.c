#include "moar.h"

/* Takes a type and sets it up as a parametric type, provided it's OK to do so. */
void MVM_6model_parametric_setup(MVMThreadContext *tc, MVMObject *type, MVMObject *parameterizer) {
    MVMSTable *st = STABLE(type);

    /* Ensure that the type is not already parametric or parameterized. */
    if (st->mode_flags & MVM_PARAMETRIC_TYPE)
        MVM_exception_throw_adhoc(tc, "This type is already parametric");
    if (st->mode_flags & MVM_PARAMETERIZED_TYPE)
        MVM_exception_throw_adhoc(tc, "Cannot make a parameterized type also be parametric");

    /* Store the parameterizer. */
    MVM_ASSIGN_REF(tc, &(st->header), st->paramet.ric.parameterizer, parameterizer);

    /* For now, we use a simple pairwise array, with parameters and the type
     * that is based on those parameters interleaved. It does make resolution
     * O(n), so we might like to do some hash in the future. */
     MVMROOT(tc, st, {
        MVMObject *lookup = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        MVM_ASSIGN_REF(tc, &(st->header), st->paramet.ric.lookup, lookup);
     });

    /* Mark the type as parameterized. */
    st->mode_flags |= MVM_PARAMETRIC_TYPE;
}

/* Parameterize a type. Re-use an existing parameterization of there is one that
 * matches. Otherwise, run the parameterization creator. */
typedef struct {
    MVMObject   *parametric_type;
    MVMObject   *parameters;
    MVMRegister *result;
} ParameterizeReturnData;
static void finish_parameterizing(MVMThreadContext *tc, void *sr_data) {
    ParameterizeReturnData *prd = (ParameterizeReturnData *)sr_data;

    /* Mark parametric and stash required data. */
    MVMSTable *new_stable = STABLE(prd->result->o);
    MVM_ASSIGN_REF(tc, &(new_stable->header), new_stable->paramet.erized.parametric_type,
        prd->parametric_type);
    MVM_ASSIGN_REF(tc, &(new_stable->header), new_stable->paramet.erized.parameters,
        prd->parameters);
    new_stable->mode_flags |= MVM_PARAMETERIZED_TYPE;

    /* XXX TODO: add to lookup table, handle possible race. */
}
static void mark_parameterize_sr_data(MVMThreadContext *tc, MVMFrame *frame, MVMGCWorklist *worklist) {
    ParameterizeReturnData *prd = (ParameterizeReturnData *)frame->special_return_data;
    MVM_gc_worklist_add(tc, worklist, &(prd->parametric_type));
    MVM_gc_worklist_add(tc, worklist, &(prd->parameters));
}
void MVM_6model_parametric_parameterize(MVMThreadContext *tc, MVMObject *type, MVMObject *params,
                                        MVMRegister *result) {
    ParameterizeReturnData *prd;
    MVMObject *code;

    /* Ensure we have a parametric type. */
    MVMSTable *st = STABLE(type);
    if (!(st->mode_flags & MVM_PARAMETRIC_TYPE))
        MVM_exception_throw_adhoc(tc, "This type is not parametric");

    /* XXX TODO: Lookup. */

    /* It wasn't found; run parameterizer. */
    code = MVM_frame_find_invokee(tc, st->paramet.ric.parameterizer, NULL);
    prd  = MVM_malloc(sizeof(ParameterizeReturnData));
    prd->parametric_type                    = type;
    prd->parameters                         = params;
    prd->result                             = result;
    tc->cur_frame->special_return           = finish_parameterizing;
    tc->cur_frame->special_return_data      = prd;
    tc->cur_frame->mark_special_return_data = mark_parameterize_sr_data;
    MVM_args_setup_thunk(tc, result, MVM_RETURN_OBJ, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_TWO_OBJ));
    tc->cur_frame->args[0].o = st->WHAT;
    tc->cur_frame->args[1].o = params;
    STABLE(code)->invoke(tc, code, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_TWO_OBJ), tc->cur_frame->args);
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
