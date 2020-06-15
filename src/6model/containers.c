#include "moar.h"

MVMint64 MVM_6model_container_iscont_rw(MVMThreadContext *tc, MVMObject *cont) {
    if (cont && IS_CONCRETE(cont)) {
        const MVMContainerSpec *cs = STABLE(cont)->container_spec;
        if (cs && cs->can_store(tc, cont)) {
            return 1;
        }
    }
    return 0;
}

/* ***************************************************************************
 * CodePair container configuration: container with FETCH/STORE code refs
 * ***************************************************************************/

typedef struct {
    MVMObject *fetch_code;
    MVMObject *store_code;
} CodePairContData;

static void code_pair_fetch_internal(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res, MVMReturnType res_type) {
    CodePairContData        *data = (CodePairContData *)STABLE(cont)->container_data;
    MVMObject               *code = MVM_frame_find_invokee(tc, data->fetch_code, NULL);
    MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);
    MVM_args_setup_thunk(tc, res, res_type, inv_arg_callsite);
    tc->cur_frame->args[0].o      = cont;
    STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
}

static void code_pair_fetch(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    code_pair_fetch_internal(tc, cont, res, MVM_RETURN_OBJ);
}

static void code_pair_fetch_i(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    code_pair_fetch_internal(tc, cont, res, MVM_RETURN_INT);
}

static void code_pair_fetch_n(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    code_pair_fetch_internal(tc, cont, res, MVM_RETURN_NUM);
}

static void code_pair_fetch_s(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    code_pair_fetch_internal(tc, cont, res, MVM_RETURN_STR);
}

static void code_pair_store_internal(MVMThreadContext *tc, MVMObject *cont, MVMRegister value, MVMCallsite *cs) {
    CodePairContData         *data = (CodePairContData *)STABLE(cont)->container_data;
    MVMObject                *code = MVM_frame_find_invokee(tc, data->store_code, NULL);
    MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, cs);
    tc->cur_frame->args[0].o       = cont;
    tc->cur_frame->args[1]         = value;
    STABLE(code)->invoke(tc, code, cs, tc->cur_frame->args);
}

static void code_pair_store(MVMThreadContext *tc, MVMObject *cont, MVMObject *obj) {
    MVMRegister r;
    r.o = obj;
    code_pair_store_internal(tc, cont, r, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ));
}

static void code_pair_store_i(MVMThreadContext *tc, MVMObject *cont, MVMint64 value) {
    MVMRegister r;
    r.i64 = value;
    code_pair_store_internal(tc, cont, r, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_INT));
}

static void code_pair_store_n(MVMThreadContext *tc, MVMObject *cont, MVMnum64 value) {
    MVMRegister r;
    r.n64 = value;
    code_pair_store_internal(tc, cont, r, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_NUM));
}

static void code_pair_store_s(MVMThreadContext *tc, MVMObject *cont, MVMString *value) {
    MVMRegister r;
    r.s = value;
    code_pair_store_internal(tc, cont, r, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_STR));
}

static void code_pair_gc_mark_data(MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist) {
    CodePairContData *data = (CodePairContData *)st->container_data;

    MVM_gc_worklist_add(tc, worklist, &data->fetch_code);
    MVM_gc_worklist_add(tc, worklist, &data->store_code);
}

static void code_pair_gc_free_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_free_null(st->container_data);
}

static void code_pair_serialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    CodePairContData *data = (CodePairContData *)st->container_data;

    MVM_serialization_write_ref(tc, writer, data->fetch_code);
    MVM_serialization_write_ref(tc, writer, data->store_code);
}

static void code_pair_deserialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    CodePairContData *data = (CodePairContData *)st->container_data;

    MVM_ASSIGN_REF(tc, &(st->header), data->fetch_code, MVM_serialization_read_ref(tc, reader));
    MVM_ASSIGN_REF(tc, &(st->header), data->store_code, MVM_serialization_read_ref(tc, reader));
}

static MVMint32 code_pair_can_store(MVMThreadContext *tc, MVMObject *cont) {
    return 1;
}

static const MVMContainerSpec code_pair_spec = {
    "code_pair",
    code_pair_fetch,
    code_pair_fetch_i,
    code_pair_fetch_n,
    code_pair_fetch_s,
    code_pair_store,
    code_pair_store_i,
    code_pair_store_n,
    code_pair_store_s,
    code_pair_store,
    NULL, /* spesh */
    code_pair_gc_mark_data,
    code_pair_gc_free_data,
    code_pair_serialize,
    code_pair_deserialize,
    NULL,
    code_pair_can_store,
    NULL, /* cas */
    NULL, /* atomic_load */
    NULL, /* atomic_store */
    0
};

static void code_pair_set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    CodePairContData *data = MVM_malloc(sizeof(CodePairContData));

    data->fetch_code   = NULL;
    data->store_code   = NULL;
    st->container_data = data;
    st->container_spec = &code_pair_spec;
}

