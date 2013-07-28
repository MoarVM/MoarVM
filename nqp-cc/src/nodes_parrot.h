/* MAST::CompUnit */
typedef struct {
    PMC    *st;
    PMC    *sc;
    PMC    *frames;
    STRING *hll;
    PMC    *main_frame;
    PMC    *load_frame;
    PMC    *deserialize_frame;
    PMC    *sc_handles;
} MAST_CompUnit;

/* MAST::Frame */
typedef struct {
    PMC    *st;
    PMC    *sc;
    STRING *cuuid;
    STRING *name;
    PMC    *lexical_types;
    PMC    *lexical_names;
    PMC    *local_types;
    PMC    *instructions;
    PMC    *outer;
} MAST_Frame;

/* MAST::Op */
typedef struct {
    PMC    *st;
    PMC    *sc;
    INTVAL  bank;
    INTVAL  op;
    PMC    *operands;
} MAST_Op;

/* MAST::SVal */
typedef struct {
    PMC    *st;
    PMC    *sc;
    STRING *value;
} MAST_SVal;

/* MAST::IVal */
typedef struct {
    PMC    *st;
    PMC    *sc;
    INTVAL  value;
} MAST_IVal;

/* MAST::NVal */
typedef struct {
    PMC      *st;
    PMC      *sc;
    FLOATVAL  value;
} MAST_NVal;

/* MAST::Label */
typedef struct {
    PMC    *st;
    PMC    *sc;
    STRING *name;
} MAST_Label;

/* MAST::Local */
typedef struct {
    PMC    *st;
    PMC    *sc;
    INTVAL  index;
} MAST_Local;

/* MAST::Lexical */
typedef struct {
    PMC    *st;
    PMC    *sc;
    INTVAL  index;
    INTVAL  frames_out;
} MAST_Lexical;

/* MAST::Call */
typedef struct {
    PMC    *st;
    PMC    *sc;
    PMC    *target;
    PMC    *flags;
    PMC    *args;
    PMC    *result;
} MAST_Call;

/* MAST::Annotated */
typedef struct {
    PMC    *st;
    PMC    *sc;
    STRING *file;
    INTVAL  line;
    PMC    *instructions;
} MAST_Annotated;

/* MAST::HandlerScope */
typedef struct {
    PMC    *st;
    PMC    *sc;
    PMC    *instructions;
    INTVAL  category_mask;
    INTVAL  action;
    PMC    *goto_label;
    PMC    *block_local;
} MAST_HandlerScope;

/* Node types structure. */
typedef struct {
    PMC *CompUnit;
    PMC *Frame;
    PMC *Op;
    PMC *SVal;
    PMC *IVal;
    PMC *NVal;
    PMC *Label;
    PMC *Local;
    PMC *Lexical;
    PMC *Call;
    PMC *Annotated;
    PMC *HandlerScope;
} MASTNodeTypes;

/* This means we can talk about MASTNode in the compiler, not PMC. */
typedef PMC MASTNode;

/* Similar for strings. */
typedef STRING VMSTR;

/* Way of talking about the interpreter. */
#define VM PARROT_INTERP
#define vm interp

/* Some macros for getting at and examining nodes data. */
#define ISTYPE(VM, s, t)            (STABLE(s)->type_check(VM, s, t))
#define DIE(vm, msg, ...)           Parrot_ex_throw_from_c_args(interp, NULL, EXCEPTION_INVALID_OPERATION, msg, ## __VA_ARGS__)
#define GET_CompUnit(n)             ((MAST_CompUnit *)PMC_data(n))
#define GET_Frame(n)                ((MAST_Frame *)PMC_data(n))
#define GET_Op(n)                   ((MAST_Op *)PMC_data(n))
#define GET_Label(n)                ((MAST_Label *)PMC_data(n))
#define GET_Local(n)                ((MAST_Local *)PMC_data(n))
#define GET_Lexical(n)              ((MAST_Lexical *)PMC_data(n))
#define GET_IVal(n)                 ((MAST_IVal *)PMC_data(n))
#define GET_NVal(n)                 ((MAST_NVal *)PMC_data(n))
#define GET_SVal(n)                 ((MAST_SVal *)PMC_data(n))
#define GET_Call(n)                 ((MAST_Call *)PMC_data(n))
#define GET_Annotated(n)            ((MAST_Annotated *)PMC_data(n))
#define GET_HandlerScope(n)         ((MAST_HandlerScope *)PMC_data(n))
#define NEWLIST_I(vm)               (Parrot_pmc_new(interp, enum_class_ResizableIntegerArray))
#define NEWLIST_S(vm)               (Parrot_pmc_new(interp, enum_class_ResizableStringArray))
#define ELEMS(vm, arr)              ((unsigned int )VTABLE_elements(vm, arr))
#define ATPOS(vm, arr, i)           (VTABLE_get_pmc_keyed_int(vm, arr, i))
#define ATPOS_I(vm, arr, i)         (VTABLE_get_integer_keyed_int(vm, arr, i))
#define ATPOS_S(vm, arr, i)         (VTABLE_get_string_keyed_int(vm, arr, i))
#define BINDPOS(vm, arr, i, v)      (VTABLE_set_pmc_keyed_int(vm, arr, i, v))
#define BINDPOS_I(vm, arr, i, v)    (VTABLE_set_integer_keyed_int(vm, arr, i, v))
#define BINDPOS_S(vm, arr, i, v)    (VTABLE_set_string_keyed_int(vm, arr, i, v))
#define NEWHASH(vm)                 (Parrot_pmc_new(interp, enum_class_Hash))
#define HASHELEMS(vm, hash)         ((unsigned int )VTABLE_elements(vm, hash))
#define ATKEY(vm, hash, k)          (VTABLE_get_pmc_keyed_str(vm, hash, k))
#define ATKEY_I(vm, hash, k)        (VTABLE_get_integer_keyed_str(vm, hash, k))
#define BINDKEY(vm, hash, k, v)     (VTABLE_set_pmc_keyed_str(vm, hash, k, v))
#define BINDKEY_I(vm, hash, k, v)   (VTABLE_set_integer_keyed_str(vm, hash, k, v))
#define EXISTSKEY(vm, hash, k)      (VTABLE_exists_keyed_str(vm, hash, k))
#define DELETEKEY(vm, hash, k)      (VTABLE_delete_keyed_str(vm, hash, k))
#define EMPTY_STRING(vm)            (Parrot_str_new_constant(interp, ""))
#define VM_STRING_IS_NULL(s)        STRING_IS_NULL(s)
#define VM_OBJ_IS_NULL(o)           PMC_IS_NULL(o)

