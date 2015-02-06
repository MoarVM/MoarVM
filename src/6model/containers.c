#include "moar.h"

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
    CodePairContData *data = (CodePairContData *)st->container_data;

    if (data) {
        MVM_free(data);
        st->container_data = NULL;
    }
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

static const MVMContainerConfigurer ContainerConfigurer = {
    code_pair_set_container_spec,
    code_pair_configure_container_spec
};

/* ***************************************************************************
 * Container registry and configuration
 * ***************************************************************************/

/* Adds a container configurer to the registry. */
void MVM_6model_add_container_config(MVMThreadContext *tc, MVMString *name,
        const MVMContainerConfigurer *configurer) {
    void *kdata;
    MVMContainerRegistry *entry;
    size_t klen;

    MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, name, "add container config needs concrete string");

    uv_mutex_lock(&tc->instance->mutex_container_registry);

    HASH_FIND(hash_handle, tc->instance->container_registry, kdata, klen, entry);

    if (!entry) {
        entry = MVM_malloc(sizeof(MVMContainerRegistry));
        entry->name = name;
        entry->configurer  = configurer;
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->name);
    }

    HASH_ADD_KEYPTR(hash_handle, tc->instance->container_registry, kdata, klen, entry);

    uv_mutex_unlock(&tc->instance->mutex_container_registry);
}

/* Gets a container configurer from the registry. */
const MVMContainerConfigurer * MVM_6model_get_container_config(MVMThreadContext *tc, MVMString *name) {
    void *kdata;
    MVMContainerRegistry *entry;
    size_t klen;

    MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, name, "get container config needs concrete string");

    HASH_FIND(hash_handle, tc->instance->container_registry, kdata, klen, entry);
    return entry != NULL ? entry->configurer : NULL;
}

/* Does initial setup work of the container registry, including registering
 * the various built-in container types. */
void MVM_6model_containers_setup(MVMThreadContext *tc) {
    /* Add built-in configurations. */
    MVM_6model_add_container_config(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "code_pair"), &ContainerConfigurer);
}
