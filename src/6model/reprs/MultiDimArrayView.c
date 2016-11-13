#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Computes the flat number of elements from the given dimension list. */
static MVMint64 flat_elements(MVMint64 num_dimensions, MVMint64 *dimensions) {
    MVMint64 result = dimensions[0];
    MVMint64 i;
    for (i = 1; i < num_dimensions; i++)
        result *= dimensions[i];
    return result;
}

/* Computes the flat size from representation data. */
static size_t flat_size(MVMMultiDimArrayViewREPRData *repr_data, MVMint64 *dimensions) {
    return repr_data->elem_size * flat_elements(repr_data->num_dimensions, dimensions);
}

/* Takes a number of dimensions, indices we were passed, and dimension sizes.
 * Computes the offset into flat space. */
MVM_STATIC_INLINE size_t indices_to_flat_index(MVMThreadContext *tc, MVMint64 num_dimensions, MVMint64 *dimensions, MVMint64 *strides, MVMint64 *indices, MVMint64 initial_position) {
    size_t   result     = initial_position;
    MVMint64 i;
    for (i = num_dimensions - 1; i >= 0; i--) {
        MVMint64  dim_size = dimensions[i];
        MVMint64  stride   = strides[i];
        MVMint64  index    = indices[i];
        if (index >= 0 && index < dim_size) {
            result += index * stride;
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Index %"PRId64" for dimension %"PRId64" out of range (must be 0..%"PRId64")",
                index, i + 1, dim_size - 1);
        }
    }
    return result;
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMMultiDimArrayView);
    });

    return st->WHAT;
}