/* Copies of MVM operand read/write/literal flags. */
#define MVM_operand_literal     0
#define MVM_operand_read_reg    1
#define MVM_operand_write_reg   2
#define MVM_operand_read_lex    3
#define MVM_operand_write_lex   4
#define MVM_operand_rw_mask     7

/* Copies of MVM register data types. */
#define MVM_reg_int8            1
#define MVM_reg_int16           2
#define MVM_reg_int32           3
#define MVM_reg_int64           4
#define MVM_reg_num32           5
#define MVM_reg_num64           6
#define MVM_reg_str             7
#define MVM_reg_obj             8

/* Copies of MVM operand data types. */
#define MVM_operand_int8        (MVM_reg_int8 << 3)
#define MVM_operand_int16       (MVM_reg_int16 << 3)
#define MVM_operand_int32       (MVM_reg_int32 << 3)
#define MVM_operand_int64       (MVM_reg_int64 << 3)
#define MVM_operand_num32       (MVM_reg_num32 << 3)
#define MVM_operand_num64       (MVM_reg_num64 << 3)
#define MVM_operand_str         (MVM_reg_str << 3)
#define MVM_operand_obj         (MVM_reg_obj << 3)
#define MVM_operand_ins         (9 << 3)
#define MVM_operand_type_var    (10 << 3)
#define MVM_operand_lex_outer   (11 << 3)
#define MVM_operand_coderef     (12 << 3)
#define MVM_operand_callsite    (13 << 3)
#define MVM_operand_type_mask   (15 << 3)

/* Most operands an operation will have. */
#define MVM_MAX_OPERANDS 8

/* Information about an opcode. */
typedef struct _MVMOpInfo {
    unsigned char  opcode;
    const char    *name;
    unsigned char  num_operands;
    unsigned char  operands[MVM_MAX_OPERANDS];
} MVMOpInfo;

/* Copy of callsite argument flags. */
typedef enum {
    /* Argument is an object. */
    MVM_CALLSITE_ARG_OBJ = 1,

    /* Argument is a native integer, signed. */
    MVM_CALLSITE_ARG_INT = 2,

    /* Argument is a native floating point number. */
    MVM_CALLSITE_ARG_NUM = 4,

    /* Argument is a native NFG string (MVMString REPR). */
    MVM_CALLSITE_ARG_STR = 8,

    /* Argument is named; in this case, there are two entries in
     * the argument list, the first a MVMString naming the arg and
     * after that the arg. */
    MVM_CALLSITE_ARG_NAMED = 32,

    /* Argument is flattened. What this means is up to the target. */
    MVM_CALLSITE_ARG_FLAT = 64,

    /* Argument is flattened and named. */
    MVM_CALLSITE_ARG_FLAT_NAMED = 128
} MVMCallsiteFlags;

/* Type mappings. */
#define MVMStorageSpec      storage_spec

/* Constant mappings. */
#define MVM_STORAGE_SPEC_BP_INT     STORAGE_SPEC_BP_INT
#define MVM_STORAGE_SPEC_BP_NUM     STORAGE_SPEC_BP_NUM
#define MVM_STORAGE_SPEC_BP_STR     STORAGE_SPEC_BP_STR

/* Copy parrot's endianness */
#ifdef PARROT_BIGENDIAN
 #if PARROT_BIGENDIAN
  #define MVM_BIGENDIAN       1
 #endif
#endif
