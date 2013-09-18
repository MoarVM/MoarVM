/* A GC sync point is a point where we can check if we're being signalled
 * to stop to do a GC run. This is placed at points where it is safe to
 * do such a thing, and hopefully so that it happens often enough; note
 * that every call down to the allocator is also a sync point, so this
 * really only means we need to do this enough to make sure tight native
 * loops trigger it. */
/* Don't use a MVM_load(&tc->gc_status) here for performance, it's okay
 * if the interrupt is delayed a bit. */
#define GC_SYNC_POINT(tc) \
    if (tc->gc_status) { \
        MVM_gc_enter_from_interrupt(tc); \
    }

/* Different views of a register. */
union MVMRegister {
    MVMObject         *o;
    MVMString *s;
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
};

/* Most operands an operation will have. */
#define MVM_MAX_OPERANDS 8

/* Information about an opcode. */
struct MVMOpInfo {
    MVMuint16   opcode;
    const char *name;
    char        mark[2];
    MVMuint8    num_operands;
    MVMuint8    operands[MVM_MAX_OPERANDS];
};

/* Operand read/write/literal flags. */
#define MVM_operand_literal     0
#define MVM_operand_read_reg    1
#define MVM_operand_write_reg   2
#define MVM_operand_read_lex    3
#define MVM_operand_write_lex   4
#define MVM_operand_rw_mask     7

/* Register data types. */
#define MVM_reg_int8            1
#define MVM_reg_int16           2
#define MVM_reg_int32           3
#define MVM_reg_int64           4
#define MVM_reg_num32           5
#define MVM_reg_num64           6
#define MVM_reg_str             7
#define MVM_reg_obj             8

/* Operand data types. */
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
#define MVM_operand_coderef     (12 << 3)
#define MVM_operand_callsite    (13 << 3)
#define MVM_operand_type_mask   (15 << 3)

/* Functions. */
void MVM_interp_run(MVMThreadContext *tc, void (*initial_invoke)(MVMThreadContext *, void *), void *invoke_data);
MVM_PUBLIC void MVM_interp_enable_tracing();
