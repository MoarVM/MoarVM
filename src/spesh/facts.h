/* Facts we might have about a local. */
struct MVMSpeshFacts {
    /* Flags indicating things we know. */
    MVMint32 flags;

    /* The number of usages it has. */
    MVMint32 usages;

    /* Known type, if any. */
    MVMObject *type;

    /* Known type post-decontainerization, if any. */
    MVMObject *decont_type;

    /* The instruction that's the origin of this register + version */
    MVMSpeshIns *assigner;

    /* Known value, if any. */
    union {
        MVMObject *o;
        MVMint64 i64;
        MVMint32 i32;
        MVMint16 i16;
        MVMint8  i8;
        MVMnum32 n32;
        MVMnum64 n64;
    } value;
};

/* Various fact flags. */
#define MVM_SPESH_FACT_KNOWN_TYPE           1   /* Has a known type. */
#define MVM_SPESH_FACT_KNOWN_VALUE          2   /* Has a known value. */
#define MVM_SPESH_FACT_DECONTED             4   /* Know it's decontainerized. */
#define MVM_SPESH_FACT_CONCRETE             8   /* Know it's a concrete object. */
#define MVM_SPESH_FACT_TYPEOBJ              16  /* Know it's a type object. */
#define MVM_SPESH_FACT_KNOWN_DECONT_TYPE    32  /* Has a known type after decont. */
#define MVM_SPESH_FACT_DECONT_CONCRETE      64  /* Is concrete after decont. */
#define MVM_SPESH_FACT_DECONT_TYPEOBJ       128 /* Is a type object after decont. */

/* Discovers spesh facts and builds up information about them. */
void MVM_spesh_facts_discover(MVMThreadContext *tc, MVMSpeshGraph *g);
