/* MAST::CompUnit */
typedef struct {
    MVMP6opaque  p6o_header;
    MVMObject   *frames;
    MVMString   *hll;
    MVMObject   *main_frame;
    MVMObject   *load_frame;
    MVMObject   *deserialize_frame;
    MVMObject   *sc_handles;
    MVMObject   *sc_lookup;
    MVMObject   *extop_sigs;
    MVMObject   *extop_idx;
    MVMObject   *extop_names;
} MAST_CompUnit;

/* MAST::Frame */
typedef struct {
    MVMP6opaque  p6o_header;
    MVMString   *cuuid;
    MVMString   *name;
    MVMObject   *lexical_types;
    MVMObject   *lexical_names;
    MVMObject   *local_types;
    MVMObject   *instructions;
    MVMObject   *outer;
    MVMObject   *lexical_map;
    MVMint64     flags;
    MVMint64     index;
    MVMObject   *static_lex_values;
    MVMint64     code_obj_sc_dep_idx;
    MVMint64     code_obj_sc_idx;
} MAST_Frame;

/* MAST::Op */
typedef struct {
    MVMP6opaque  p6o_header;
    MVMint64     op;
    MVMObject   *operands;
} MAST_Op;

/* MAST::ExtOp */
typedef struct {
    MVMP6opaque  p6o_header;
    MVMint64     op;
    MVMObject   *operands;
    MVMString   *name;
} MAST_ExtOp;

/* MAST::SVal */
typedef struct {
    MVMP6opaque  p6o_header;
    MVMString   *value;
} MAST_SVal;

/* MAST::IVal */
typedef struct {
    MVMP6opaque p6o_header;
    MVMint64    value;
} MAST_IVal;

/* MAST::NVal */
typedef struct {
    MVMP6opaque p6o_header;
    MVMnum64    value;
} MAST_NVal;

/* MAST::Label */
typedef struct {
    MVMP6opaque  p6o_header;
} MAST_Label;

/* MAST::Local */
typedef struct {
    MVMP6opaque p6o_header;
    MVMint64    index;
} MAST_Local;

/* MAST::Lexical */
typedef struct {
    MVMP6opaque p6o_header;
    MVMint64    index;
    MVMint64    frames_out;
} MAST_Lexical;

/* MAST::Call */
typedef struct {
    MVMP6opaque  p6o_header;
    MVMObject   *target;
    MVMObject   *flags;
    MVMObject   *args;
    MVMObject   *result;
    MVMint64     op;
} MAST_Call;

/* MAST::Annotated */
typedef struct {
    MVMP6opaque  p6o_header;
    MVMString   *file;
    MVMint64     line;
    MVMObject   *instructions;
} MAST_Annotated;

/* MAST::HandlerScope */
typedef struct {
    MVMP6opaque  p6o_header;
    MVMObject   *instructions;
    MVMint64     category_mask;
    MVMint64     action;
    MVMObject   *goto_label;
    MVMObject   *block_local;
    MVMObject   *label_local;
} MAST_HandlerScope;

/* Node types structure. */
typedef struct _MASTNodeTypes {
    MVMObject *CompUnit;
    MVMObject *Frame;
    MVMObject *Op;
    MVMObject *ExtOp;
    MVMObject *SVal;
    MVMObject *IVal;
    MVMObject *NVal;
    MVMObject *Label;
    MVMObject *Local;
    MVMObject *Lexical;
    MVMObject *Call;
    MVMObject *Annotated;
    MVMObject *HandlerScope;
} MASTNodeTypes;

/* This means we can talk about MASTNode in the compiler, not MVMObject. */
typedef MVMObject MASTNode;

/* Similar for strings. */
typedef MVMString VMSTR;

/* Way of talking about the interpreter. */
#define VM MVMThreadContext *tc
#define vm tc

/* Some macros for getting at and examining nodes data. */
#define ISTYPE(VM, s, t)            (STABLE(s) == STABLE(t))
#define DIE(vm, msg, ...)           MVM_exception_throw_adhoc(tc, msg, ## __VA_ARGS__)
#define GET_CompUnit(n)             ((MAST_CompUnit *)n)
#define GET_Frame(n)                ((MAST_Frame *)n)
#define GET_Op(n)                   ((MAST_Op *)n)
#define GET_ExtOp(n)                ((MAST_ExtOp *)n)
#define GET_Label(n)                ((MAST_Label *)n)
#define GET_Local(n)                ((MAST_Local *)n)
#define GET_Lexical(n)              ((MAST_Lexical *)n)
#define GET_IVal(n)                 ((MAST_IVal *)n)
#define GET_NVal(n)                 ((MAST_NVal *)n)
#define GET_SVal(n)                 ((MAST_SVal *)n)
#define GET_Call(n)                 ((MAST_Call *)n)
#define GET_Annotated(n)            ((MAST_Annotated *)n)
#define GET_HandlerScope(n)         ((MAST_HandlerScope *)n)
#define NEWLIST_I(vm)               (MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIntArray))
#define NEWLIST_S(vm)               (MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStrArray))
#define ELEMS(vm, arr)              ((unsigned int )MVM_repr_elems(vm, arr))
#define ATPOS(vm, arr, i)           (MVM_repr_at_pos_o(vm, arr, i))
#define ATPOS_I(vm, arr, i)         (MVM_repr_at_pos_i(vm, arr, i))
#define ATPOS_S(vm, arr, i)         (MVM_repr_at_pos_s(vm, arr, i))
#define ATPOS_I_C(vm, arr, i)       (MVM_repr_get_int(vm, MVM_repr_at_pos_o(vm, arr, i)))
#define ATPOS_S_C(vm, arr, i)       (MVM_repr_get_str(vm, MVM_repr_at_pos_o(vm, arr, i)))
#define BINDPOS(vm, arr, i, v)      (MVM_repr_bind_pos_o(vm, arr, i, v))
#define BINDPOS_I(vm, arr, i, v)    (MVM_repr_bind_pos_i(vm, arr, i, v))
#define BINDPOS_S(vm, arr, i, v)    (MVM_repr_bind_pos_s(vm, arr, i, v))
#define NEWHASH(vm)                 (MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash))
#define HASHELEMS(vm, hash)         ((unsigned int )MVM_repr_elems(vm, hash))
#define ATKEY(vm, hash, k)          (MVM_repr_at_key_o(vm, hash, k))
#define ATKEY_I(vm, hash, k)        (MVM_repr_get_int(tc, MVM_repr_at_key_o(vm, hash, k)))
#define BINDKEY(vm, hash, k, v)     (MVM_repr_bind_key_o(vm, hash, k, v))
#define BINDKEY_I(vm, hash, k, v)   do {                                          \
    MVMObject *boxed = MVM_repr_box_int(tc, tc->instance->boot_types.BOOTInt, v); \
    MVM_repr_bind_key_o(vm, hash, k, boxed);                                      \
} while (0)
#define EXISTSKEY(vm, hash, k)      (MVM_repr_exists_key(vm, hash, k))
#define DELETEKEY(vm, hash, k)      (MVM_repr_delete_key(vm, hash, k))
#define EMPTY_STRING(vm)            (tc->instance->str_consts.empty)
#define VM_STRING_IS_NULL(s)        (s == NULL)
#define VM_OBJ_IS_NULL(o)           (o == NULL)
#define VM_STRING_TO_C_STRING(vm, s) (MVM_string_ascii_encode_any(tc, s))

#define DIE_FREE(vm, waste, msg, ...) MVM_exception_throw_adhoc_free(tc, waste, msg, ## __VA_ARGS__)
