#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps Decoder_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &Decoder_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMDecoder);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation Decoder");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMDecoder *decoder = (MVMDecoder *)obj;
    if (decoder->body.ds)
        MVM_string_decodestream_destroy(tc, decoder->body.ds);
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMDecoder);
}

/* Initializes the representation. */
const MVMREPROps * MVMDecoder_initialize(MVMThreadContext *tc) {
    return &Decoder_this_repr;
}

static const MVMREPROps Decoder_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    deserialize_stable_size,
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "Decoder", /* name */
    MVM_REPR_ID_Decoder,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

/* Assert that the passed object really is a decoder; throw if not. */
void MVM_decoder_ensure_decoder(MVMThreadContext *tc, MVMObject *decoder, const char *op) {
    if (REPR(decoder)->ID != MVM_REPR_ID_Decoder || !IS_CONCRETE(decoder))
        MVM_exception_throw_adhoc(tc,
            "Operatation '%s' can only work on an object with the Decoder representation",
            op);
}

/* Checks and sets the decoder single-user sanity check flag. */
static void enter_single_user(MVMThreadContext *tc, MVMDecoder *decoder) {
    if (!MVM_trycas(&(decoder->body.in_use), 0, 1))
       MVM_exception_throw_adhoc(tc, "Decoder may not be used concurrently"); 
}

/* Releases the decoder single-user sanity check flag. */
static void exit_single_user(MVMThreadContext *tc, MVMDecoder *decoder) {
    decoder->body.in_use = 0;
}

/* Configures the decoder with the specified encoding and other configuration. */
static int should_translate_newlines(MVMThreadContext *tc, MVMObject *config) {
    if (IS_CONCRETE(config) && REPR(config)->ID == MVM_REPR_ID_MVMHash) {
        MVMObject *value = MVM_repr_at_key_o(tc, config,
            tc->instance->str_consts.translate_newlines);
        return IS_CONCRETE(value) && MVM_repr_get_int(tc, value) != 0;
    }
    return 0;
}
void MVM_decoder_configure(MVMThreadContext *tc, MVMDecoder *decoder,
                           MVMString *encoding, MVMObject *config) {
    if (!decoder->body.ds) {
        MVMuint8 encid = MVM_string_find_encoding(tc, encoding);
        enter_single_user(tc, decoder);
        decoder->body.ds = MVM_string_decodestream_create(tc, encid, 0,
            should_translate_newlines(tc, config));
        decoder->body.sep_spec = MVM_malloc(sizeof(MVMDecodeStreamSeparators));
        MVM_string_decode_stream_sep_default(tc, decoder->body.sep_spec);
        exit_single_user(tc, decoder);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Decoder already configured");
    }
}

/* Obtains the DecodeStream object provided it's initialized, throwing if not. */
static MVMDecodeStream * get_ds(MVMThreadContext *tc, MVMDecoder *decoder) {
    MVMDecodeStream *ds = decoder->body.ds;
    if (!ds)
        MVM_exception_throw_adhoc(tc, "Decoder not yet configured");
    return ds;
}

/* Gets the separators specification for the decoder. */
MVM_STATIC_INLINE MVMDecodeStreamSeparators * get_sep_spec(MVMThreadContext *tc, MVMDecoder *decoder) {
    return decoder->body.sep_spec;
}

/* Sets the separators to be used by this decode stream. */
void MVM_decoder_set_separators(MVMThreadContext *tc, MVMDecoder *decoder, MVMObject *seps) {
    MVMint32 is_str_array = REPR(seps)->pos_funcs.get_elem_storage_spec(tc,
        STABLE(seps)).boxed_primitive == MVM_STORAGE_SPEC_BP_STR;
    get_ds(tc, decoder); /* Ensure we're sufficiently initialized. */
    if (is_str_array) {
        MVMString **c_seps = NULL;
        MVMuint64 i;
        MVMuint64 num_seps = MVM_repr_elems(tc, seps);
        if (num_seps > 0xFFFFFF)
            MVM_exception_throw_adhoc(tc, "Too many line separators");
        c_seps = MVM_malloc((num_seps ? num_seps : 1) * sizeof(MVMString *));
        for (i = 0; i < num_seps; i++)
            c_seps[i] = MVM_repr_at_pos_s(tc, seps, i);
        enter_single_user(tc, decoder);
        MVM_string_decode_stream_sep_from_strings(tc, get_sep_spec(tc, decoder),
            c_seps, num_seps);
        exit_single_user(tc, decoder);
        MVM_free(c_seps);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Set separators requires a native string array");
    }
}

/* Adds bytes to the decode stream. */
void MVM_decoder_add_bytes(MVMThreadContext *tc, MVMDecoder *decoder, MVMObject *buffer) {
    MVMDecodeStream *ds = get_ds(tc, decoder);
    if (REPR(buffer)->ID == MVM_REPR_ID_VMArray) {
        /* To be safe, we need to make a copy of data in a resizable array; it
         * may change/move under us. */
        char *output = NULL, *copy = NULL;
        MVMint64 output_size;
        switch (((MVMArrayREPRData *)STABLE(buffer)->REPR_data)->slot_type) {
            case MVM_ARRAY_U8:
            case MVM_ARRAY_I8:
                output = (char *)(((MVMArray *)buffer)->body.slots.i8 + ((MVMArray *)buffer)->body.start);
                output_size = ((MVMArray *)buffer)->body.elems;
                break;
            case MVM_ARRAY_U16:
            case MVM_ARRAY_I16:
                output = (char *)(((MVMArray *)buffer)->body.slots.i16 + ((MVMArray *)buffer)->body.start);
                output_size = ((MVMArray *)buffer)->body.elems * 2;
                break;
            case MVM_ARRAY_U32:
            case MVM_ARRAY_I32:
                output = (char *)(((MVMArray *)buffer)->body.slots.i32 + ((MVMArray *)buffer)->body.start);
                output_size = ((MVMArray *)buffer)->body.elems * 4;
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Can only add bytes from an int array to a decoder");
        }
        copy = MVM_malloc(output_size);
        memcpy(copy, output, output_size);
        enter_single_user(tc, decoder);
        MVM_string_decodestream_add_bytes(tc, ds, copy, output_size);
        exit_single_user(tc, decoder);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot add bytes to a decoder with a %s",
            REPR(buffer)->name);
    }
}

