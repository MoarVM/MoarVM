#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

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
    return &this_repr;
}

static const MVMREPROps this_repr = {
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

/* Configures the decoder with the specified encoding and other configuration. */
void MVM_decoder_configure(MVMThreadContext *tc, MVMDecoder *decoder,
                           MVMString *encoding, MVMObject *config) {
    if (!decoder->body.ds) {
        MVMuint8 encid = MVM_string_find_encoding(tc, encoding);
        decoder->body.ds = MVM_string_decodestream_create(tc, encid, 0, 0);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Decoder already configured");
    }
}

/* Obtains the DecodeStream object provided it's initialized, throwing if not. */
static MVMDecodeStream * get_ds(MVMThreadContext *tc, MVMDecoder *decoder) {
    MVMDecodeStream *ds = decoder->body.ds;
    if (!ds)
        MVM_exception_throw_adhoc(tc, "Docder not yet configured");
    return ds;
}

/* Adds bytes to the deocde stream. */
void MVM_decoder_add_bytes(MVMThreadContext *tc, MVMDecoder *decoder, MVMObject *buffer) {
    MVMDecodeStream *ds = get_ds(tc, decoder);
    if (REPR(buffer)->ID == MVM_REPR_ID_MVMArray) {
        /* To be safe, we need to make a copy of data in a resizable array; it
         * may change/move under us. */
        char *output, *copy;
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
        MVM_string_decodestream_add_bytes(tc, ds, copy, output_size);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot add bytes to a decoder with a %s",
            REPR(buffer)->name);
    }
}

/* Takes the specified number of chars from the decoder, or all if there
 * is not enough. */
MVMString * MVM_decoder_take_chars(MVMThreadContext *tc, MVMDecoder *decoder, MVMint64 chars) {
    return MVM_string_decodestream_get_chars(tc, get_ds(tc, decoder), (MVMint32)chars);
}

/* Takes all chars from the decoder. */
MVMString * MVM_decoder_take_all_chars(MVMThreadContext *tc, MVMDecoder *decoder) {
    return MVM_string_decodestream_get_all(tc, get_ds(tc, decoder));
}

/* Takes all available chars from the decoder. */
MVMString * MVM_decoder_take_available_chars(MVMThreadContext *tc, MVMDecoder *decoder) {
    return MVM_string_decodestream_get_available(tc, get_ds(tc, decoder));
}

/* Returns true if the decoder is empty. */
MVMint64 MVM_decoder_empty(MVMThreadContext *tc, MVMDecoder *decoder) {
    return MVM_string_decodestream_is_empty(tc, get_ds(tc, decoder));
}
