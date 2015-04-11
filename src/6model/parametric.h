void MVM_6model_parametric_setup(MVMThreadContext *tc, MVMObject *type, MVMObject *parameterizer);
void MVM_6model_parametric_parameterize(MVMThreadContext *tc, MVMObject *type, MVMObject *params,
    MVMRegister *result);
MVMObject * MVM_6model_parametric_try_find_parameterization(MVMThreadContext *tc, MVMSTable *st, MVMObject *params);
MVMObject * MVM_6model_parametric_type_parameterized(MVMThreadContext *tc, MVMObject *type);
MVMObject * MVM_6model_parametric_type_parameters(MVMThreadContext *tc, MVMObject *type);
MVMObject * MVM_6model_parametric_type_parameter_at(MVMThreadContext *tc, MVMObject *type, MVMint64 idx);