static void code_pair_configure_container_spec(MVMThreadContext *tc, MVMSTable *st, MVMObject *config) {
    CodePairContData *data = (CodePairContData *)st->container_data;

    MVMROOT2(tc, config, st, {
        MVMString *fetch = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "fetch");
        MVMString *store;

        if (!MVM_repr_exists_key(tc, config, fetch))
            MVM_exception_throw_adhoc(tc, "Container spec 'code_pair' must be configured with a fetch");

        MVM_ASSIGN_REF(tc, &(st->header), data->fetch_code, MVM_repr_at_key_o(tc, config, fetch));

        store = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "store");

        if (!MVM_repr_exists_key(tc, config, store))
            MVM_exception_throw_adhoc(tc, "Container spec 'code_pair' must be configured with a store");

        MVM_ASSIGN_REF(tc, &(st->header), data->store_code, MVM_repr_at_key_o(tc, config, store));
    });
}

static const MVMContainerConfigurer CodePairContainerConfigurer = {
    code_pair_set_container_spec,
    code_pair_configure_container_spec
};

/* ***************************************************************************
 * Value and descriptor container configuration: container is a P6opaque with
 * an attribute designated to hold an (object) value, and other designated to
 * be a descriptor.
 * ***************************************************************************/

/* Registered container operation callbacks. */
typedef struct {
    /* We cache the offsets in the P6opaque of the value and descriptor. */
    size_t value_offset;
    size_t descriptor_offset;

    /* Callbacks. */
    MVMObject *store;
    MVMObject *store_unchecked;
    MVMObject *cas;
    MVMObject *atomic_store;

    /* Retained for serializatin purposes only. */
    MVMObject *attrs_class;
    MVMString *value_attr;
    MVMString *descriptor_attr;
} MVMValueDescContainer;

static void calculate_attr_offsets(MVMThreadContext *tc, MVMSTable *st, MVMValueDescContainer *data) {
    data->value_offset = MVM_p6opaque_attr_offset(tc, st->WHAT, data->attrs_class,
            data->value_attr) + sizeof(MVMObject);
    data->descriptor_offset = MVM_p6opaque_attr_offset(tc, st->WHAT, data->attrs_class,
            data->descriptor_attr) + sizeof(MVMObject);
}

static MVMObject * read_container_value(MVMObject *cont) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)STABLE(cont)->container_data;
    return *((MVMObject **)((char*)cont + data->value_offset));
}

static MVMObject * read_container_descriptor(MVMObject *cont) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)STABLE(cont)->container_data;
    return *((MVMObject **)((char*)cont + data->descriptor_offset));
}

static void value_desc_cont_fetch(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    MVMObject *value = read_container_value(cont);
    res->o = value ? value : tc->instance->VMNull;
}

static void value_desc_cont_fetch_i(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    res->i64 = MVM_repr_get_int(tc, read_container_value(cont));
}

static void value_desc_cont_fetch_n(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    res->n64 = MVM_repr_get_num(tc, read_container_value(cont));
}

static void value_desc_cont_fetch_s(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    res->s = MVM_repr_get_str(tc, read_container_value(cont));
}

static void value_desc_cont_store(MVMThreadContext *tc, MVMObject *cont, MVMObject *value) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)STABLE(cont)->container_data;
    MVMObject *code = MVM_frame_find_invokee(tc, data->store, NULL);
    MVMCallsite *cs = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ);
    MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, cs);
    tc->cur_frame->args[0].o = cont;
    tc->cur_frame->args[1].o = value;
    STABLE(code)->invoke(tc, code, cs, tc->cur_frame->args);
}

static void value_desc_cont_store_i(MVMThreadContext *tc, MVMObject *cont, MVMint64 value) {
    MVMObject *boxed;
    MVMROOT(tc, cont, {
        boxed = MVM_repr_box_int(tc, MVM_hll_current(tc)->int_box_type, value);
    });
    value_desc_cont_store(tc, cont, boxed);
}

static void value_desc_cont_store_n(MVMThreadContext *tc, MVMObject *cont, MVMnum64 value) {
    MVMObject *boxed;
    MVMROOT(tc, cont, {
        boxed = MVM_repr_box_num(tc, MVM_hll_current(tc)->num_box_type, value);
    });
    value_desc_cont_store(tc, cont, boxed);
}

static void value_desc_cont_store_s(MVMThreadContext *tc, MVMObject *cont, MVMString *value) {
    MVMObject *boxed;
    MVMROOT(tc, cont, {
        boxed = MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type, value);
    });
    value_desc_cont_store(tc, cont, boxed);
}

static void value_desc_cont_store_unchecked(MVMThreadContext *tc, MVMObject *cont, MVMObject *value) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)STABLE(cont)->container_data;
    MVMObject *code = MVM_frame_find_invokee(tc, data->store_unchecked, NULL);
    MVMCallsite *cs = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ);
    MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, cs);
    tc->cur_frame->args[0].o = cont;
    tc->cur_frame->args[1].o = value;
    STABLE(code)->invoke(tc, code, cs, tc->cur_frame->args);
}

