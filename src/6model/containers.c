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
    MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_INV_ARG);
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
    code_pair_store_internal(tc, cont, r, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_TWO_OBJ));
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
    code_pair_can_store,
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

    MVMROOT(tc, config, {
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
    native_ref_can_store,
    1
};

static void native_ref_set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    st->container_spec = &native_ref_spec;
}

static void native_ref_configure_container_spec(MVMThreadContext *tc, MVMSTable *st, MVMObject *config) {
    /* Nothing to do. */
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
    MVMContainerRegistry *entry;

    uv_mutex_lock(&tc->instance->mutex_container_registry);

    MVM_HASH_GET(tc, tc->instance->container_registry, name, entry);

    if (!entry) {
        entry = MVM_malloc(sizeof(MVMContainerRegistry));
        entry->name = name;
        entry->configurer  = configurer;
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->name,
            "Container configuration name");
        MVM_HASH_BIND(tc, tc->instance->container_registry, name, entry);
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->hash_handle.key,
            "Container configuration hash key");
    }

    uv_mutex_unlock(&tc->instance->mutex_container_registry);
}

/* Gets a container configurer from the registry. */
const MVMContainerConfigurer * MVM_6model_get_container_config(MVMThreadContext *tc, MVMString *name) {
    MVMContainerRegistry *entry;
    MVM_HASH_GET(tc, tc->instance->container_registry, name, entry);
    return entry != NULL ? entry->configurer : NULL;
}

/* Does initial setup work of the container registry, including registering
 * the various built-in container types. */
void MVM_6model_containers_setup(MVMThreadContext *tc) {
    /* Add built-in configurations. */
    MVM_6model_add_container_config(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "code_pair"), &CodePairContainerConfigurer);
    MVM_6model_add_container_config(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "native_ref"), &NativeRefContainerConfigurer);
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
