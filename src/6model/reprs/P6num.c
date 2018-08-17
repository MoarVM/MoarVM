#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps P6num_this_repr;

static void mk_storage_spec(MVMThreadContext *tc, MVMuint16 bits, MVMStorageSpec *spec) {
    spec->bits = bits;
    spec->inlineable      = MVM_STORAGE_SPEC_INLINED;
    spec->boxed_primitive = MVM_STORAGE_SPEC_BP_NUM;
    spec->can_box         = MVM_STORAGE_SPEC_CAN_BOX_NUM;
    switch (bits) {
        case 64: spec->align = ALIGNOF(MVMnum64); break;
        case 32: spec->align = ALIGNOF(MVMnum32); break;
        default: spec->align = ALIGNOF(MVMnum64); break;
    }
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &P6num_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVMP6numREPRData *repr_data = (MVMP6numREPRData *)MVM_malloc(sizeof(MVMP6numREPRData));

        repr_data->bits = sizeof(MVMnum64) * 8;
        mk_storage_spec(tc, repr_data->bits, &repr_data->storage_spec);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMP6num);
        st->REPR_data = repr_data;
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;
    MVMP6numBody     *src_body  = (MVMP6numBody *)src;
    MVMP6numBody     *dest_body = (MVMP6numBody *)dest;
    switch (repr_data->bits) {
        case 32: dest_body->value.n32 = src_body->value.n32; break;
        default: dest_body->value.n64 = src_body->value.n64; break;
    }
}

static void set_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMnum64 value) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;
    switch (repr_data->bits) {
        case 32: ((MVMP6numBody *)data)->value.n32 = (MVMnum32)value; break;
        default: ((MVMP6numBody *)data)->value.n64 = value; break;
    }
}

static MVMnum64 get_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;
    switch (repr_data->bits) {
        case 32: return ((MVMP6numBody *)data)->value.n32;
        default: return ((MVMP6numBody *)data)->value.n64;
    }
}

/* Marks the representation data in an STable.*/
static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_free(st->REPR_data);
}

static const MVMStorageSpec default_storage_spec = {
    MVM_STORAGE_SPEC_INLINED,     /* inlineable */
    sizeof(MVMnum64) * 8,         /* bits */
    ALIGNOF(MVMnum64),            /* align */
    MVM_STORAGE_SPEC_BP_NUM,      /* boxed_primitive */
    MVM_STORAGE_SPEC_CAN_BOX_NUM, /* can_box */
    0,                            /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;
    if (repr_data && repr_data->bits)
        return &repr_data->storage_spec;
    return &default_storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;
    MVMStringConsts  str_consts = tc->instance->str_consts;

    MVMObject *info = MVM_repr_at_key_o(tc, info_hash, str_consts.float_str);
    if (!MVM_is_null(tc, info)) {
        MVMObject *bits_o = MVM_repr_at_key_o(tc, info, str_consts.bits);

        if (!MVM_is_null(tc, bits_o)) {
            repr_data->bits = MVM_repr_get_int(tc, bits_o);

            switch (repr_data->bits) {
                case MVM_P6NUM_C_TYPE_FLOAT:      repr_data->bits = 8 * sizeof(float);       break;
                case MVM_P6NUM_C_TYPE_DOUBLE:     repr_data->bits = 8 * sizeof(double);      break;
                case MVM_P6NUM_C_TYPE_LONGDOUBLE: repr_data->bits = 8 * sizeof(long double); break;
            }

            if (repr_data->bits != 32 && repr_data->bits != 64)
                MVM_exception_throw_adhoc(tc, "MVMP6num: Unsupported num size (%dbit)", repr_data->bits);
        }
    }
    if (repr_data->bits)
        mk_storage_spec(tc, repr_data->bits, &repr_data->storage_spec);
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMP6num);
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;
    MVM_serialization_write_int(tc, writer, repr_data->bits);
}

/* Deserializes representation data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)MVM_malloc(sizeof(MVMP6numREPRData));


    repr_data->bits        = MVM_serialization_read_int(tc, reader);

    if (repr_data->bits !=  1 && repr_data->bits !=  2 && repr_data->bits !=  4 && repr_data->bits != 8
     && repr_data->bits != 16 && repr_data->bits != 32 && repr_data->bits != 64)
        MVM_exception_throw_adhoc(tc, "MVMP6num: Unsupported int size (%dbit)", repr_data->bits);

    if (repr_data->bits)
        mk_storage_spec(tc, repr_data->bits, &repr_data->storage_spec);
    st->REPR_data = repr_data;
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMnum64 value = MVM_serialization_read_num(tc, reader);
    set_num(tc, st, root, data, value);
}

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVM_serialization_write_num(tc, writer, get_num(tc, st, NULL, data));
}

static void spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;
    switch (ins->info->opcode) {
        case MVM_OP_box_n: {
            if (repr_data->bits == 64 && !(st->mode_flags & MVM_FINALIZE_TYPE)) {
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
                ins->info = MVM_op_get_op(MVM_OP_sp_bind_n);
                ins->operands[2] = ins->operands[1];
                ins->operands[1].lit_i16 = offsetof(MVMP6num, body.value);
                MVM_spesh_usages_add_by_reg(tc, g, ins->operands[0], ins);
            }
            break;
        }
        case MVM_OP_unbox_n:
        case MVM_OP_decont_n: {
            if (repr_data->bits == 64 || !repr_data->bits) {
                /* Lower into a direct memory read. */
                MVMSpeshOperand *orig_operands = ins->operands;
                ins->info = MVM_op_get_op(MVM_OP_sp_get_n);
                ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                ins->operands[0] = orig_operands[0];
                ins->operands[1] = orig_operands[1];
                ins->operands[2].lit_i16 = offsetof(MVMP6num, body.value);
            }
            break;
        }
    }
}

/* Initializes the representation. */
const MVMREPROps * MVMP6num_initialize(MVMThreadContext *tc) {
    return &P6num_this_repr;
}

static const MVMREPROps P6num_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        MVM_REPR_DEFAULT_SET_INT,
        MVM_REPR_DEFAULT_GET_INT,
        set_num,
        get_num,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
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
    NULL, /* gc_mark */
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    gc_free_repr_data,
    compose,
    spesh,
    "P6num", /* name */
    MVM_REPR_ID_P6num,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
