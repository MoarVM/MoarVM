#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps P6str_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &P6str_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject        *obj       = MVM_gc_allocate_type_object(tc, st);
        MVMP6strREPRData *repr_data = MVM_malloc(sizeof(MVMP6strREPRData));
        repr_data->type = MVM_P6STR_C_TYPE_CHAR;

        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMP6str);
        st->REPR_data = repr_data;
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
    MVM_STORAGE_SPEC_INLINED,     /* inlineable */
    sizeof(MVMString *) * 8,      /* bits */
    ALIGNOF(void *),              /* align */
    MVM_STORAGE_SPEC_BP_STR,      /* boxed_primitive */
    MVM_STORAGE_SPEC_CAN_BOX_STR, /* can_box */
    0,                            /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMP6strREPRData *repr_data  = (MVMP6strREPRData *)st->REPR_data;
    MVMStringConsts   str_consts = tc->instance->str_consts;
    MVMObject        *info       = MVM_repr_at_key_o(tc, info_hash, str_consts.string);
    if (!MVM_is_null(tc, info)) {
        MVMObject *chartype_o = MVM_repr_at_key_o(tc, info, str_consts.chartype);
        if (!MVM_is_null(tc, chartype_o)) {
            repr_data->type = MVM_repr_get_int(tc, chartype_o);
        }
        else {
            repr_data->type = MVM_P6STR_C_TYPE_CHAR;
        }
    }
    else {
        repr_data->type = MVM_P6STR_C_TYPE_CHAR;
    }
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

static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMP6strREPRData *repr_data = (MVMP6strREPRData *)st->REPR_data;
    MVM_serialization_write_int(tc, writer, repr_data->type);
}

static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMP6strREPRData *repr_data = MVM_malloc(sizeof(MVMP6strREPRData));

    if (reader->root.version >= 23) {
        repr_data->type = MVM_serialization_read_int(tc, reader);
    }
    else {
        repr_data->type = MVM_P6STR_C_TYPE_CHAR;
    }

    if (repr_data->type != MVM_P6STR_C_TYPE_CHAR && repr_data->type != MVM_P6STR_C_TYPE_WCHAR_T
     && repr_data->type != MVM_P6STR_C_TYPE_CHAR16_T && repr_data->type != MVM_P6STR_C_TYPE_CHAR32_T)
        MVM_exception_throw_adhoc(tc, "P6str: unsupported character type (%d)", repr_data->type);

    st->REPR_data = repr_data;
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

                MVM_spesh_graph_add_comment(tc, g, fastcreate, "%s into a %s",
                        ins->info->name,
                        MVM_6model_get_stable_debug_name(tc, st));

                /* Change instruction to a bind. */
                MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[2], ins);
                ins->info = MVM_op_get_op(MVM_OP_sp_bind_s_nowb);
                ins->operands[2] = ins->operands[1];
                ins->operands[1].lit_i16 = offsetof(MVMP6str, body.value);
                MVM_spesh_usages_add_by_reg(tc, g, ins->operands[0], ins);
            }
            break;
        }
        case MVM_OP_unbox_s:
        case MVM_OP_decont_s: {
            /* Lower into a direct memory read. */
            MVMSpeshOperand *orig_operands = ins->operands;

            MVM_spesh_graph_add_comment(tc, g, ins, "%s from a %s",
                    ins->info->name,
                    MVM_6model_get_stable_debug_name(tc, st));

            ins->info = MVM_op_get_op(MVM_OP_sp_get_s);
            ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0] = orig_operands[0];
            ins->operands[1] = orig_operands[1];
            ins->operands[2].lit_i16 = offsetof(MVMP6str, body.value);
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
    deserialize,
    serialize_repr_data,
    deserialize_repr_data,
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
