#define MVMPhiNodeCacheSize             48
#define MVMPhiNodeCacheSparseBegin      32

/* Top level of a spesh graph, representing a particular static frame (and
 * potentially having others inlined into it). */
struct MVMSpeshGraph {
    /* The static frame this is the spesh graph for. */
    MVMStaticFrame *sf;

    /* The callsite this spesh graph has been tailored to. */
    MVMCallsite *cs;

    /* The bytecode we're building the graph out of. */
    MVMuint8 *bytecode;

    /* Exception handler map for that bytecode. */
    MVMFrameHandler *handlers;

    /* The size of the bytecode we're building the graph out of. */
    MVMuint32 bytecode_size;

    /* Number of exception handlers. */
    MVMuint32 num_handlers;

    /* The entry basic block. */
    MVMSpeshBB *entry;

    /* Gathered facts about each version of a local (top-level array is per
     * local, then array hanging off it is per version). */
    MVMSpeshFacts **facts;

    /* Number of fact entries per local. */
    MVMuint16 *fact_counts;

    /* Argument guards added. */
    MVMSpeshGuard *arg_guards;

    /* Number of argument guards we have. */
    MVMint32 num_arg_guards;

    /* Log-based guards added. */
    MVMSpeshLogGuard *log_guards;

    /* Number of log-based guards we have. */
    MVMint32 num_log_guards;

    /* Memory blocks we allocate to store spesh nodes, and which we free along
     * with the graph. Contains a link to previous blocks. */
    MVMSpeshMemBlock *mem_block;

    /* Values placed in spesh slots. */
    MVMCollectable **spesh_slots;

    /* Number of spesh slots we have used and allocated. */
    MVMint32 num_spesh_slots;
    MVMint32 alloc_spesh_slots;

    /* De-opt indexes, as pairs of integers. The first integer, set when we
     * build the graph, is the return address in the original bytecode. The
     * code-gen phase for the specialized bytecode will fill in the second
     * integers afterwards, which are the return address in the specialized
     * bytecode. */
    MVMint32 *deopt_addrs;
    MVMint32  num_deopt_addrs;
    MVMint32  alloc_deopt_addrs;

    /* Table of information about inlines, laid out in order of nesting
     * depth. Thus, going through the table in order and finding when we
     * are within the bounds will show up each call frame that needs to
     * be created in deopt. */
    MVMSpeshInline *inlines;
    MVMint32 num_inlines;

    /* Logging slots, along with the number of them. */
    MVMint32 num_log_slots;
    MVMCollectable **log_slots;

    /* Number of basic blocks we have. */
    MVMint32 num_bbs;

    /* The list of local types (only set up if we do inlines). */
    MVMuint16 *local_types;

    /* The list of lexical types (only set up if we do inlines). */
    MVMuint16 *lexical_types;

    /* The total number of locals, accounting for any inlining done and
     * added temporaries. */
    MVMuint16 num_locals;

    /* The total number of lexicals, accounting for any inlining done. */
    MVMuint16 num_lexicals;

    /* Temporary local registers added to aid transformations, along with a
     * count of the number we have and have allocated space for so far. */
    MVMuint16          num_temps;
    MVMuint16          alloc_temps;
    MVMSpeshTemporary *temps;

    /* We need to create new MVMOpInfo structs for each number of
     * arguments a PHI node can take. We cache them here, so that we
     * allocate fewer of them across our spesh alloc blocks.
     */
    MVMOpInfo *phi_infos;
};

/* The default allocation chunk size for memory blocks used to store spesh
 * graph nodes. Power of two is best; we start small also. */
#define MVM_SPESH_FIRST_MEMBLOCK_SIZE 32768
#define MVM_SPESH_MEMBLOCK_SIZE       8192

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

/* A temporary register, added to support transformations. */
struct MVMSpeshTemporary {
    /* The number of the local along with the current SSA index. */
    MVMuint16 orig;
    MVMuint16 i;

    /* What kind of register is it? */
    MVMuint16 kind;

    /* Is it currently in use? */
    MVMuint16 in_use;
};

/* A basic block in the graph (sequences of instructions where control will
 * always enter at the start and leave at the end). */