static void value_desc_cont_gc_mark_data(MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)st->container_data;
    MVM_gc_worklist_add(tc, worklist, &data->store);
    MVM_gc_worklist_add(tc, worklist, &data->store_unchecked);
    MVM_gc_worklist_add(tc, worklist, &data->cas);
    MVM_gc_worklist_add(tc, worklist, &data->atomic_store);
    MVM_gc_worklist_add(tc, worklist, &data->attrs_class);
    MVM_gc_worklist_add(tc, worklist, &data->value_attr);
    MVM_gc_worklist_add(tc, worklist, &data->descriptor_attr);
}

static void value_desc_cont_gc_free_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_free_null(st->container_data);
}

static void value_desc_cont_serialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)st->container_data;
    MVM_serialization_write_ref(tc, writer, data->store);
    MVM_serialization_write_ref(tc, writer, data->store_unchecked);
    MVM_serialization_write_ref(tc, writer, data->cas);
    MVM_serialization_write_ref(tc, writer, data->atomic_store);
    MVM_serialization_write_ref(tc, writer, data->attrs_class);
    MVM_serialization_write_str(tc, writer, data->value_attr);
    MVM_serialization_write_str(tc, writer, data->descriptor_attr);
}

static void value_desc_cont_deserialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)st->container_data;
    MVM_ASSIGN_REF(tc, &(st->header), data->store, MVM_serialization_read_ref(tc, reader));
    MVM_ASSIGN_REF(tc, &(st->header), data->store_unchecked, MVM_serialization_read_ref(tc, reader));
    MVM_ASSIGN_REF(tc, &(st->header), data->cas, MVM_serialization_read_ref(tc, reader));
    MVM_ASSIGN_REF(tc, &(st->header), data->atomic_store, MVM_serialization_read_ref(tc, reader));
    MVM_ASSIGN_REF(tc, &(st->header), data->attrs_class, MVM_serialization_read_ref(tc, reader));
    MVM_ASSIGN_REF(tc, &(st->header), data->value_attr, MVM_serialization_read_str(tc, reader));
    MVM_ASSIGN_REF(tc, &(st->header), data->descriptor_attr, MVM_serialization_read_str(tc, reader));
}


static void value_desc_cont_post_deserialize(MVMThreadContext *tc, MVMSTable *st) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)st->container_data;
    calculate_attr_offsets(tc, st, data);
}

static void value_desc_cont_spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)st->container_data;
    switch (ins->info->opcode) {
    case MVM_OP_decont: {
        MVMSpeshOperand *old_operands = ins->operands;
        ins->info = MVM_op_get_op(MVM_OP_sp_get_o);
        ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
        ins->operands[0] = old_operands[0];
        ins->operands[1] = old_operands[1];
        ins->operands[2].lit_i16 = (MVMuint16)data->value_offset;
        break;
        }
    default: break;
    }
}

static MVMint32 value_desc_cont_can_store(MVMThreadContext *tc, MVMObject *cont) {
    return !MVM_is_null(tc, read_container_descriptor(cont));
}

static void value_desc_cont_cas(MVMThreadContext *tc, MVMObject *cont,
                              MVMObject *expected, MVMObject *value,
                              MVMRegister *result) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)STABLE(cont)->container_data;
    MVMObject *code = MVM_frame_find_invokee(tc, data->cas, NULL);
    MVMCallsite *cs = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ_OBJ);
    MVM_args_setup_thunk(tc, result, MVM_RETURN_OBJ, cs);
    tc->cur_frame->args[0].o = cont;
    tc->cur_frame->args[1].o = expected;
    tc->cur_frame->args[2].o = value;
    STABLE(code)->invoke(tc, code, cs, tc->cur_frame->args);
}

static MVMObject * value_desc_cont_atomic_load(MVMThreadContext *tc, MVMObject *cont) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)STABLE(cont)->container_data;
    MVMObject *value = (MVMObject *)MVM_load(((char*)cont + data->value_offset));
    return value ? value : tc->instance->VMNull;
}

void value_desc_cont_atomic_store(MVMThreadContext *tc, MVMObject *cont, MVMObject *value) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)STABLE(cont)->container_data;
    MVMObject *code = MVM_frame_find_invokee(tc, data->atomic_store, NULL);
    MVMCallsite *cs = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ);
    MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, cs);
    tc->cur_frame->args[0].o = cont;
    tc->cur_frame->args[1].o = value;
    STABLE(code)->invoke(tc, code, cs, tc->cur_frame->args);
}

static const MVMContainerSpec value_desc_cont_spec = {
    "value_desc_cont",
    value_desc_cont_fetch,
    value_desc_cont_fetch_i,
    value_desc_cont_fetch_n,
    value_desc_cont_fetch_s,
    value_desc_cont_store,
    value_desc_cont_store_i,
    value_desc_cont_store_n,
    value_desc_cont_store_s,
    value_desc_cont_store_unchecked,
    value_desc_cont_spesh,
    value_desc_cont_gc_mark_data,
    value_desc_cont_gc_free_data,
    value_desc_cont_serialize,
    value_desc_cont_deserialize,
    value_desc_cont_post_deserialize,
    value_desc_cont_can_store,
    value_desc_cont_cas,
    value_desc_cont_atomic_load,
    value_desc_cont_atomic_store,
    1
};

