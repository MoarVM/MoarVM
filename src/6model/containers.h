/* Container specification information, for types that serve as containers.
 * A container is something that can be assigned into. It may be some kind
 * of container object (like Perl 6's Scalar) or it may be a reference to a
 * native lexical or object field. The function table determines the way it
 * behaves. */
typedef struct _MVMContainerSpec {
    /* Fetches a value out of a container. Used for decontainerization. */
    void (*fetch) (MVMThreadContext *tc, MVMObject *cont, MVMRegister *res);

    /* Stores a value in a container. Used for assignment. */
    void (*store) (MVMThreadContext *tc, MVMObject *cont, MVMObject *obj);

    /* Stores a value in a container, without any checking of it (this
     * assumes an optimizer or something else already did it). Used for
     * assignment. */
    void (*store_unchecked) (MVMThreadContext *tc, MVMObject *cont, MVMObject *obj);

    /* Name of this container specification. */
    MVMString *name;

    /* Marks container data, if any. */
    void (*gc_mark_data) (MVMThreadContext *tc, MVMSTable *st, struct _MVMGCWorklist *worklist);

    /* Frees container data, if any. */
    void (*gc_free_data) (MVMThreadContext *tc, MVMSTable *st);

    /* Serializes the container data, if any. */
    void (*serialize) (MVMThreadContext *tc, MVMSTable *st, struct _MVMSerializationWriter *writer);

    /* Deserializes the container data, if any. */
    void (*deserialize) (MVMThreadContext *tc, MVMSTable *st, struct _MVMSerializationReader *reader);
} MVMContainerSpec;

/* A container configurer knows how to attach a certain type of container
 * to an STable and configure it. */
typedef struct _MVMContainerConfigurer{
    /* Sets this container spec in place for the specified STable. */
    void (*set_container_spec) (MVMThreadContext *tc, MVMSTable *st);

    /* Configures the container spec with the specified info. */
    void (*configure_container_spec) (MVMThreadContext *tc, MVMSTable *st, MVMObject *config);
} MVMContainerConfigurer;

void MVM_6model_add_container_config(MVMThreadContext *tc, MVMString *name, MVMContainerConfigurer *configurer);
MVMContainerConfigurer * MVM_6model_get_container_config(MVMThreadContext *tc, MVMString *name);
void MVM_6model_containers_setup(MVMThreadContext *tc);

/* Macro for decontainerization. */
#define DECONT(tc, src, dest) do {\
    if(IS_CONCRETE(src) && STABLE(src)->container_spec)\
        STABLE(src)->container_spec->fetch(tc, src, &(dest));\
    else\
        ((dest).o = src); \
    } while(0)
