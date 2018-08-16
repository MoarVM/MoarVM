#include "moar.h"
#ifdef MVM_BOOL
#include <stdbool.h>
#endif

/* This representation's function pointer table. */
static const MVMREPROps P6int_this_repr;

static void mk_storage_spec(MVMThreadContext *tc, MVMuint16 bits, MVMuint16 is_unsigned, MVMStorageSpec *spec) {
    /* create storage spec */
    spec->inlineable      = MVM_STORAGE_SPEC_INLINED;
    spec->boxed_primitive = MVM_STORAGE_SPEC_BP_INT;
    spec->can_box         = MVM_STORAGE_SPEC_CAN_BOX_INT;
    spec->bits            = bits;
    spec->is_unsigned     = is_unsigned;
    switch (bits) {
    case 64: spec->align = ALIGNOF(MVMint64); break;
    case 32: spec->align = ALIGNOF(MVMint32); break;
    case 16: spec->align = ALIGNOF(MVMint16); break;
    default: spec->align = ALIGNOF(MVMint8);  break;
    }
}


/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &P6int_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVMP6intREPRData *repr_data = (MVMP6intREPRData *)MVM_malloc(sizeof(MVMP6intREPRData));

        repr_data->bits = sizeof(MVMint64) * 8;
        repr_data->is_unsigned = 0;
        mk_storage_spec(tc, repr_data->bits, repr_data->is_unsigned, &repr_data->storage_spec);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMP6int);
        st->REPR_data = repr_data;

    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    MVMP6intBody *src_body  = (MVMP6intBody *)src;
    MVMP6intBody *dest_body = (MVMP6intBody *)dest;
    switch (repr_data->bits) {
        case 64: dest_body->value.i64 = src_body->value.i64; break;
        case 32: dest_body->value.i32 = src_body->value.i32; break;
        case 16: dest_body->value.i16 = src_body->value.i16; break;
        default: dest_body->value.i8 = src_body->value.i8; break;
    }
}

static void set_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 value) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    switch (repr_data->bits) {
        case 64: ((MVMP6intBody *)data)->value.u64 = value; break;
        case 32: ((MVMP6intBody *)data)->value.u32 = (MVMuint32)value; break;
        case 16: ((MVMP6intBody *)data)->value.u16 = (MVMuint16)value; break;
        default: ((MVMP6intBody *)data)->value.u8 = (MVMuint8)value; break;
    }
}

static MVMuint64 get_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    switch (repr_data->bits) {
        case 64: return ((MVMP6intBody *)data)->value.u64;
        case 32: return ((MVMP6intBody *)data)->value.u32;
        case 16: return ((MVMP6intBody *)data)->value.u16;
        default: return ((MVMP6intBody *)data)->value.u8;
    }
}

static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    switch (repr_data->bits) {
        case 64: ((MVMP6intBody *)data)->value.i64 = value; break;
        case 32: ((MVMP6intBody *)data)->value.i32 = (MVMint32)value; break;
        case 16: ((MVMP6intBody *)data)->value.i16 = (MVMint16)value; break;
        default: ((MVMP6intBody *)data)->value.i8 = (MVMint8)value; break;
    }
}

static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    switch (repr_data->bits) {
        case 64: return ((MVMP6intBody *)data)->value.i64;
        case 32: return ((MVMP6intBody *)data)->value.i32;
        case 16: return ((MVMP6intBody *)data)->value.i16;
        default: return ((MVMP6intBody *)data)->value.i8;
    }
}

/* Marks the representation data in an STable.*/
static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_free(st->REPR_data);
}