static void value_desc_cont_set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    if (st->container_data)
        value_desc_cont_gc_free_data(tc, st);
    MVMValueDescContainer *data = MVM_calloc(1, sizeof(MVMValueDescContainer));
    st->container_data = data;
    st->container_spec = &value_desc_cont_spec;
}

static MVMObject * grab_one_value(MVMThreadContext *tc, MVMObject *config, const char *key) {
    MVMString *key_str;
    MVMROOT(tc, config, {
        key_str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, key);
    });
    if (!MVM_repr_exists_key(tc, config, key_str))
        MVM_exception_throw_adhoc(tc, "Container spec must be configured with a '%s'", key);
    return MVM_repr_at_key_o(tc, config, key_str);
}
static void value_desc_cont_configure_container_spec(MVMThreadContext *tc, MVMSTable *st, MVMObject *config) {
    MVMValueDescContainer *data = (MVMValueDescContainer *)st->container_data;
    MVMROOT2(tc, st, config, {
        MVMObject *value;
        value = grab_one_value(tc, config, "store");
        MVM_ASSIGN_REF(tc, &(st->header), data->store, value);
        value = grab_one_value(tc, config, "store_unchecked");
        MVM_ASSIGN_REF(tc, &(st->header), data->store_unchecked, value);
        value = grab_one_value(tc, config, "cas");
        MVM_ASSIGN_REF(tc, &(st->header), data->cas, value);
        value = grab_one_value(tc, config, "atomic_store");
        MVM_ASSIGN_REF(tc, &(st->header), data->atomic_store, value);
        value = grab_one_value(tc, config, "attrs_class");
        MVM_ASSIGN_REF(tc, &(st->header), data->attrs_class, value);
        value = grab_one_value(tc, config, "value_attr");
        MVM_ASSIGN_REF(tc, &(st->header), data->value_attr, MVM_repr_get_str(tc, value));
        value = grab_one_value(tc, config, "descriptor_attr");
        MVM_ASSIGN_REF(tc, &(st->header), data->descriptor_attr, MVM_repr_get_str(tc, value));
    });
    calculate_attr_offsets(tc, st, data);
}

static const MVMContainerConfigurer ValueDescContainerConfigurer = {
    value_desc_cont_set_container_spec,
    value_desc_cont_configure_container_spec
};

/* ***************************************************************************
 * Native reference container configuration
 * ***************************************************************************/

static void native_ref_fetch_i(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(cont)->REPR_data;
    if (repr_data->primitive_type != MVM_STORAGE_SPEC_BP_INT)
        MVM_exception_throw_adhoc(tc, "This container does not reference a native integer");
    switch (repr_data->ref_kind) {
        case MVM_NATIVEREF_LEX:
            res->i64 = MVM_nativeref_read_lex_i(tc, cont);
            break;
        case MVM_NATIVEREF_ATTRIBUTE:
            res->i64 = MVM_nativeref_read_attribute_i(tc, cont);
            break;
        case MVM_NATIVEREF_POSITIONAL:
            res->i64 = MVM_nativeref_read_positional_i(tc, cont);
            break;
        case MVM_NATIVEREF_MULTIDIM:
            res->i64 = MVM_nativeref_read_multidim_i(tc, cont);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown native int reference kind");
    }
}

static void native_ref_fetch_n(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(cont)->REPR_data;
    if (repr_data->primitive_type != MVM_STORAGE_SPEC_BP_NUM)
        MVM_exception_throw_adhoc(tc, "This container does not reference a native number");
    switch (repr_data->ref_kind) {
        case MVM_NATIVEREF_LEX:
            res->n64 = MVM_nativeref_read_lex_n(tc, cont);
            break;
        case MVM_NATIVEREF_ATTRIBUTE:
            res->n64 = MVM_nativeref_read_attribute_n(tc, cont);
            break;
        case MVM_NATIVEREF_POSITIONAL:
            res->n64 = MVM_nativeref_read_positional_n(tc, cont);
            break;
        case MVM_NATIVEREF_MULTIDIM:
            res->n64 = MVM_nativeref_read_multidim_n(tc, cont);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown native num reference kind");
    }
}

static void native_ref_fetch_s(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(cont)->REPR_data;
    if (repr_data->primitive_type != MVM_STORAGE_SPEC_BP_STR)
        MVM_exception_throw_adhoc(tc, "This container does not reference a native string");
    switch (repr_data->ref_kind) {
        case MVM_NATIVEREF_LEX:
            res->s = MVM_nativeref_read_lex_s(tc, cont);
            break;
        case MVM_NATIVEREF_ATTRIBUTE:
            res->s = MVM_nativeref_read_attribute_s(tc, cont);
            break;
        case MVM_NATIVEREF_POSITIONAL:
            res->s = MVM_nativeref_read_positional_s(tc, cont);
            break;
        case MVM_NATIVEREF_MULTIDIM:
            res->s = MVM_nativeref_read_multidim_s(tc, cont);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown native str reference kind");
    }
}

