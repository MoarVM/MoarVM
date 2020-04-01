/* Container specification information, for types that serve as containers.
 * A container is something that can be assigned into. It may be some kind
 * of container object (like Perl 6's Scalar) or it may be a reference to a
 * native lexical or object field. The function table determines the way it
 * behaves. */
struct MVMContainerSpec {
    /* Name of this container specification. */
    char *name;

    /* Fetches a value out of a container. Used for decontainerization. */
    void (*fetch) (MVMThreadContext *tc, MVMObject *cont, MVMRegister *res);

    /* Native value fetches. */
    void (*fetch_i) (MVMThreadContext *tc, MVMObject *cont, MVMRegister *res);
    void (*fetch_n) (MVMThreadContext *tc, MVMObject *cont, MVMRegister *res);
    void (*fetch_s) (MVMThreadContext *tc, MVMObject *cont, MVMRegister *res);

    /* Stores a value in a container. Used for assignment. */
    void (*store) (MVMThreadContext *tc, MVMObject *cont, MVMObject *obj);

    /* Native container stores. */
    void (*store_i) (MVMThreadContext *tc, MVMObject *cont, MVMint64 value);
    void (*store_n) (MVMThreadContext *tc, MVMObject *cont, MVMnum64 value);
    void (*store_s) (MVMThreadContext *tc, MVMObject *cont, MVMString *value);

    /* Stores a value in a container, without any checking of it (this
     * assumes an optimizer or something else already did it). Used for
     * assignment. */
    void (*store_unchecked) (MVMThreadContext *tc, MVMObject *cont, MVMObject *obj);

    /* Allow the Container Spec to emit better bytecode, for example for
     * a decont operation. */
    void (*spesh) (MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins);

    /* Marks container data, if any. */
    void (*gc_mark_data) (MVMThreadContext *tc, MVMSTable *st, MVMGCWorklist *worklist);

    /* Frees container data, if any. */
    void (*gc_free_data) (MVMThreadContext *tc, MVMSTable *st);

    /* Serializes the container data, if any. */
    void (*serialize) (MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer);

    /* Deserializes the container data, if any. */
    void (*deserialize) (MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader);

    /* Called when the whole of the REPR data has been deserialized (so the
     * offsets of attributes could be queried, for example). */
    void (*post_deserialize) (MVMThreadContext *tc, MVMSTable *st);

    /* Returns a non-zero value if we can store to the container. */
    MVMint32 (*can_store) (MVMThreadContext *tc, MVMObject *cont);

    /* If available, reference atomic compare and swap operation, atomic load
     * operation, and atomic store operation. */
    void (*cas) (MVMThreadContext *tc, MVMObject *cont, MVMObject *expected,
        MVMObject *value, MVMRegister *result);
    MVMObject * (*atomic_load) (MVMThreadContext *tc, MVMObject *cont);
    void (*atomic_store) (MVMThreadContext *tc, MVMObject *cont, MVMObject *value);

    /* Set this to a non-zero value if a fetch promises to never invoke any
     * code. This means the VM knows it can safely decontainerize in places
     * it would not be safe or practical to return to the interpreter. */
    MVMuint8 fetch_never_invokes;
};

/* A container configurer knows how to attach a certain type of container
 * to an STable and configure it. */
struct MVMContainerConfigurer {
    /* Sets this container spec in place for the specified STable. */
    void (*set_container_spec) (MVMThreadContext *tc, MVMSTable *st);

    /* Configures the container spec with the specified info. */
    void (*configure_container_spec) (MVMThreadContext *tc, MVMSTable *st, MVMObject *config);
};

/* Container registry is a hash mapping names of container configurations
 * to function tables. */
struct MVMContainerRegistry {
    MVMString              *name;
    const MVMContainerConfigurer *configurer;

    /* Inline handle to the hash in which this is stored. */
    UT_hash_handle hash_handle;
};

MVM_PUBLIC void MVM_6model_add_container_config(MVMThreadContext *tc, MVMString *name, const MVMContainerConfigurer *configurer);
const MVMContainerConfigurer * MVM_6model_get_container_config(MVMThreadContext *tc, MVMString *name);
void MVM_6model_containers_setup(MVMThreadContext *tc);
MVMint64 MVM_6model_container_iscont_rw(MVMThreadContext *tc, MVMObject *cont);
MVMint64 MVM_6model_container_iscont_i(MVMThreadContext *tc, MVMObject *cont);
MVMint64 MVM_6model_container_iscont_n(MVMThreadContext *tc, MVMObject *cont);
MVMint64 MVM_6model_container_iscont_s(MVMThreadContext *tc, MVMObject *cont);
void MVM_6model_container_decont_i(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res);
void MVM_6model_container_decont_n(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res);
void MVM_6model_container_decont_s(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res);
void MVM_6model_container_decont_u(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res);
void MVM_6model_container_assign_i(MVMThreadContext *tc, MVMObject *cont, MVMint64 value);
void MVM_6model_container_assign_n(MVMThreadContext *tc, MVMObject *cont, MVMnum64 value);
void MVM_6model_container_assign_s(MVMThreadContext *tc, MVMObject *cont, MVMString *value);
void MVM_6model_container_cas(MVMThreadContext *tc, MVMObject *cont,
    MVMObject *expected, MVMObject *value, MVMRegister *result);
MVMObject * MVM_6model_container_atomic_load(MVMThreadContext *tc, MVMObject *cont);
void MVM_6model_container_atomic_store(MVMThreadContext *tc, MVMObject *cont, MVMObject *value);
MVMint64 MVM_6model_container_cas_i(MVMThreadContext *tc, MVMObject *cont,
    MVMint64 expected, MVMint64 value);
MVMint64 MVM_6model_container_atomic_load_i(MVMThreadContext *tc, MVMObject *cont);
void MVM_6model_container_atomic_store_i(MVMThreadContext *tc, MVMObject *cont, MVMint64 value);
MVMint64 MVM_6model_container_atomic_inc(MVMThreadContext *tc, MVMObject *cont);
MVMint64 MVM_6model_container_atomic_dec(MVMThreadContext *tc, MVMObject *cont);
MVMint64 MVM_6model_container_atomic_add(MVMThreadContext *tc, MVMObject *cont, MVMint64 value);

void *MVM_container_devirtualize_fetch_for_jit(MVMThreadContext *tc, MVMSTable *st, MVMuint16 type);
void *MVM_container_devirtualize_store_for_jit(MVMThreadContext *tc, MVMSTable *st, MVMuint16 type);
