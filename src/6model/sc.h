/* A serialization context exists (optionally) per compilation unit.
 * It contains the declarative objects for the compilation unit, and
 * they are serialized if code is pre-compiled. */
typedef struct _MVMSerializationContext {
    /* Commonalities that all collectable entities have. */
    MVMCollectable header;
    
    /* The handle of this SC. */
    struct _MVMString *handle;
    
    /* Description (probably the file name) if any. */
    struct _MVMString *description;
    
    /* The root set of objects that live in this SC. */
    MVMObject *root_objects;
    
    /* The root set of STables that live in this SC. */
    MVMObject *root_stables;
    
    /* The root set of code refs that live in this SC. */
    MVMObject *root_codes;
    
    /* XXX Repossession info. */
} MVMSerializationContext;
