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
