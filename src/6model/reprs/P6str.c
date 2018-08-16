#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps P6str_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &P6str_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMP6str);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMP6strBody *src_body  = (MVMP6strBody *)src;
    MVMP6strBody *dest_body = (MVMP6strBody *)dest;
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->value, src_body->value);
}

static void set_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVM_ASSIGN_REF(tc, &(root->header), ((MVMP6strBody *)data)->value, value);
}

static MVMString * get_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    return ((MVMP6strBody *)data)->value;
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_INLINED, /* inlineable */
    sizeof(MVMString*) * 8,   /* bits */
    ALIGNOF(void *),               /* align */
    MVM_STORAGE_SPEC_BP_STR,       /* boxed_primitive */
    MVM_STORAGE_SPEC_CAN_BOX_STR,  /* can_box */
    0,                          /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVM_gc_worklist_add(tc, worklist, &((MVMP6strBody *)data)->value);
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMP6str);
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVM_ASSIGN_REF(tc, &(root->header), ((MVMP6strBody *)data)->value,
        MVM_serialization_read_str(tc, reader));
}

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVM_serialization_write_str(tc, writer, ((MVMP6strBody *)data)->value);
}

static void spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    switch (ins->info->opcode) {
        case MVM_OP_box_s: {
            if (!(st->mode_flags & MVM_FINALIZE_TYPE)) {
                /* Prepend a fastcreate instruction. */
                MVMSpeshIns *fastcreate = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                MVMSpeshFacts *tgt_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                fastcreate->info = MVM_op_get_op(MVM_OP_sp_fastcreate);
                fastcreate->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                fastcreate->operands[0] = ins->operands[0];
                tgt_facts->writer = fastcreate;
                fastcreate->operands[1].lit_i16 = st->size;
                fastcreate->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
                MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, fastcreate);
                tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE;
                tgt_facts->type = st->WHAT;

                /* Change instruction to a bind. */
                MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                ins->info = MVM_op_get_op(MVM_OP_sp_bind_s);
                ins->operands[2] = ins->operands[1];
                ins->operands[1].lit_i16 = offsetof(MVMP6str, body.value);
                MVM_spesh_usages_add_by_reg(tc, g, ins->operands[0], ins);
            }
            break;
        }
    }
}

/* Initializes the representation. */
const MVMREPROps * MVMP6str_initialize(MVMThreadContext *tc) {
    return &P6str_this_repr;
}

static const MVMREPROps P6str_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        MVM_REPR_DEFAULT_SET_INT,
        MVM_REPR_DEFAULT_GET_INT,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        set_str,
        get_str,
        MVM_REPR_DEFAULT_SET_UINT,
        MVM_REPR_DEFAULT_GET_UINT,
        MVM_REPR_DEFAULT_GET_BOXED_REF
    },    /* box_funcs */
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    serialize,
    deserialize, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    deserialize_stable_size,
    gc_mark,
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    spesh,
    "P6str", /* name */
    MVM_REPR_ID_P6str,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