static void native_ref_fetch(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(cont)->REPR_data;
    MVMHLLConfig         *hll       = STABLE(cont)->hll_owner;
    MVMRegister           tmp;
    if (!hll)
        hll = MVM_hll_current(tc);
    switch (repr_data->primitive_type) {
        case MVM_STORAGE_SPEC_BP_INT:
            native_ref_fetch_i(tc, cont, &tmp);
            res->o = MVM_repr_box_int(tc, hll->int_box_type, tmp.i64);
            break;
        case MVM_STORAGE_SPEC_BP_NUM:
            native_ref_fetch_n(tc, cont, &tmp);
            res->o = MVM_repr_box_num(tc, hll->num_box_type, tmp.n64);
            break;
        case MVM_STORAGE_SPEC_BP_STR:
            native_ref_fetch_s(tc, cont, &tmp);
            res->o = MVM_repr_box_str(tc, hll->str_box_type, tmp.s);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown native reference primitive type");
    }
}

static void native_ref_store_i(MVMThreadContext *tc, MVMObject *cont, MVMint64 value) {
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(cont)->REPR_data;
    if (repr_data->primitive_type != MVM_STORAGE_SPEC_BP_INT)
        MVM_exception_throw_adhoc(tc, "This container does not reference a native integer");
    switch (repr_data->ref_kind) {
        case MVM_NATIVEREF_LEX:
            MVM_nativeref_write_lex_i(tc, cont, value);
            break;
        case MVM_NATIVEREF_ATTRIBUTE:
            MVM_nativeref_write_attribute_i(tc, cont, value);
            break;
        case MVM_NATIVEREF_POSITIONAL:
            MVM_nativeref_write_positional_i(tc, cont, value);
            break;
        case MVM_NATIVEREF_MULTIDIM:
            MVM_nativeref_write_multidim_i(tc, cont, value);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown native int reference kind");
    }
}

static void native_ref_store_n(MVMThreadContext *tc, MVMObject *cont, MVMnum64 value) {
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(cont)->REPR_data;
    if (repr_data->primitive_type != MVM_STORAGE_SPEC_BP_NUM)
        MVM_exception_throw_adhoc(tc, "This container does not reference a native number");
    switch (repr_data->ref_kind) {
        case MVM_NATIVEREF_LEX:
            MVM_nativeref_write_lex_n(tc, cont, value);
            break;
        case MVM_NATIVEREF_ATTRIBUTE:
            MVM_nativeref_write_attribute_n(tc, cont, value);
            break;
        case MVM_NATIVEREF_POSITIONAL:
            MVM_nativeref_write_positional_n(tc, cont, value);
            break;
        case MVM_NATIVEREF_MULTIDIM:
            MVM_nativeref_write_multidim_n(tc, cont, value);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown native num reference kind");
    }
}

static void native_ref_store_s(MVMThreadContext *tc, MVMObject *cont, MVMString *value) {
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(cont)->REPR_data;
    if (repr_data->primitive_type != MVM_STORAGE_SPEC_BP_STR)
        MVM_exception_throw_adhoc(tc, "This container does not reference a native string");
    switch (repr_data->ref_kind) {
        case MVM_NATIVEREF_LEX:
            MVM_nativeref_write_lex_s(tc, cont, value);
            break;
        case MVM_NATIVEREF_ATTRIBUTE:
            MVM_nativeref_write_attribute_s(tc, cont, value);
            break;
        case MVM_NATIVEREF_POSITIONAL:
            MVM_nativeref_write_positional_s(tc, cont, value);
            break;
        case MVM_NATIVEREF_MULTIDIM:
            MVM_nativeref_write_multidim_s(tc, cont, value);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown native str reference kind");
    }
}

static void native_ref_store(MVMThreadContext *tc, MVMObject *cont, MVMObject *obj) {
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(cont)->REPR_data;
    switch (repr_data->primitive_type) {
        case MVM_STORAGE_SPEC_BP_INT:
            native_ref_store_i(tc, cont, MVM_repr_get_int(tc, obj));
            break;
        case MVM_STORAGE_SPEC_BP_NUM:
            native_ref_store_n(tc, cont, MVM_repr_get_num(tc, obj));
            break;
        case MVM_STORAGE_SPEC_BP_STR:
            native_ref_store_s(tc, cont, MVM_repr_get_str(tc, obj));
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown native reference primitive type");
    }
}

static void native_ref_serialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    /* Nothing to do. */
}

static void native_ref_deserialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    /* Nothing to do. */
}

static MVMint32 native_ref_can_store(MVMThreadContext *tc, MVMObject *cont) {
    return 1;
}

static const MVMContainerSpec native_ref_spec = {
    "native_ref",
    native_ref_fetch,
    native_ref_fetch_i,
    native_ref_fetch_n,
    native_ref_fetch_s,
    native_ref_store,
    native_ref_store_i,
    native_ref_store_n,
    native_ref_store_s,
    native_ref_store,
    NULL, /* spesh */
    NULL, /* gc_mark_data */
    NULL, /* gc_free_data */
    native_ref_serialize,
    native_ref_deserialize,
    NULL,
    native_ref_can_store,
    NULL, /* cas */
    NULL, /* atomic_load */
    NULL, /* atomic_store */
    1
};

