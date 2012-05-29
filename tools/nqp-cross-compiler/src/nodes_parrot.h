/* MAST::CompUnit */
typedef struct {
    PMC    *st;
    PMC    *sc;
    PMC    *reprs;
    PMC    *scs;
    PMC    *frames;
    PMC    *callsites;
} MAST_CompUnit;

/* MAST::Frame */
typedef struct {
    PMC    *st;
    PMC    *sc;
    PMC    *lexical_types;
    PMC    *lexical_names;
    PMC    *local_types;
    PMC    *instructions;
} MAST_Frame;

/* MAST::Op */
typedef struct {
    PMC    *st;
    PMC    *sc;
    INTVAL  bank;
    INTVAL  op;
    PMC    *operands;
} MAST_Op;

/* MAST::StrLit */
typedef struct {
    PMC    *st;
    PMC    *sc;
    STRING *value;
} MAST_StrLit;

/* MAST::IntLit */
typedef struct {
    PMC    *st;
    PMC    *sc;
    INTVAL  value;
} MAST_IntLit;

/* MAST::NumLit */
typedef struct {
    PMC      *st;
    PMC      *sc;
    FLOATVAL  value;
} MAST_NumLit;

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

/* XXX MAST::Call and MAST::CallMethod todo. */

/* Node types structure. */
typedef struct {
    PMC *CompUnit;
    PMC *Frame;
    PMC *Op;
    PMC *StrLit;
    PMC *IntLit;
    PMC *NumLit;
    PMC *Label;
    PMC *Local;
    PMC *Lexical;
} MASTNodeTypes;

/* This means we can talk about MASTNode in the compiler, not PMC. */
typedef PMC MASTNode;

/* Way of talking about the interpreter. */
#define VM PARROT_INTERP
#define vm interp

/* Some macros for getting at and examining nodes data. */
#define ISTYPE(VM, s, t)    (STABLE(s)->type_check(VM, s, t))
#define DIE(vm, msg)        Parrot_ex_throw_from_c_args(interp, NULL, EXCEPTION_INVALID_OPERATION, msg)
#define GET_CompUnit(n)     ((MAST_CompUnit *)PMC_data(n))
#define GET_Frame(n)        ((MAST_Frame *)PMC_data(n))
#define GET_Op(n)           ((MAST_Op *)PMC_data(n))
#define ELEMS(vm, arr)      ((unsigned int )VTABLE_elements(vm, arr))
#define ATPOS(vm, arr, i)   (VTABLE_get_pmc_keyed_int(vm, arr, i))

/* Copies of register data type constants. */
#define MVM_reg_int8        1
#define MVM_reg_int16       2
#define MVM_reg_int32       3
#define MVM_reg_int64       4
#define MVM_reg_num32       5
#define MVM_reg_num64       6
#define MVM_reg_str         7
#define MVM_reg_obj         8

/* Type mappings. */
#define MVMStorageSpec      storage_spec
