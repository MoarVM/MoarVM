/* Callsite argument flags. */
#define MVM_CALLSITE_ARG_TYPE_MASK       15
#define MVM_CALLSITE_ARG_NAMED_FLAT_MASK 31
typedef enum {
    /* Argument is an object. */
    MVM_CALLSITE_ARG_OBJ = 1,

    /* Argument is a native integer, signed. */
    MVM_CALLSITE_ARG_INT = 2,

    /* Argument is a native floating point number. */
    MVM_CALLSITE_ARG_NUM = 4,

    /* Argument is a native NFG string (MVMString REPR). */
    MVM_CALLSITE_ARG_STR = 8,

    /* Argument is a literal. */
    MVM_CALLSITE_ARG_LITERAL = 16,

    /* Argument is named. The name is placed in the MVMCallsite. */
    MVM_CALLSITE_ARG_NAMED = 32,

    /* Argument is flattened. What this means is up to the target. */
    MVM_CALLSITE_ARG_FLAT = 64,

    /* Argument is flattened and named. */
    MVM_CALLSITE_ARG_FLAT_NAMED = 128
} MVMCallsiteFlags;

typedef enum {
    /* Zero argument callsite. */
    MVM_CALLSITE_ID_NULL_ARGS,

    /* Dummy, invocant-arg callsite. Taken from coerce.c;
     * OBJ */
    MVM_CALLSITE_ID_INV_ARG,

    /* Callsite for container store. Taken from containers.c;
     * OBJ, OBJ */
    MVM_CALLSITE_ID_TWO_OBJ,

    /* Callsite for method not found errors. Taken from 6model.c;
     * OBJ, STR */
    MVM_CALLSITE_ID_METH_NOT_FOUND,

    /* Callsite for finding methods. Taken from 6model.c;
     * OBJ, OBJ, STR */
    MVM_CALLSITE_ID_FIND_METHOD,

    /* Callsite for typechecks. Taken from 6model.c;
     * OBJ, OBJ, OBJ */
    MVM_CALLSITE_ID_TYPECHECK,

    /* Callsite OBJ, INT */
    MVM_CALLSITE_ID_OBJ_INT,

    /* Callsite OBJ, INT */
    MVM_CALLSITE_ID_OBJ_NUM,

    /* Callsite OBJ, STR */
    MVM_CALLSITE_ID_OBJ_STR,

    /* Callsite INT, INT */
    MVM_CALLSITE_ID_INT_INT,
} MVMCommonCallsiteID;

/* A callsite entry is just one of the above flags. */
typedef MVMuint8 MVMCallsiteEntry;

/* A callsite is an argument count, a bunch of flags, and names of named
 * arguments (excluding any flattening ones). Note that it does not contain
 * the argument values; this is the *statically known* things about the
 * callsite and is immutable. It describes how to process the callsite
 * memory buffer. */
struct MVMCallsite {
    /* The set of flags. */
    MVMCallsiteEntry *arg_flags;

    /* The number of arg flags. */
    MVMuint16 flag_count;

    /* The total argument count (including 2 for each named arg). */
    MVMuint16 arg_count;

    /* Number of positionals, including flattening positionals but
     * excluding named positionals. */
    MVMuint16 num_pos;

    /* Whether it has any flattening args. */
    MVMuint8 has_flattening;

    /* Whether it has been interned (which means it is suitable for using in
     * specialization). */
    MVMuint8 is_interned;

    /* Cached version of this callsite with an extra invocant arg. */
    MVMCallsite *with_invocant;

    /* Names of named arguments, in the order that they are passed (and thus
     * matching the flags). Note that named flattening args do not have an
     * entry here, even though they come in the nameds section. */
    MVMString **arg_names;
};

/* Minimum callsite size is due to certain things internally expecting us to
 * have that many slots available (e.g. find_method(how, obj, name)). */
#define MVM_MIN_CALLSITE_SIZE 3

/* Maximum arity + 1 that we'll intern callsites by. */
#define MVM_INTERN_ARITY_LIMIT 8

/* Interned callsites data structure. */
struct MVMCallsiteInterns {
    /* Array of callsites, by arity. */
    MVMCallsite **by_arity[MVM_INTERN_ARITY_LIMIT];

    /* Number of callsites we have interned by arity. */
    MVMint32 num_by_arity[MVM_INTERN_ARITY_LIMIT];
};

/* Initialize the "common" callsites */
void MVM_callsite_initialize_common(MVMThreadContext *tc);

/* Get any of the "common" callsites */
MVM_PUBLIC MVMCallsite *MVM_callsite_get_common(MVMThreadContext *tc, MVMCommonCallsiteID id);

int MVM_callsite_is_common(MVMCallsite *cs);

void MVM_callsite_destroy(MVMCallsite *cs);

MVMCallsite *MVM_callsite_copy(MVMThreadContext *tc, const MVMCallsite *cs);

/* Callsite interning function. */
MVM_PUBLIC void MVM_callsite_try_intern(MVMThreadContext *tc, MVMCallsite **cs);

/* Callsite transformation function. */
MVMCallsite * MVM_callsite_drop_positional(MVMThreadContext *tc, MVMCallsite *cs, MVMuint32 idx);

/* Count the number of nameds (excluding flattening). */
MVM_STATIC_INLINE MVMuint16 MVM_callsite_num_nameds(MVMThreadContext *tc, const MVMCallsite *cs) {
    MVMuint16 i = cs->num_pos;
    MVMuint16 nameds = 0;
    while (i < cs->flag_count) {
        if (!(cs->arg_flags[i] & MVM_CALLSITE_ARG_FLAT_NAMED))
            nameds++;
        i++;
    }
    return nameds;
}
