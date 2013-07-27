#include "moarvm.h"

/* ***************************************************************************
 * CodePair container configuration: container with FETCH/STORE code refs
 * ***************************************************************************/

typedef struct {
    MVMObject *fetch_code;
    MVMObject *store_code;
} CodePairContData;

typedef struct {
    MVMContainerConfigurer *configurer;

    /* Inline handle to the hash in which this is stored. */
    UT_hash_handle hash_handle;
} ContainerRegistry;

/* Dummy, code pair fetch and store arg callsite. */
static MVMCallsiteEntry fetch_arg_flags[] = { MVM_CALLSITE_ARG_OBJ };
static MVMCallsiteEntry store_arg_flags[] = { MVM_CALLSITE_ARG_OBJ, MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     fetch_arg_callsite = { fetch_arg_flags, 1, 1 };
static MVMCallsite     store_arg_callsite = { store_arg_flags, 2, 2 };

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

static MVMContainerSpec *code_pair_spec = NULL;

static void code_pair_set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    CodePairContData *data = malloc(sizeof(CodePairContData));

    data->fetch_code   = NULL;
    data->store_code   = NULL;
    st->container_data = data;
    st->container_spec = code_pair_spec;
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

static MVMContainerConfigurer * initialize_code_pair_spec(MVMThreadContext *tc) {
    MVMContainerConfigurer *cc      = malloc(sizeof(MVMContainerConfigurer));

    code_pair_spec                  = malloc(sizeof(MVMContainerSpec));
    code_pair_spec->name            = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "code_pair");
    code_pair_spec->fetch           = code_pair_fetch;
    code_pair_spec->store           = code_pair_store;
    code_pair_spec->store_unchecked = code_pair_store;
    code_pair_spec->gc_mark_data    = code_pair_gc_mark_data;
    code_pair_spec->gc_free_data    = code_pair_gc_free_data;
    code_pair_spec->serialize       = code_pair_serialize;
    code_pair_spec->deserialize     = code_pair_deserialize;

    cc->set_container_spec          = code_pair_set_container_spec;
    cc->configure_container_spec    = code_pair_configure_container_spec;

    return cc;
}

/* ***************************************************************************
 * Container registry and configuration
 * ***************************************************************************/

/* Container registry is a hash mapping names of container configurations
 * to function tables. */
static ContainerRegistry *container_registry;

/* Adds a container configurer to the registry. */
void MVM_6model_add_container_config(MVMThreadContext *tc, MVMString *name,
        MVMContainerConfigurer *configurer) {
    void *kdata;
    ContainerRegistry *entry;
    size_t klen;

    MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, name, "add container config needs concrete string");

    if (apr_thread_mutex_lock(tc->instance->mutex_container_registry) != APR_SUCCESS) {
        MVM_exception_throw_adhoc(tc, "Unable to lock container registry hash");
    }

    HASH_FIND(hash_handle, container_registry, kdata, klen, entry);

    if (!entry) {
        entry = malloc(sizeof(ContainerRegistry));
        entry->configurer = configurer;
    }

    HASH_ADD_KEYPTR(hash_handle, container_registry, kdata, klen, entry);

    if (apr_thread_mutex_unlock(tc->instance->mutex_container_registry) != APR_SUCCESS) {
        MVM_exception_throw_adhoc(tc, "Unable to unlock container registry hash");
    }
}

/* Gets a container configurer from the registry. */
MVMContainerConfigurer * MVM_6model_get_container_config(MVMThreadContext *tc, MVMString *name) {
    void *kdata;
    ContainerRegistry *entry;
    size_t klen;

    MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, name, "get container config needs concrete string");

    HASH_FIND(hash_handle, container_registry, kdata, klen, entry);
    return entry != NULL ? entry->configurer : NULL;
}

/* Does initial setup work of the container registry, including registering
 * the various built-in container types. */
void MVM_6model_containers_setup(MVMThreadContext *tc) {
    /* Add built-in configurations. */
    MVM_6model_add_container_config(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "code_pair"),
        initialize_code_pair_spec(tc));
}