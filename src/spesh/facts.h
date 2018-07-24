/* Facts we might have about a particular SSA version of a register. */
struct MVMSpeshFacts {
    /* Flags indicating things we know. */
    MVMint32 flags;

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

    /* Usages of this version of the register; thanks to SSA form, this taken
     * together with writer form a define-use chain. */
    MVMSpeshUsages usage;

    /* The log guard(s) the facts depend on, if any. */
    MVMuint32 *log_guards;
    MVMuint32 num_log_guards;

    /* Has the instruction that wrote this value been deleted? */
    MVMuint16 dead_writer;

    /* Information associated with this value for the use of partial escape
     * analysis. */
    MVMSpeshPEAInfo pea;
};

/* Various fact flags. */
#define MVM_SPESH_FACT_KNOWN_TYPE           1   /* Has a known type. */
#define MVM_SPESH_FACT_KNOWN_VALUE          2   /* Has a known value. */
#define MVM_SPESH_FACT_CONCRETE             8   /* Know it's a concrete object. */
#define MVM_SPESH_FACT_TYPEOBJ              16  /* Know it's a type object. */
#define MVM_SPESH_FACT_KNOWN_DECONT_TYPE    32  /* Has a known type after decont. */
#define MVM_SPESH_FACT_DECONT_CONCRETE      64  /* Is concrete after decont. */
#define MVM_SPESH_FACT_DECONT_TYPEOBJ       128 /* Is a type object after decont. */
#define MVM_SPESH_FACT_HASH_ITER            512 /* Is an iter over hashes. */
#define MVM_SPESH_FACT_ARRAY_ITER           1024 /* Is an iter over arrays
                                                    (mutually exclusive with HASH_ITER, but neither of them is necessarily set) */
#define MVM_SPESH_FACT_KNOWN_BOX_SRC        2048 /* We know what register this value was boxed from */
#define MVM_SPESH_FACT_RW_CONT              8192 /* Known to be an rw container */

void MVM_spesh_facts_discover(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshPlanned *p,
    MVMuint32 is_specialized);
void MVM_spesh_facts_depend(MVMThreadContext *tc, MVMSpeshGraph *g,
    MVMSpeshFacts *target, MVMSpeshFacts *source);
void MVM_spesh_facts_object_facts(MVMThreadContext *tc, MVMSpeshGraph *g,
    MVMSpeshOperand tgt, MVMObject *obj);
