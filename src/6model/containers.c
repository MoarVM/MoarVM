#include "moarvm.h"

/* ***************************************************************************
 * CodePair container configuration: container with FETCH/STORE code refs
 * ***************************************************************************/

typedef struct {
    MVMObject *fetch_code;
    MVMObject *store_code;
} CodePairContData;

static MVMObject * code_pair_fetch(MVMThreadContext *tc, MVMCallsite *callsite, MVMObject *cont) {
    CodePairContData *data = (CodePairContData *)STABLE(cont)->container_data;
}

static void code_pair_store(MVMThreadContext *tc, MVMCallsite *callsite, MVMObject *cont, MVMObject *value) {
    CodePairContData *data = (CodePairContData *)STABLE(cont)->container_data;
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
        data = NULL;
    }
}

static void code_pair_serialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    CodePairContData *data = (CodePairContData *)st->container_data;
    writer->write_ref(tc, writer, data->fetch_code);
    writer->write_ref(tc, writer, data->store_code);
}
    
static void code_pair_deserialize(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    CodePairContData *data = (CodePairContData *)st->container_data;
    data->fetch_code = reader->read_ref(tc, reader);
    data->store_code = reader->read_ref(tc, reader);
}

static MVMContainerSpec *code_pair_spec = NULL;

static void code_pair_set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    CodePairContData *data = malloc(sizeof(CodePairContData));
    data->fetch_code = NULL;
    data->store_code = NULL;
    st->container_data = data;
    st->container_spec = code_pair_spec;
}