/* Allocates the mutli-dimensional array and sets up its dimensions array with
 * all zeroes, for later filling. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    if (repr_data) {
        MVMObject *obj = MVM_gc_allocate_object(tc, st);
        ((MVMMultiDimArrayView *)obj)->body.dimensions = MVM_fixed_size_alloc_zeroed(tc,
            tc->instance->fsa, repr_data->num_dimensions * sizeof(MVMint64));
        ((MVMMultiDimArrayView *)obj)->body.strides = MVM_fixed_size_alloc_zeroed(tc,
            tc->instance->fsa, repr_data->num_dimensions * sizeof(MVMint64));
        return obj;
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Cannot allocate a multi-dim array type before it is composed");
    }
}

/* Composes the representation. */
static void spec_to_repr_data(MVMThreadContext *tc, MVMMultiDimArrayViewREPRData *repr_data, const MVMStorageSpec *spec) {
    switch (spec->boxed_primitive) {
        case MVM_STORAGE_SPEC_BP_INT:
            if (spec->is_unsigned) {
                switch (spec->bits) {
                    case 64:
                        repr_data->slot_type = MVM_ARRAY_U64;
                        repr_data->elem_size = sizeof(MVMuint64);
                        break;
                    case 32:
                        repr_data->slot_type = MVM_ARRAY_U32;
                        repr_data->elem_size = sizeof(MVMuint32);
                        break;
                    case 16:
                        repr_data->slot_type = MVM_ARRAY_U16;
                        repr_data->elem_size = sizeof(MVMuint16);
                        break;
                    case 8:
                        repr_data->slot_type = MVM_ARRAY_U8;
                        repr_data->elem_size = sizeof(MVMuint8);
                        break;
                    case 4:
                        repr_data->slot_type = MVM_ARRAY_U4;
                        repr_data->elem_size = 0;
                        break;
                    case 2:
                        repr_data->slot_type = MVM_ARRAY_U2;
                        repr_data->elem_size = 0;
                        break;
                    case 1:
                        repr_data->slot_type = MVM_ARRAY_U1;
                        repr_data->elem_size = 0;
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc,
                            "MVMMultiDimArrayView: Unsupported uint size");
                }
            }
            else {
                switch (spec->bits) {
                    case 64:
                        repr_data->slot_type = MVM_ARRAY_I64;
                        repr_data->elem_size = sizeof(MVMint64);
                        break;
                    case 32:
                        repr_data->slot_type = MVM_ARRAY_I32;
                        repr_data->elem_size = sizeof(MVMint32);
                        break;
                    case 16:
                        repr_data->slot_type = MVM_ARRAY_I16;
                        repr_data->elem_size = sizeof(MVMint16);
                        break;
                    case 8:
                        repr_data->slot_type = MVM_ARRAY_I8;
                        repr_data->elem_size = sizeof(MVMint8);
                        break;
                    case 4:
                        repr_data->slot_type = MVM_ARRAY_I4;
                        repr_data->elem_size = 0;
                        break;
                    case 2:
                        repr_data->slot_type = MVM_ARRAY_I2;
                        repr_data->elem_size = 0;
                        break;
                    case 1:
                        repr_data->slot_type = MVM_ARRAY_I1;
                        repr_data->elem_size = 0;
                        break;
                    default:
                        MVM_exception_throw_adhoc(tc,
                            "MVMMultiDimArrayView: Unsupported int size");
                }
            }
            break;
        case MVM_STORAGE_SPEC_BP_NUM:
            switch (spec->bits) {
                case 64:
                    repr_data->slot_type = MVM_ARRAY_N64;
                    repr_data->elem_size = sizeof(MVMnum64);
                    break;
                case 32:
                    repr_data->slot_type = MVM_ARRAY_N32;
                    repr_data->elem_size = sizeof(MVMnum32);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc,
                        "MVMMultiDimArrayView: Unsupported num size");
            }
            break;
        case MVM_STORAGE_SPEC_BP_STR:
            repr_data->slot_type = MVM_ARRAY_STR;
            repr_data->elem_size = sizeof(MVMString *);
            break;
        default:
            repr_data->slot_type = MVM_ARRAY_OBJ;
            repr_data->elem_size = sizeof(MVMObject *);
    }
}
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *repr_info) {
    MVMStringConsts              *str_consts = &(tc->instance->str_consts);
    MVMMultiDimArrayViewREPRData *repr_data;

    MVMObject *info = MVM_repr_at_key_o(tc, repr_info, str_consts->array);
    if (!MVM_is_null(tc, info)) {
        MVMObject *dims = MVM_repr_at_key_o(tc, info, str_consts->dimensions);
        MVMObject *type = MVM_repr_at_key_o(tc, info, str_consts->type);
        MVMint64 dimensions;
        if (!MVM_is_null(tc, dims)) {
            dimensions = MVM_repr_get_int(tc, dims);
            if (dimensions < 1)
                MVM_exception_throw_adhoc(tc,
                    "MultiDimArrayView REPR must be composed with at least 1 dimension");
            repr_data = MVM_calloc(1, sizeof(MVMMultiDimArrayViewREPRData));
            repr_data->num_dimensions = dimensions;
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "MultiDimArrayView REPR must be composed with a number of dimensions");
        }
        if (!MVM_is_null(tc, type)) {
            const MVMStorageSpec *spec = REPR(type)->get_storage_spec(tc, STABLE(type));
            MVM_ASSIGN_REF(tc, &(st->header), repr_data->elem_type, type);
            spec_to_repr_data(tc, repr_data, spec);
        }
        else {
            repr_data->slot_type = MVM_ARRAY_OBJ;
            repr_data->elem_size = sizeof(MVMObject *);
        }
        st->REPR_data = repr_data;
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "MultiDimArrayView REPR must be composed with array information");
    }
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    MVMMultiDimArrayViewBody     *src_body  = (MVMMultiDimArrayViewBody *)src;
    MVMMultiDimArrayViewBody     *dest_body = (MVMMultiDimArrayViewBody *)dest;
    if (src_body->target) {
        size_t dim_size  = repr_data->num_dimensions * sizeof(MVMint64);
        size_t data_size = flat_size(repr_data, src_body->dimensions);
        dest_body->dimensions = MVM_fixed_size_alloc(tc, tc->instance->fsa, dim_size);
        dest_body->strides    = MVM_fixed_size_alloc(tc, tc->instance->fsa, dim_size);
        dest_body->target     = src_body->target;
        dest_body->initial_position = src_body->initial_position;
        memcpy(dest_body->dimensions, src_body->dimensions, dim_size);
        memcpy(dest_body->strides, src_body->strides, data_size);
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMMultiDimArrayViewBody *body = (MVMMultiDimArrayViewBody *)data;
    if (body->target) {
        MVM_gc_worklist_add(tc, worklist, body->target);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
	MVMMultiDimArrayView *arr = (MVMMultiDimArrayView *)obj;
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)STABLE(obj)->REPR_data;
    MVM_fixed_size_free(tc, tc->instance->fsa,
        repr_data->num_dimensions * sizeof(MVMint64),
        arr->body.dimensions);
    MVM_fixed_size_free(tc, tc->instance->fsa,
        repr_data->num_dimensions * sizeof(MVMint64),
        arr->body.strides);
}