static void native_ref_set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    st->container_spec = &native_ref_spec;
}

static void native_ref_configure_container_spec(MVMThreadContext *tc, MVMSTable *st, MVMObject *config) {
    /* Nothing to do. */
}

void *MVM_container_devirtualize_fetch_for_jit(MVMThreadContext *tc, MVMSTable *st, MVMuint16 type) {
    if (type != MVM_reg_int64)
        return NULL;
    if (st->container_spec == &native_ref_spec) {
        MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)st->REPR_data;
        switch (repr_data->ref_kind) {
            case MVM_NATIVEREF_LEX:
                switch (type) {
                    case MVM_reg_int64: return MVM_nativeref_read_lex_i;
                    case MVM_reg_num64: return MVM_nativeref_read_lex_n;
                    case MVM_reg_str:   return MVM_nativeref_read_lex_s;
                }
                break;
            case MVM_NATIVEREF_ATTRIBUTE:
                switch (type) {
                    case MVM_reg_int64: return MVM_nativeref_read_attribute_i;
                    case MVM_reg_num64: return MVM_nativeref_read_attribute_n;
                    case MVM_reg_str:   return MVM_nativeref_read_attribute_s;
                }
                break;
            case MVM_NATIVEREF_POSITIONAL:
                switch (type) {
                    case MVM_reg_int64: return MVM_nativeref_read_positional_i;
                    case MVM_reg_num64: return MVM_nativeref_read_positional_n;
                    case MVM_reg_str:   return MVM_nativeref_read_positional_s;
                }
                break;
            case MVM_NATIVEREF_MULTIDIM:
                switch (type) {
                    case MVM_reg_int64: return MVM_nativeref_read_multidim_i;
                    case MVM_reg_num64: return MVM_nativeref_read_multidim_n;
                    case MVM_reg_str:   return MVM_nativeref_read_multidim_s;
                }
                break;
            default:
                return NULL;
        }
    }
    return NULL;
}

void *MVM_container_devirtualize_store_for_jit(MVMThreadContext *tc, MVMSTable *st, MVMuint16 type) {
    if (type != MVM_reg_int64)
        return NULL;
    if (st->container_spec == &native_ref_spec) {
        MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)st->REPR_data;
        switch (repr_data->ref_kind) {
            case MVM_NATIVEREF_LEX:
                switch (type) {
                    case MVM_reg_int64: return MVM_nativeref_write_lex_i;
                    case MVM_reg_num64: return MVM_nativeref_write_lex_n;
                    case MVM_reg_str:   return MVM_nativeref_write_lex_s;
                }
                break;
            case MVM_NATIVEREF_ATTRIBUTE:
                switch (type) {
                    case MVM_reg_int64: return MVM_nativeref_write_attribute_i;
                    case MVM_reg_num64: return MVM_nativeref_write_attribute_n;
                    case MVM_reg_str:   return MVM_nativeref_write_attribute_s;
                }
                break;
            case MVM_NATIVEREF_POSITIONAL:
                switch (type) {
                    case MVM_reg_int64: return MVM_nativeref_write_positional_i;
                    case MVM_reg_num64: return MVM_nativeref_write_positional_n;
                    case MVM_reg_str:   return MVM_nativeref_write_positional_s;
                }
                break;
            case MVM_NATIVEREF_MULTIDIM:
                switch (type) {
                    case MVM_reg_int64: return MVM_nativeref_write_multidim_i;
                    case MVM_reg_num64: return MVM_nativeref_write_multidim_n;
                    case MVM_reg_str:   return MVM_nativeref_write_multidim_s;
                }
                break;
            default:
                return NULL;
        }
    }
    return NULL;
}

static const MVMContainerConfigurer NativeRefContainerConfigurer = {
    native_ref_set_container_spec,
    native_ref_configure_container_spec
};

/* ***************************************************************************
 * Container registry and configuration
 * ***************************************************************************/

/* Adds a container configurer to the registry. */
void MVM_6model_add_container_config(MVMThreadContext *tc, MVMString *name,
        const MVMContainerConfigurer *configurer) {

    if (!MVM_str_hash_key_is_valid(tc, name)) {
        MVM_str_hash_key_throw_invalid(tc, name);
    }

    uv_mutex_lock(&tc->instance->mutex_container_registry);

    MVMContainerRegistry *entry = MVM_str_hash_lvalue_fetch_nocheck(tc, &tc->instance->container_registry, name);

    if (!entry->hash_handle.key) {
        entry->configurer      = configurer;
        entry->hash_handle.key = name;
    }

    uv_mutex_unlock(&tc->instance->mutex_container_registry);
}

