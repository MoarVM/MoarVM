#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMHash);
    });

    return st->WHAT;
}

MVM_STATIC_INLINE void extract_key(MVMThreadContext *tc, void **kdata, size_t *klen, MVMObject *key) {
    MVM_HASH_EXTRACT_KEY(tc, kdata, klen, key, "MVMHash representation requires MVMString keys")
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMHashBody *src_body  = (MVMHashBody *)src;
    MVMHashBody *dest_body = (MVMHashBody *)dest;
    MVMHashEntry *current, *tmp;
    unsigned bucket_tmp;

    /* NOTE: if we really wanted to, we could avoid rehashing... */
    HASH_ITER(hash_handle, src_body->hash_head, current, tmp, bucket_tmp) {
        size_t klen;
        void *kdata;
        MVMHashEntry *new_entry = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            sizeof(MVMHashEntry));
        MVM_ASSIGN_REF(tc, &(dest_root->header), new_entry->key, current->key);
        MVM_ASSIGN_REF(tc, &(dest_root->header), new_entry->value, current->value);
        extract_key(tc, &kdata, &klen, new_entry->key);

        HASH_ADD_KEYPTR(hash_handle, dest_body->hash_head, kdata, klen, new_entry);
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMHashBody *body = (MVMHashBody *)data;
    MVMHashEntry *current, *tmp;
    unsigned bucket_tmp;

    HASH_ITER(hash_handle, body->hash_head, current, tmp, bucket_tmp) {
        MVM_gc_worklist_add(tc, worklist, &current->key);
        MVM_gc_worklist_add(tc, worklist, &current->value);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMHash *h = (MVMHash *)obj;
    MVMHashEntry *current, *tmp;
    unsigned bucket_tmp;
    HASH_ITER(hash_handle, h->body.hash_head, current, tmp, bucket_tmp) {
        if (current != h->body.hash_head)
            MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMHashEntry), current);
    }
    tmp = h->body.hash_head;
    HASH_CLEAR(hash_handle, h->body.hash_head);
    if (tmp)
        MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMHashEntry), tmp);
}

static void at_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister *result, MVMuint16 kind) {
    MVMHashBody *body = (MVMHashBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;
    extract_key(tc, &kdata, &klen, key);
    HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
    if (kind == MVM_reg_obj)
        result->o = entry != NULL ? entry->value : tc->instance->VMNull;
    else
        MVM_exception_throw_adhoc(tc,
            "MVMHash representation does not support native type storage");
}

static void bind_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister value, MVMuint16 kind) {
    MVMHashBody *body = (MVMHashBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;

    extract_key(tc, &kdata, &klen, key);

    /* first check whether we can must update the old entry. */
    HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
    if (!entry) {
        entry = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            sizeof(MVMHashEntry));
        HASH_ADD_KEYPTR(hash_handle, body->hash_head, kdata, klen, entry);
    }
    else
        entry->hash_handle.key = (void *)kdata;
    MVM_ASSIGN_REF(tc, &(root->header), entry->key, key);
    if (kind == MVM_reg_obj) {
        MVM_ASSIGN_REF(tc, &(root->header), entry->value, value.o);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "MVMHash representation does not support native type storage");
    }
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMHashBody *body = (MVMHashBody *)data;
    return HASH_CNT(hash_handle, body->hash_head);
}

static MVMint64 exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMHashBody *body = (MVMHashBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;
    extract_key(tc, &kdata, &klen, key);

    HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
    return entry != NULL;
}

static void delete_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMHashBody *body = (MVMHashBody *)data;
    MVMHashEntry *old_entry;
    size_t klen;
    void *kdata;
    extract_key(tc, &kdata, &klen, key);

    HASH_FIND(hash_handle, body->hash_head, kdata, klen, old_entry);
    if (old_entry) {
        HASH_DELETE(hash_handle, body->hash_head, old_entry);
        MVM_fixed_size_free(tc, tc->instance->fsa,
            sizeof(MVMHashEntry), old_entry);
    }
}

static MVMStorageSpec get_value_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    spec.bits            = 0;
    spec.align           = 0;
    spec.is_unsigned     = 0;
    return spec;
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
    /* XXX key and value types will be communicated here */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMHash);
}

/* Bytecode specialization for this REPR. */
static void spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    switch (ins->info->opcode) {
    case MVM_OP_create: {
        if (!(st->mode_flags & MVM_FINALIZE_TYPE)) {
            MVMSpeshOperand target   = ins->operands[0];
            MVMSpeshOperand type     = ins->operands[1];
            ins->info                = MVM_op_get_op(MVM_OP_sp_fastcreate);
            ins->operands            = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0]         = target;
            ins->operands[1].lit_i16 = sizeof(MVMHash);
            ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
            MVM_spesh_get_facts(tc, g, type)->usages--;
        }
        break;
    }
    }
}

/* Initializes the representation. */
const MVMREPROps * MVMHash_initialize(MVMThreadContext *tc) {
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
    {
        at_key,
        bind_key,
        exists_key,
        delete_key,
        get_value_storage_spec
    },    /* ass_funcs */
    elems,
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
    spesh,
    "VMHash", /* name */
    MVM_REPR_ID_MVMHash,
    0, /* refs_frames */
};
