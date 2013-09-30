#include "moarvm.h"

/* ***************************************************************************
 * CodePair container configuration: container with FETCH/STORE code refs
 * ***************************************************************************/

typedef struct {
    MVMObject *fetch_code;
    MVMObject *store_code;
} CodePairContData;

/* Dummy, code pair fetch and store arg callsite. */
static MVMCallsiteEntry fetch_arg_flags[] = { MVM_CALLSITE_ARG_OBJ };
static MVMCallsiteEntry store_arg_flags[] = { MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     fetch_arg_callsite = { fetch_arg_flags, 1, 1, 0 };
static MVMCallsite     store_arg_callsite = { store_arg_flags, 2, 2, 0 };

static void code_pair_fetch(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    CodePairContData      *data   = (CodePairContData *)STABLE(cont)->container_data;
    MVMObject             *code   = MVM_frame_find_invokee(tc, data->fetch_code);

    tc->cur_frame->return_value   = res;
    tc->cur_frame->return_type    = MVM_RETURN_OBJ;
    tc->cur_frame->return_address = *(tc->interp_cur_op);
    tc->cur_frame->args[0].o      = cont;

    STABLE(code)->invoke(tc, code, &fetch_arg_callsite, tc->cur_frame->args);
}

static void code_pair_store(MVMThreadContext *tc, MVMObject *cont, MVMObject *obj) {
    CodePairContData      *data   = (CodePairContData *)STABLE(cont)->container_data;
    MVMObject             *code   = MVM_frame_find_invokee(tc, data->store_code);

    tc->cur_frame->return_value   = NULL;
    tc->cur_frame->return_type    = MVM_RETURN_VOID;
    tc->cur_frame->return_address = *(tc->interp_cur_op);
    tc->cur_frame->args[0].o      = cont;
    tc->cur_frame->args[1].o      = obj;

    STABLE(code)->invoke(tc, code, &store_arg_callsite, tc->cur_frame->args);
}

static void code_pair_gc_mark_data(MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist) {
    CodePairContData *data = (CodePairContData *)st->container_data;

    MVM_gc_worklist_add(tc, worklist, &data->fetch_code);
    MVM_gc_worklist_add(tc, worklist, &data->store_code);
}

static void code_pair_gc_free_data(MVMThreadContext *tc, MVMSTable *st) {
    CodePairContData *data = (CodePairContData *)st->container_data;

    if (data) {
        free(data);
        st->container_data = NULL;
    }
}

static void code_pair_serialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    CodePairContData *data = (CodePairContData *)st->container_data;

    writer->write_ref(tc, writer, data->fetch_code);
    writer->write_ref(tc, writer, data->store_code);
}

static void code_pair_deserialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    CodePairContData *data = (CodePairContData *)st->container_data;

    MVM_ASSIGN_REF(tc, st, data->fetch_code, reader->read_ref(tc, reader));
    MVM_ASSIGN_REF(tc, st, data->store_code, reader->read_ref(tc, reader));
}

static const MVMContainerSpec code_pair_spec = {
    "code_pair",
    code_pair_fetch,
    code_pair_store,
    code_pair_store,
    code_pair_gc_mark_data,
    code_pair_gc_free_data,
    code_pair_serialize,
    code_pair_deserialize
};

static void code_pair_set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    CodePairContData *data = malloc(sizeof(CodePairContData));

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

        MVM_ASSIGN_REF(tc, st, data->fetch_code, MVM_repr_at_key_boxed(tc, config, fetch));

        store = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "store");

        if (!MVM_repr_exists_key(tc, config, store))
            MVM_exception_throw_adhoc(tc, "Container spec 'code_pair' must be configured with a store");

        MVM_ASSIGN_REF(tc, st, data->store_code, MVM_repr_at_key_boxed(tc, config, store));
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
        entry = malloc(sizeof(MVMContainerRegistry));
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

#define register_static_container(name) \
    MVM_6model_add_container_config(tc, \
            MVM_string_ascii_decode_nt(tc, tc->instance->VMString, #name), \
            &MVM_CONTAINER_CONF_ ## name)

/* Does initial setup work of the container registry, including registering
 * the various built-in container types. */
void MVM_6model_containers_setup(MVMThreadContext *tc) {
    /* Add built-in configurations. */
    MVM_6model_add_container_config(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "code_pair"), &ContainerConfigurer);

    /* Add containers for C types. */
    register_static_container(CChar);
    register_static_container(CDouble);
    register_static_container(CFloat);
    register_static_container(CFPtr);
    register_static_container(CInt);
    register_static_container(CInt16);
    register_static_container(CInt32);
    register_static_container(CInt64);
    register_static_container(CInt8);
    register_static_container(CIntMax);
    register_static_container(CIntPtr);
    register_static_container(CLDouble);
    register_static_container(CLLong);
    register_static_container(CLong);
    register_static_container(CShort);
    register_static_container(CUChar);
    register_static_container(CUInt);
    register_static_container(CUInt16);
    register_static_container(CUInt32);
    register_static_container(CUInt64);
    register_static_container(CUInt8);
    register_static_container(CUIntMax);
    register_static_container(CUIntPtr);
    register_static_container(CULLong);
    register_static_container(CULong);
    register_static_container(CUShort);
    register_static_container(CVoid);
}