static const MVMStorageSpec default_storage_spec = {
    MVM_STORAGE_SPEC_INLINED,     /* inlineable */
    sizeof(MVMint64) * 8,         /* bits */
    ALIGNOF(MVMint64),            /* align */
    MVM_STORAGE_SPEC_BP_INT,      /* boxed_primitive */
    MVM_STORAGE_SPEC_CAN_BOX_INT, /* can_box */
    0,                            /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    if (repr_data && repr_data->bits)
        return &repr_data->storage_spec;
    return &default_storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    MVMStringConsts  str_consts = tc->instance->str_consts;

    MVMObject *info = MVM_repr_at_key_o(tc, info_hash, str_consts.integer);
    if (!MVM_is_null(tc, info)) {
        MVMObject *bits_o        = MVM_repr_at_key_o(tc, info, str_consts.bits);
        MVMObject *is_unsigned_o = MVM_repr_at_key_o(tc, info, str_consts.unsigned_str);

        if (!MVM_is_null(tc, bits_o)) {
            repr_data->bits = MVM_repr_get_int(tc, bits_o);

            switch (repr_data->bits) {
                case MVM_P6INT_C_TYPE_CHAR:     repr_data->bits = 8 * sizeof(char);      break;
                case MVM_P6INT_C_TYPE_SHORT:    repr_data->bits = 8 * sizeof(short);     break;
                case MVM_P6INT_C_TYPE_INT:      repr_data->bits = 8 * sizeof(int);       break;
                case MVM_P6INT_C_TYPE_LONG:     repr_data->bits = 8 * sizeof(long);      break;
                case MVM_P6INT_C_TYPE_LONGLONG: repr_data->bits = 8 * sizeof(long long); break;
                case MVM_P6INT_C_TYPE_SIZE_T:   repr_data->bits = 8 * sizeof(size_t);    break;
#ifdef MVM_BOOL
                case MVM_P6INT_C_TYPE_BOOL:     repr_data->bits = 8 * sizeof(MVM_BOOL);  break;
#else
                case MVM_P6INT_C_TYPE_BOOL:     repr_data->bits = 8 * sizeof(char);      break;
#endif
                case MVM_P6INT_C_TYPE_ATOMIC:   repr_data->bits = 8 * sizeof(AO_t);      break;
            }

            if (repr_data->bits !=  1 && repr_data->bits !=  2 && repr_data->bits !=  4 && repr_data->bits != 8
             && repr_data->bits != 16 && repr_data->bits != 32 && repr_data->bits != 64)
                MVM_exception_throw_adhoc(tc, "MVMP6int: Unsupported int size (%dbit)", repr_data->bits);
        } else {
            repr_data->bits = default_storage_spec.bits;
        }

        if (!MVM_is_null(tc, is_unsigned_o)) {
            repr_data->is_unsigned = MVM_repr_get_int(tc, is_unsigned_o);
        }
    }
    if (repr_data->bits)
        mk_storage_spec(tc, repr_data->bits, repr_data->is_unsigned, &repr_data->storage_spec);
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMP6int);
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    MVM_serialization_write_int(tc, writer, repr_data->bits);
    MVM_serialization_write_int(tc, writer, repr_data->is_unsigned);
}

/* Deserializes representation data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)MVM_malloc(sizeof(MVMP6intREPRData));


    repr_data->bits        = MVM_serialization_read_int(tc, reader);
    repr_data->is_unsigned = MVM_serialization_read_int(tc, reader);

    if (repr_data->bits !=  1 && repr_data->bits !=  2 && repr_data->bits !=  4 && repr_data->bits != 8
     && repr_data->bits != 16 && repr_data->bits != 32 && repr_data->bits != 64)
        MVM_exception_throw_adhoc(tc, "MVMP6int: Unsupported int size (%dbit)", repr_data->bits);

    mk_storage_spec(tc, repr_data->bits, repr_data->is_unsigned, &repr_data->storage_spec);

    st->REPR_data = repr_data;
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    set_int(tc, st, root, data, MVM_serialization_read_int(tc, reader));
}

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVM_serialization_write_int(tc, writer, get_int(tc, st, NULL, data));
}

static void spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    switch (ins->info->opcode) {
        case MVM_OP_box_i: {
            if (repr_data->bits == 64 && !(st->mode_flags & MVM_FINALIZE_TYPE)) {
                /* Turn into a sp_fastbox_i[_ic] instruction. */
                MVMint32 int_cache_type_idx = MVM_intcache_type_index(tc, st->WHAT);
                MVMSpeshFacts *tgt_facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
                MVMSpeshOperand *orig_operands = ins->operands;
                ins->info = MVM_op_get_op(int_cache_type_idx < 0
                        ? MVM_OP_sp_fastbox_i
                        : MVM_OP_sp_fastbox_i_ic);
                ins->operands = MVM_spesh_alloc(tc, g, 6 * sizeof(MVMSpeshOperand));
                ins->operands[0] = orig_operands[0];
                ins->operands[1].lit_i16 = st->size;
                ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
                ins->operands[3].lit_i16 = offsetof(MVMP6int, body.value);
                ins->operands[4] = orig_operands[1];
                ins->operands[5].lit_i16 = (MVMint16)int_cache_type_idx;
                MVM_spesh_usages_delete_by_reg(tc, g, orig_operands[2], ins);
                tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE;
                tgt_facts->type = st->WHAT;
            }
            break;
        }
    }
}
/* Initializes the representation. */
const MVMREPROps * MVMP6int_initialize(MVMThreadContext *tc) {
    return &P6int_this_repr;
}

static const MVMREPROps P6int_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        set_int,
        get_int,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
        set_uint,
        get_uint,
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
    "P6int", /* name */
    MVM_REPR_ID_P6int,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