/* Marks the representation data in an STable.*/
static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    if (repr_data == NULL)
        return;
    MVM_gc_worklist_add(tc, worklist, &repr_data->elem_type);
}

/* Free representation data. */
static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_free(st->REPR_data);
}

/* Gets the storage specification for this representation. */
static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Serializes the data held in the array. */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    MVMMultiDimArrayViewBody     *body      = (MVMMultiDimArrayViewBody *)data;
    MVMint64 i;

    /* Write out dimensions. */
    for (i = 0; i < repr_data->num_dimensions; i++)
        MVM_serialization_write_int(tc, writer, body->dimensions[i]);
    /* Write out strides. */
    for (i = 0; i < repr_data->num_dimensions; i++)
        MVM_serialization_write_int(tc, writer, body->strides[i]);

    MVM_serialization_write_int(tc, writer, body->initial_position);

    MVM_serialization_write_ref(tc, writer, body->target);
}

/* Deserializes the data held in the array. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    MVMMultiDimArrayViewBody     *body      = (MVMMultiDimArrayViewBody *)data;
    MVMint64 i;

    /* Read in dimensions. */
    for (i = 0; i < repr_data->num_dimensions; i++)
        body->dimensions[i] = MVM_serialization_read_int(tc, reader);

    /* Read in strides. */
    for (i = 0; i < repr_data->num_dimensions; i++)
        body->strides[i] = MVM_serialization_read_int(tc, reader);

    body->initial_position = MVM_serialization_read_int(tc, reader);

    MVM_ASSIGN_REF(tc, &(root->header), body->target, MVM_serialization_read_ref(tc, reader));

}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    if (repr_data) {
        MVM_serialization_write_int(tc, writer, repr_data->num_dimensions);
        MVM_serialization_write_ref(tc, writer, repr_data->elem_type);
    }
    else {
        MVM_serialization_write_int(tc, writer, 0);
    }
}

