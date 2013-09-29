#include "moarvm.h"

static const MVMContainerSpecEx spec;

static void set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    if (st->REPR->ID != MVM_REPR_ID_CScalar)
        MVM_exception_throw_adhoc(tc,
                "can only make C scalar objects into CVoid containers");

    st->container_spec = &spec.basic;
    st->container_data = NULL;
}

static void configure_container_spec(MVMThreadContext *tc, MVMSTable *st,
        MVMObject *config) {
    /* noop */
}

const MVMContainerConfigurer MVM_CONTAINER_CONF_CVoid = {
    set_container_spec,
    configure_container_spec
};