/* Gets a container configurer from the registry. */
const MVMContainerConfigurer * MVM_6model_get_container_config(MVMThreadContext *tc, MVMString *name) {

    if (!MVM_str_hash_key_is_valid(tc, name)) {
        MVM_str_hash_key_throw_invalid(tc, name);
    }

    uv_mutex_lock(&tc->instance->mutex_container_registry);
    MVMContainerRegistry *entry = MVM_str_hash_fetch_nocheck(tc, &tc->instance->container_registry, name);
    uv_mutex_unlock(&tc->instance->mutex_container_registry);
    return entry ? entry->configurer : NULL;
}

/* Does initial setup work of the container registry, including registering
 * the various built-in container types. */
void MVM_6model_containers_setup(MVMThreadContext *tc) {
    /* Add built-in configurations. */
    MVM_6model_add_container_config(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "code_pair"), &CodePairContainerConfigurer);
    MVM_6model_add_container_config(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "native_ref"), &NativeRefContainerConfigurer);
    MVM_6model_add_container_config(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "value_desc_cont"), &ValueDescContainerConfigurer);
}

/* ***************************************************************************
 * Native container/reference operations
 * ***************************************************************************/

/* Check if this is a container referencing a given native. */
static MVMint64 get_container_primitive(MVMThreadContext *tc, MVMObject *cont) {
    if (cont && IS_CONCRETE(cont)) {
        const MVMContainerSpec *cs = STABLE(cont)->container_spec;
        if (cs == &native_ref_spec && REPR(cont)->ID == MVM_REPR_ID_NativeRef)
            return ((MVMNativeRefREPRData *)STABLE(cont)->REPR_data)->primitive_type;
    }
    return MVM_STORAGE_SPEC_BP_NONE;
}
MVMint64 MVM_6model_container_iscont_i(MVMThreadContext *tc, MVMObject *cont) {
    return get_container_primitive(tc, cont) == MVM_STORAGE_SPEC_BP_INT;
}
MVMint64 MVM_6model_container_iscont_n(MVMThreadContext *tc, MVMObject *cont) {
    return get_container_primitive(tc, cont) == MVM_STORAGE_SPEC_BP_NUM;
}
MVMint64 MVM_6model_container_iscont_s(MVMThreadContext *tc, MVMObject *cont) {
    return get_container_primitive(tc, cont) == MVM_STORAGE_SPEC_BP_STR;
}

/* If it's a container, do a fetch_i. Otherwise, try to unbox the received
 * value as a native integer. */
void MVM_6model_container_decont_i(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    const MVMContainerSpec *cs = STABLE(cont)->container_spec;
    if (cs && IS_CONCRETE(cont))
        cs->fetch_i(tc, cont, res);
    else
        res->i64 = MVM_repr_get_int(tc, cont);
}

/* If it's a container, do a fetch_n. Otherwise, try to unbox the received
 * value as a native number. */
void MVM_6model_container_decont_n(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    const MVMContainerSpec *cs = STABLE(cont)->container_spec;
    if (cs && IS_CONCRETE(cont))
        cs->fetch_n(tc, cont, res);
    else
        res->n64 = MVM_repr_get_num(tc, cont);
}

/* If it's a container, do a fetch_s. Otherwise, try to unbox the received
 * value as a native string. */
void MVM_6model_container_decont_s(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    const MVMContainerSpec *cs = STABLE(cont)->container_spec;
    if (cs && IS_CONCRETE(cont))
        cs->fetch_s(tc, cont, res);
    else
        res->s = MVM_repr_get_str(tc, cont);
}

/* If it's a container, do a fetch_i. Otherwise, try to unbox the received
 * value as a native unsigned integer. */
void MVM_6model_container_decont_u(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    const MVMContainerSpec *cs = STABLE(cont)->container_spec;
    if (cs && IS_CONCRETE(cont))
        /* XXX We need a fetch_u at some point. */
        cs->fetch_i(tc, cont, res);
    else
        res->u64 = MVM_repr_get_uint(tc, cont);
}

/* Checks we have a container, and provided we do, assigns an int into it. */
void MVM_6model_container_assign_i(MVMThreadContext *tc, MVMObject *cont, MVMint64 value) {
    const MVMContainerSpec *cs = STABLE(cont)->container_spec;
    if (cs && IS_CONCRETE(cont))
        cs->store_i(tc, cont, value);
    else
        MVM_exception_throw_adhoc(tc, "Cannot assign to an immutable value");
}

/* Checks we have a container, and provided we do, assigns a num into it. */
void MVM_6model_container_assign_n(MVMThreadContext *tc, MVMObject *cont, MVMnum64 value) {
    const MVMContainerSpec *cs = STABLE(cont)->container_spec;
    if (cs && IS_CONCRETE(cont))
        cs->store_n(tc, cont, value);
    else
        MVM_exception_throw_adhoc(tc, "Cannot assign to an immutable value");
}

/* Checks we have a container, and provided we do, assigns a str into it. */
void MVM_6model_container_assign_s(MVMThreadContext *tc, MVMObject *cont, MVMString *value) {
    const MVMContainerSpec *cs = STABLE(cont)->container_spec;
    if (cs && IS_CONCRETE(cont))
        cs->store_s(tc, cont, value);
    else
        MVM_exception_throw_adhoc(tc, "Cannot assign to an immutable value");
}

