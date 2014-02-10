/* Top level of a spesh graph, representing a particular static frame (and
 * potentially having others inlined into it). */
struct MVMSpeshGraph {
    /* The static frame this is the spesh graph for. */
    MVMStaticFrame *sf;

    /* The entry basic block. */
    MVMSpeshBB *entry;

    /* Memory blocks we allocate to store spesh nodes, and which we free along
     * with the graph. Contains a link to previous blocks. */
    MVMSpeshMemBlock *mem_block;

    /* Number of basic blocks we have. */
    MVMint32 num_bbs;
};

/* The default allocation chunk size for memory blocks used to store spesh
 * graph nodes. Power of two is best. */
#define MVM_SPESH_MEMBLOCK_SIZE 32768

/* A block of bump-pointer allocated memory. */
struct MVMSpeshMemBlock {
    /* The memory buffer itself. */
    char *buffer;

    /* Current allocation position. */
    char *alloc;

    /* Allocation limit. */
    char *limit;

    /* Previous, now full, memory block. */
    MVMSpeshMemBlock *prev;
};

/* A basic block in the graph (sequences of instructions where control will
 * always enter at the start and leave at the end). */
struct MVMSpeshBB {
    /* Head/tail of doubly linked list of instructions. */
    MVMSpeshIns *first_ins;
    MVMSpeshIns *last_ins;

    /* Basic blocks we may go to after this one. */
    MVMSpeshBB **succ;
    MVMuint16    num_succ;

    /* The next basic block in original linear code order. */
    MVMSpeshBB *linear_next;

    /* Index (just an ascending integer along the linear_next chain), used as
     * the block identifier in dominance computation and for debug output. */
    MVMint32 idx;
};

/* An instruction in the spesh graph. */
struct MVMSpeshIns {
    /* Instruction information. */
    MVMOpInfo *info;

    /* Operand information. */
    MVMSpeshOperand *operands;

    /* Previous and next instructions, within a basic block boundary. */
    MVMSpeshIns *prev;
    MVMSpeshIns *next;
};

/* Union type of operands in a spesh instruction; the op info and phase of the
 * optimizer we're in determines which of these we look at. */
union MVMSpeshOperand {
    MVMint64     lit_i64;
    MVMint32     lit_i32;
    MVMint16     lit_i16;
    MVMint8      lit_i8;
    MVMnum64     lit_n64;
    MVMnum32     lit_n32;
    MVMString   *lit_str;
    MVMCallsite *callsite;
    MVMCode     *coderef;
    MVMuint32    ins_offset;
    MVMSpeshBB  *ins_bb;
    MVMuint16    reg_orig;
    MVMuint32    reg_ssa;
};

/* Functions to create/destory the spesh graph. */
MVMSpeshGraph * MVM_spesh_graph_create(MVMThreadContext *tc, MVMStaticFrame *sf);
void MVM_spesh_graph_destroy(MVMThreadContext *tc, MVMSpeshGraph *g);