/* Deserializes the REPR data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMint64 num_dims;

    if (reader->root.version >= 19) {
        num_dims = MVM_serialization_read_int(tc, reader);
    } else {
        num_dims = MVM_serialization_read_int64(tc, reader);
    }

    if (num_dims > 0) {
        MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)MVM_malloc(sizeof(MVMMultiDimArrayViewREPRData));
        MVMObject *type;

        repr_data->num_dimensions = num_dims;
        type = MVM_serialization_read_ref(tc, reader);
        MVM_ASSIGN_REF(tc, &(st->header), repr_data->elem_type, type);

        if (type) {
            MVM_serialization_force_stable(tc, reader, STABLE(type));
            spec_to_repr_data(tc, repr_data, REPR(type)->get_storage_spec(tc, STABLE(type)));
        }
        else {
            repr_data->slot_type = MVM_ARRAY_OBJ;
            repr_data->elem_size = sizeof(MVMObject *);
        }

        st->REPR_data = repr_data;
    }
}

static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMMultiDimArrayView);
}

static void push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVM_exception_throw_adhoc(tc, "Cannot push onto a fixed dimension array");
}
static void pop(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVM_exception_throw_adhoc(tc, "Cannot pop a fixed dimension array");
}
static void unshift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVM_exception_throw_adhoc(tc, "Cannot unshift onto a fixed dimension array");
}
static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVM_exception_throw_adhoc(tc, "Cannot shift a fixed dimension array");
}
static void asplice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *from, MVMint64 offset, MVMuint64 count) {
    MVM_exception_throw_adhoc(tc, "Cannot splice a fixed dimension array");
}

static void at_pos_multidim(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_indices, MVMint64 *indices, MVMRegister *value, MVMuint16 kind) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    if (num_indices == repr_data->num_dimensions) {
        MVMMultiDimArrayViewBody *body = (MVMMultiDimArrayViewBody *)data;
        size_t flat_index = indices_to_flat_index(tc, repr_data->num_dimensions, body->dimensions, body->strides, indices, body->initial_position);
        MVMMultiDimArrayBody *targetbody = &((MVMMultiDimArray*)body->target)->body;
        switch (repr_data->slot_type) {
            case MVM_ARRAY_OBJ:
                if (kind == MVM_reg_obj) {
                    MVMObject *found = targetbody->slots.o[flat_index];
                    value->o = found ? found : tc->instance->VMNull;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected object register");
                }
                break;
            case MVM_ARRAY_STR:
                if (kind == MVM_reg_str)
                    value->s = targetbody->slots.s[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected string register");
                break;
            case MVM_ARRAY_I64:
                if (kind == MVM_reg_int64)
                    value->i64 = (MVMint64)targetbody->slots.i64[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected int register");
                break;
            case MVM_ARRAY_I32:
                if (kind == MVM_reg_int64)
                    value->i64 = (MVMint64)targetbody->slots.i32[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected int register");
                break;
            case MVM_ARRAY_I16:
                if (kind == MVM_reg_int64)
                    value->i64 = (MVMint64)targetbody->slots.i16[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected int register");
                break;
            case MVM_ARRAY_I8:
                if (kind == MVM_reg_int64)
                    value->i64 = (MVMint64)targetbody->slots.i8[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected int register");
                break;
            case MVM_ARRAY_N64:
                if (kind == MVM_reg_num64)
                    value->n64 = (MVMnum64)targetbody->slots.n64[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected num register");
                break;
            case MVM_ARRAY_N32:
                if (kind == MVM_reg_num64)
                    value->n64 = (MVMnum64)targetbody->slots.n32[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected num register");
                break;
            case MVM_ARRAY_U64:
                if (kind == MVM_reg_int64)
                    value->i64 = (MVMint64)targetbody->slots.u64[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected int register");
                break;
            case MVM_ARRAY_U32:
                if (kind == MVM_reg_int64)
                    value->i64 = (MVMint64)targetbody->slots.u32[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected int register");
                break;
            case MVM_ARRAY_U16:
                if (kind == MVM_reg_int64)
                    value->i64 = (MVMint64)targetbody->slots.u16[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected int register");
                break;
            case MVM_ARRAY_U8:
                if (kind == MVM_reg_int64)
                    value->i64 = (MVMint64)targetbody->slots.u8[flat_index];
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: atpos expected int register");
                break;
            default:
                MVM_exception_throw_adhoc(tc, "MultiDimArrayView: Unhandled slot type");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Cannot access %"PRId64" dimension array with %"PRId64" indices",
            repr_data->num_dimensions, num_indices);
    }
}

static void bind_pos_multidim(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_indices, MVMint64 *indices, MVMRegister value, MVMuint16 kind) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    if (num_indices == repr_data->num_dimensions) {
        MVMMultiDimArrayViewBody *body = (MVMMultiDimArrayViewBody *)data;
        size_t flat_index = indices_to_flat_index(tc, repr_data->num_dimensions, body->dimensions, body->strides, indices, body->initial_position);
        MVMCollectable *targetheader = &((MVMMultiDimArray*)body->target)->common.header;
        MVMMultiDimArrayBody *targetbody = &((MVMMultiDimArray*)body->target)->body;
        switch (repr_data->slot_type) {
            case MVM_ARRAY_OBJ:
                if (kind == MVM_reg_obj) {
                    MVM_ASSIGN_REF(tc, targetheader, targetbody->slots.o[flat_index], value.o);
                }
                else {
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected object register");
                }
                break;
            case MVM_ARRAY_STR:
                if (kind == MVM_reg_str) {
                    MVM_ASSIGN_REF(tc, targetheader, targetbody->slots.s[flat_index], value.s);
                }
                else {
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected string register");
                }
                break;
            case MVM_ARRAY_I64:
                if (kind == MVM_reg_int64)
                    targetbody->slots.i64[flat_index] = value.i64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected int register");
                break;
            case MVM_ARRAY_I32:
                if (kind == MVM_reg_int64)
                    targetbody->slots.i32[flat_index] = (MVMint32)value.i64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected int register");
                break;
            case MVM_ARRAY_I16:
                if (kind == MVM_reg_int64)
                    targetbody->slots.i16[flat_index] = (MVMint16)value.i64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected int register");
                break;
            case MVM_ARRAY_I8:
                if (kind == MVM_reg_int64)
                    targetbody->slots.i8[flat_index] = (MVMint8)value.i64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected int register");
                break;
            case MVM_ARRAY_N64:
                if (kind == MVM_reg_num64)
                    targetbody->slots.n64[flat_index] = value.n64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected num register");
                break;
            case MVM_ARRAY_N32:
                if (kind == MVM_reg_num64)
                    targetbody->slots.n32[flat_index] = (MVMnum32)value.n64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected num register");
                break;
            case MVM_ARRAY_U64:
                if (kind == MVM_reg_int64)
                    targetbody->slots.u64[flat_index] = value.i64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected int register");
                break;
            case MVM_ARRAY_U32:
                if (kind == MVM_reg_int64)
                    targetbody->slots.u32[flat_index] = (MVMuint32)value.i64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected int register");
                break;
            case MVM_ARRAY_U16:
                if (kind == MVM_reg_int64)
                    targetbody->slots.u16[flat_index] = (MVMuint16)value.i64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected int register");
                break;
            case MVM_ARRAY_U8:
                if (kind == MVM_reg_int64)
                    targetbody->slots.u8[flat_index] = (MVMuint8)value.i64;
                else
                    MVM_exception_throw_adhoc(tc, "MultiDimArrayView: bindpos expected int register");
                break;
            default:
                MVM_exception_throw_adhoc(tc, "MultiDimArrayView: Unhandled slot type");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Cannot access %"PRId64" dimension array with %"PRId64" indices",
            repr_data->num_dimensions, num_indices);
    }
}

static void dimensions(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 *num_dimensions, MVMint64 **dimensions) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    if (repr_data) {
        MVMMultiDimArrayViewBody *body = (MVMMultiDimArrayViewBody *)data;
        *num_dimensions = repr_data->num_dimensions;
        *dimensions = body->dimensions;
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Cannot query a multi-dim array's dimensionality before it is composed");
    }
}

static void set_dimensions(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 num_dimensions, MVMint64 *dimensions) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    if (num_dimensions == repr_data->num_dimensions) {
        MVMMultiDimArrayViewBody *body = (MVMMultiDimArrayViewBody *)data;
        size_t size = flat_size(repr_data, dimensions);
        /* TODO check if the amount of elements fits the target's flat elements */
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Array type of %"PRId64" dimensions cannot be initialized with %"PRId64" dimensions",
            repr_data->num_dimensions, num_dimensions);
    }
}

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    at_pos_multidim(tc, st, root, data, 1, &index, value, kind);
}