struct MVMSpeshBB {
    /* Head/tail of doubly linked list of instructions. */
    MVMSpeshIns *first_ins;
    MVMSpeshIns *last_ins;

    /* Basic blocks we may go to after this one. */
    MVMSpeshBB **succ;

    /* Basic blocks that we may arrive into this one from. */
    MVMSpeshBB **pred;

    /* Children in the dominator tree. */
    MVMSpeshBB **children;

    /* Dominance frontier set. */
    MVMSpeshBB **df;

    /* Counts for the above, grouped together to avoid alignment holes. */
    MVMuint16    num_succ;
    MVMuint16    num_pred;
    MVMuint16    num_children;
    MVMuint16    num_df;

    /* The next basic block in original linear code order. */
    MVMSpeshBB *linear_next;

    /* Index (just an ascending integer along the linear_next chain), used as
     * the block identifier in dominance computation and for debug output. */
    MVMint32 idx;

    /* The block's reverse post-order index, assinged when computing
     * dominance. */
    MVMint32 rpo_idx;

    /* We cache the instruction pointer of the very first instruction so that
     * we can output a line number for every BB */
    MVMuint32 initial_pc;

    /* Is this block an inlining of another one? */
    MVMint32 inlined;
};

/* The SSA phi instruction. */
#define MVM_SSA_PHI 32767

/* An instruction in the spesh graph. */
struct MVMSpeshIns {
    /* Instruction information. */
    const MVMOpInfo *info;

    /* Operand information. */
    MVMSpeshOperand *operands;

    /* Previous and next instructions, within a basic block boundary. */
    MVMSpeshIns *prev;
    MVMSpeshIns *next;

    /* Any annotations on the instruction. */
    MVMSpeshAnn *annotations;
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
    MVMuint32    lit_str_idx;
    MVMuint16    callsite_idx;
    MVMuint16    coderef_idx;
    MVMuint32    ins_offset;
    MVMSpeshBB  *ins_bb;
    struct {
        MVMuint16 idx;
        MVMuint16 outers;
    } lex;
    struct {
        MVMint32  i;    /* SSA-computed version. */
        MVMuint16 orig; /* Original register number. */
    } reg;
};

/* Annotations base. */
struct MVMSpeshAnn {
    /* The next annotation in the chain, if any. */
    MVMSpeshAnn *next;

    /* The type of annotation we have. */
    MVMint32 type;

    /* Data (meaning depends on type). */
    union {
        MVMint32 frame_handler_index;
        MVMint32 deopt_idx;
        MVMint32 inline_idx;
    } data;
};

/* Annotation types. */
#define MVM_SPESH_ANN_FH_START      1
#define MVM_SPESH_ANN_FH_END        2
#define MVM_SPESH_ANN_FH_GOTO       3
#define MVM_SPESH_ANN_DEOPT_ONE_INS 4
#define MVM_SPESH_ANN_DEOPT_ALL_INS 5
#define MVM_SPESH_ANN_INLINE_START  6
#define MVM_SPESH_ANN_INLINE_END    7
#define MVM_SPESH_ANN_DEOPT_INLINE  8
#define MVM_SPESH_ANN_DEOPT_OSR     9

/* Functions to create/destory the spesh graph. */
MVMSpeshGraph * MVM_spesh_graph_create(MVMThreadContext *tc, MVMStaticFrame *sf, MVMuint32 cfg_only);
MVMSpeshGraph * MVM_spesh_graph_create_from_cand(MVMThreadContext *tc, MVMStaticFrame *sf,
    MVMSpeshCandidate *cand, MVMuint32 cfg_only);
MVMSpeshBB * MVM_spesh_graph_linear_prev(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *search);
void MVM_spesh_graph_mark(MVMThreadContext *tc, MVMSpeshGraph *g, MVMGCWorklist *worklist);
void MVM_spesh_graph_destroy(MVMThreadContext *tc, MVMSpeshGraph *g);
MVM_PUBLIC void * MVM_spesh_alloc(MVMThreadContext *tc, MVMSpeshGraph *g, size_t bytes);
MVMOpInfo *get_phi(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint32 nrargs);