/* ***************************************************************************
 * Container atomic operations
 * ***************************************************************************/

void MVM_6model_container_cas(MVMThreadContext *tc, MVMObject *cont,
                              MVMObject *expected, MVMObject *value,
                              MVMRegister *result) {
    if (IS_CONCRETE(cont)) {
        MVMContainerSpec const *cs = cont->st->container_spec;
        if (cs) {
            if (cs->cas)
                cs->cas(tc, cont, expected, value, result);
            else
                MVM_exception_throw_adhoc(tc,
                    "A %s container does not know how to do atomic compare and swap",
                     MVM_6model_get_stable_debug_name(tc, cont->st));
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Cannot perform atomic compare and swap on non-container value of type %s",
                 MVM_6model_get_stable_debug_name(tc, cont->st));
        }
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Cannot perform atomic compare and swap on %s type object",
             MVM_6model_get_stable_debug_name(tc, cont->st));
    }
}

MVMObject * MVM_6model_container_atomic_load(MVMThreadContext *tc, MVMObject *cont) {
    if (IS_CONCRETE(cont)) {
        MVMContainerSpec const *cs = cont->st->container_spec;
        if (cs) {
            if (cs->atomic_load)
                return cs->atomic_load(tc, cont);
            else
                MVM_exception_throw_adhoc(tc,
                    "A %s container does not know how to do an atomic load",
                     MVM_6model_get_stable_debug_name(tc, cont->st));
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Cannot perform atomic load from a non-container value of type %s",
                 MVM_6model_get_stable_debug_name(tc, cont->st));
        }
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Cannot perform atomic load from %s type object",
             MVM_6model_get_stable_debug_name(tc, cont->st));
    }
}

void MVM_6model_container_atomic_store(MVMThreadContext *tc, MVMObject *cont, MVMObject *value) {
    if (IS_CONCRETE(cont)) {
        MVMContainerSpec const *cs = cont->st->container_spec;
        if (cs) {
            if (cs->atomic_store)
                cs->atomic_store(tc, cont, value);
            else
                MVM_exception_throw_adhoc(tc,
                    "A %s container does not know how to do an atomic store",
                     MVM_6model_get_stable_debug_name(tc, cont->st));
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Cannot perform atomic store to a non-container value of type %s",
                 MVM_6model_get_stable_debug_name(tc, cont->st));
        }
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Cannot perform atomic store to %s type object",
             MVM_6model_get_stable_debug_name(tc, cont->st));
    }
}

static AO_t * native_ref_as_atomic_i(MVMThreadContext *tc, MVMObject *cont) {
    if (REPR(cont)->ID == MVM_REPR_ID_NativeRef && IS_CONCRETE(cont)) {
        MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(cont)->REPR_data;
        if (repr_data->primitive_type == MVM_STORAGE_SPEC_BP_INT) {
            switch (repr_data->ref_kind) {
                case MVM_NATIVEREF_LEX:
                    return MVM_nativeref_as_atomic_lex_i(tc, cont);
                case MVM_NATIVEREF_ATTRIBUTE:
                    return MVM_nativeref_as_atomic_attribute_i(tc, cont);
                case MVM_NATIVEREF_POSITIONAL:
                    return MVM_nativeref_as_atomic_positional_i(tc, cont);
                case MVM_NATIVEREF_MULTIDIM:
                    return MVM_nativeref_as_atomic_multidim_i(tc, cont);
                default:
                    MVM_exception_throw_adhoc(tc, "Unknown native int reference kind");
            }
        }
    }
    MVM_exception_throw_adhoc(tc,
        "Can only do integer atomic operations on a container referencing a native integer");
}

MVMint64 MVM_6model_container_cas_i(MVMThreadContext *tc, MVMObject *cont,
                                    MVMint64 expected, MVMint64 value) {
    return (MVMint64)MVM_cas(native_ref_as_atomic_i(tc, cont), (AO_t)expected, (AO_t)value);
}

MVMint64 MVM_6model_container_atomic_load_i(MVMThreadContext *tc, MVMObject *cont) {
    return (MVMint64)MVM_load(native_ref_as_atomic_i(tc, cont));
}

void MVM_6model_container_atomic_store_i(MVMThreadContext *tc, MVMObject *cont, MVMint64 value) {
    MVM_store(native_ref_as_atomic_i(tc, cont), value);
}

MVMint64 MVM_6model_container_atomic_inc(MVMThreadContext *tc, MVMObject *cont) {
    return (MVMint64)MVM_incr(native_ref_as_atomic_i(tc, cont));
}

MVMint64 MVM_6model_container_atomic_dec(MVMThreadContext *tc, MVMObject *cont) {
    return (MVMint64)MVM_decr(native_ref_as_atomic_i(tc, cont));
}

MVMint64 MVM_6model_container_atomic_add(MVMThreadContext *tc, MVMObject *cont, MVMint64 value) {
    return (MVMint64)MVM_add(native_ref_as_atomic_i(tc, cont), (AO_t)value);
}
