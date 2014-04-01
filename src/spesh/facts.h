/* Facts we might have about a local. */
struct MVMSpeshFacts {
    /* Flags indicating things we know. */
    MVMint32 flags;

    /* Known type, if any. */
    MVMObject *type;

    /* Known value, if any. */
    union {
        MVMObject *o;
    } value;
};

/* Various fact flags. */
#define MVM_SPESH_FACT_KNOWN_TYPE   1   /* Has a known type. */
#define MVM_SPESH_FACT_KNOWN_VALUE  2   /* Has a known value. */
#define MVM_SPESH_FACT_DECONTED     4   /* Know it's decontainerized. */
#define MVM_SPESH_FACT_CONCRETE     8   /* Know it's a concrete object. */
#define MVM_SPESH_FACT_TYPEOBJ      16  /* Know it's a type object. */

/* Discovers spesh facts and builds up information about them. */
void MVM_spesh_facts_discover(MVMThreadContext *tc, MVMSpeshGraph *g);
