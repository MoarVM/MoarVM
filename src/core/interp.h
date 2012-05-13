/* A GC sync point is a point where we can check if we're being signalled
 * to stop to do a GC run. This is placed at points where it is safe to
 * do such a thing, and hopefully so that it happens often enough; note
 * that every call down to the allocator is also a sync point, so this
 * really only means we need to do this enough to make sure tight native
 * loops trigger it. */
#define GC_SYNC_POINT(tc) \
    if (tc->interupt) { \
    }

/* Different views of a register. */
typedef union _MVMRegister {
    MVMObject         *o;
    struct _MVMString *s;
    MVMint8            i8;
    MVMuint8           ui8;
    MVMint16           i16;
    MVMuint16          ui16;
    MVMint32           i32;
    MVMuint32          ui32;
    MVMint64           i64;
    MVMuint64          ui64;
    MVMnum32           n32;
    MVMnum64           n64;
} MVMRegister;

/* Most operands an operation will have. */
#define MVM_MAX_OPERANDS 8

/* Information about an opcode. */
typedef struct _MVMOpInfo {
    MVMuint8    opcode;
    const char *name;
    MVMuint8    num_operands;
    MVMuint8    operands[MVM_MAX_OPERANDS];
} MVMOpInfo;

/* Operand read/write/literal flags. */
#define MVM_operand_literal     0
#define MVM_operand_read_reg    1
#define MVM_operand_write_reg   2
#define MVM_operand_read_lex    3
#define MVM_operand_write_lex   4
#define MVM_operand_rw_mask     7

/* Operand data types. */
#define MVM_operand_ins         (1 << 3)
#define MVM_operand_int8        (2 << 3)
#define MVM_operand_int16       (3 << 3)
#define MVM_operand_int32       (4 << 3)
#define MVM_operand_int64       (5 << 3)
#define MVM_operand_num32       (6 << 3)
#define MVM_operand_num64       (7 << 3)
#define MVM_operand_str         (8 << 3)
#define MVM_operand_obj         (9 << 3)
#define MVM_operand_type_var    (10 << 3)
#define MVM_operand_lex_outer   (11 << 3)
#define MVM_operand_type_mask   (15 << 3)

/* Functions. */
void MVM_interp_run(MVMThreadContext *tc, struct _MVMFrame *initial_frame);