static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    bind_pos_multidim(tc, st, root, data, 1, &index, value, kind);
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    set_dimensions(tc, st, root, data, 1, (MVMint64 *)&count);
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMint64  _;
    MVMint64 *dims;
    dimensions(tc, st, root, data, &_, &dims);
    return (MVMuint64)dims[0];
}

static MVMStorageSpec get_elem_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMMultiDimArrayViewREPRData *repr_data = (MVMMultiDimArrayViewREPRData *)st->REPR_data;
    MVMStorageSpec spec;

    /* initialise storage spec to default values */
    spec.bits            = 0;
    spec.align           = 0;
    spec.is_unsigned     = 0;

    switch (repr_data->slot_type) {
        case MVM_ARRAY_STR:
            spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_STR;
            spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_STR;
            break;
        case MVM_ARRAY_I64:
        case MVM_ARRAY_I32:
        case MVM_ARRAY_I16:
        case MVM_ARRAY_I8:
            spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_INT;
            spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_INT;
            break;
        case MVM_ARRAY_N64:
        case MVM_ARRAY_N32:
            spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NUM;
            spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_NUM;
            break;
        case MVM_ARRAY_U64:
        case MVM_ARRAY_U32:
        case MVM_ARRAY_U16:
        case MVM_ARRAY_U8:
            spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_INT;
            spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_INT;
            spec.is_unsigned     = 1;
            break;
        default:
            spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
            spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
            spec.can_box         = 0;
            break;
    }
    return spec;
}

/* Initializes the representation. */
const MVMREPROps * MVMMultiDimArrayView_initialize(MVMThreadContext *tc) {
    return &this_repr;
}


void MVM_view_set_strides(MVMThreadContext *tc, MVMObject *target, MVMObject *strides) {
    
}

MVMObject * MVM_view_get_strides(MVMThreadContext *tc, MVMObject *target) {

}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    {
        at_pos,
        bind_pos,
        set_elems,
        push,
        pop,
        unshift,
        shift,
        asplice,
        at_pos_multidim,
        bind_pos_multidim,
        dimensions,
        set_dimensions,
        get_elem_storage_spec
    },
    MVM_REPR_DEFAULT_ASS_FUNCS,
    elems,
    get_storage_spec,
    NULL, /* change_type */
    serialize,
    deserialize,
    serialize_repr_data,
    deserialize_repr_data,
    deserialize_stable_size,
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    gc_mark_repr_data,
    gc_free_repr_data,
    compose,
    NULL, /* spesh */
    "MultiDimArrayView", /* name */
    MVM_REPR_ID_MultiDimArrayView,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
