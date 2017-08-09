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

    /* Known value, if any. */
    union {
        MVMObject *o;
        MVMint64 i;
        MVMnum64 n;
        MVMString *s;
    } value;

    /* The instruction that writes the register (noting we're in SSA form, so
     * this is unique). */
    MVMSpeshIns *writer;

    /* The deoptimization index in effect at the point of declaration, or -1
     * if none yet. */
    MVMint32 deopt_idx;

    /* The log guard the facts depend on, if any. */
    MVMuint32 log_guard;

    /* Has the instruction that wrote this value been deleted? */
    MVMuint32 dead_writer;
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
#define MVM_SPESH_FACT_FROM_LOG_GUARD       256 /* Depends on a guard being met. */
#define MVM_SPESH_FACT_HASH_ITER            512 /* Is an iter over hashes. */
#define MVM_SPESH_FACT_ARRAY_ITER           1024 /* Is an iter over arrays
                                                    (mutually exclusive with HASH_ITER, but neither of them is necessarily set) */
#define MVM_SPESH_FACT_KNOWN_BOX_SRC        2048 /* We know what register this value was boxed from */
#define MVM_SPESH_FACT_MERGED_WITH_LOG_GUARD 4096 /* These facts were merged at a PHI node, but at least one of the incoming facts had a "from log guard" flag set, so we'll have to look for that fact and increment its uses if we use this here fact. */
#define MVM_SPESH_FACT_RW_CONT               8192 /* Known to be an rw container */

void MVM_spesh_facts_discover(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshPlanned *p);
void MVM_spesh_facts_depend(MVMThreadContext *tc, MVMSpeshGraph *g,
    MVMSpeshFacts *target, MVMSpeshFacts *source);
