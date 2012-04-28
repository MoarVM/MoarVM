/* The various "bootstrap" types, based straight off of some core
 * representations. They are used during the 6model bootstrap. */
struct _MVMBootTypes {
    MVMObject *BOOTStr;
    MVMObject *BOOTArray;
    MVMObject *BOOTHash;
    MVMObject *BOOTCode;
};

/* Represents a MoarVM instance. */
typedef struct _MVMInstance {
    /* The list of active threads. */
    MVMThreadContext **threads;
    
    /* The number of active threads. */
    MVMuint16 num_threads;
    
    /* Set of bootstrapping types. */
    struct _MVMBootTypes *boot_types;
    
    /* The KnowHOW meta-object; all other meta-objects (which are
     * built in user-space) are built out of this. */
    MVMObject *KnowHOW;
} MVMInstance;