/* Takes the specified number of chars from the decoder, or all if there
 * is not enough. */
MVMString * MVM_decoder_take_chars(MVMThreadContext *tc, MVMDecoder *decoder, MVMint64 chars,
                                   MVMint64 eof) {
    MVMString *result = NULL;
    enter_single_user(tc, decoder);
    MVMROOT(tc, decoder, {
        result = MVM_string_decodestream_get_chars(tc, get_ds(tc, decoder), (MVMint32)chars, eof);
    });
    exit_single_user(tc, decoder);
    return result;
}

/* Takes all chars from the decoder. */
MVMString * MVM_decoder_take_all_chars(MVMThreadContext *tc, MVMDecoder *decoder) {
    MVMString *result = NULL;
    enter_single_user(tc, decoder);
    MVMROOT(tc, decoder, {
        result = MVM_string_decodestream_get_all(tc, get_ds(tc, decoder));
    });
    exit_single_user(tc, decoder);
    return result;
}

/* Takes all available chars from the decoder. */
MVMString * MVM_decoder_take_available_chars(MVMThreadContext *tc, MVMDecoder *decoder) {
    MVMString *result = NULL;
    enter_single_user(tc, decoder);
    MVMROOT(tc, decoder, {
        result = MVM_string_decodestream_get_available(tc, get_ds(tc, decoder));
    });
    exit_single_user(tc, decoder);
    return result;
}

/* Takes a line from the decoder. */
MVMString * MVM_decoder_take_line(MVMThreadContext *tc, MVMDecoder *decoder,
                                  MVMint64 chomp, MVMint64 incomplete_ok) {
    MVMDecodeStream *ds = get_ds(tc, decoder);
    MVMDecodeStreamSeparators *sep_spec = get_sep_spec(tc, decoder);
    MVMString *result = NULL;
    enter_single_user(tc, decoder);
    MVMROOT(tc, decoder, {
        result = incomplete_ok
            ? MVM_string_decodestream_get_until_sep_eof(tc, ds, sep_spec, (MVMint32)chomp)
            : MVM_string_decodestream_get_until_sep(tc, ds, sep_spec, (MVMint32)chomp);
    });
    exit_single_user(tc, decoder);
    return result;
}

/* Returns true if the decoder is empty. */
MVMint64 MVM_decoder_empty(MVMThreadContext *tc, MVMDecoder *decoder) {
    return MVM_string_decodestream_is_empty(tc, get_ds(tc, decoder));
}

/* Gets the number of (undecoded) bytes available in the decoder. */
MVMint64 MVM_decoder_bytes_available(MVMThreadContext *tc, MVMDecoder *decoder) {
    return MVM_string_decodestream_bytes_available(tc, get_ds(tc, decoder));
}

/* Takes bytes from the decode stream and places them into a buffer. If there
 * are less available than requested, hand back null. */
MVMObject * MVM_decoder_take_bytes(MVMThreadContext *tc, MVMDecoder *decoder,
                                   MVMObject *buf_type, MVMint64 bytes) {
    MVMDecodeStream *ds = get_ds(tc, decoder);
    char *buf = NULL;
    MVMint64 read;
    MVMObject *result = NULL;

    /* Ensure the target is in the correct form. */
    if (REPR(buf_type)->ID != MVM_REPR_ID_VMArray)
        MVM_exception_throw_adhoc(tc, "decodertakebytes requires a native array type");
    if (((MVMArrayREPRData *)STABLE(buf_type)->REPR_data)->slot_type != MVM_ARRAY_U8
            && ((MVMArrayREPRData *)STABLE(buf_type)->REPR_data)->slot_type != MVM_ARRAY_I8)
        MVM_exception_throw_adhoc(tc, "decodertakebytes requires a native array type of uint8 or int8");
    if (bytes < 0 || bytes > 0x7FFFFFFF)
        MVM_exception_throw_adhoc(tc,
            "Out of range: attempted to read %"PRId64" bytes from decoder",
            bytes);

    if (MVM_string_decodestream_bytes_available(tc, ds) < bytes)
        return tc->instance->VMNull;

    result = MVM_repr_alloc_init(tc, buf_type);
    enter_single_user(tc, decoder);
    read = MVM_string_decodestream_bytes_to_buf(tc, ds, &buf, bytes);
    exit_single_user(tc, decoder);
    ((MVMArray *)result)->body.slots.i8 = (MVMint8 *)buf;
    ((MVMArray *)result)->body.start    = 0;
    ((MVMArray *)result)->body.ssize    = read;
    ((MVMArray *)result)->body.elems    = read;
    return result;
}
