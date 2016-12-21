struct MVMJitTileTemplate {
    void (*emit)(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTile *tile, MVMJitExprTree *tree);
    const char    *path;
    const char    *expr;
    MVMint32  left_sym;
    MVMint32 right_sym;

    MVMint32  num_values;
    MVMint32  value_bitmap;
    MVMJitExprVtype vtype;
    MVMuint32  register_spec;
};

struct MVMJitTile {
    const MVMJitTileTemplate *template;
    void (*emit)(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTile *tile, MVMJitExprTree *tree);
    MVMint32 node;
    MVMint32 num_values;
    /* buffers for the args of this (pseudo) tile */
    MVMJitValue *values[8];
    MVMJitExprNode args[8];
    MVMuint32 register_spec;
};


/* A tile I'm planning to insert into the list */
struct MVMJitTileInsert {
    MVMint32 position;
    MVMint32 order;
    MVMJitTile *tile;
};

/* A list of tiles representing a (part of a) routine */
struct MVMJitTileList {
    MVMJitExprTree *tree;
    MVM_VECTOR_DECL(MVMJitTile*, items);
    MVM_VECTOR_DECL(struct MVMJitTileInsert, inserts);
    /* TODO implement structures to mark basic blocks */
};




MVMJitTile     * MVM_jit_tile_make(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                   void *emit, MVMint32 node, MVMint32 nargs, ...);
MVMJitTileList * MVM_jit_tile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree);


void MVM_jit_tile_list_insert(MVMThreadContext *tc, MVMJitTileList *list, MVMJitTile *tile, MVMint32 position, MVMint32 order);
void MVM_jit_tile_list_edit(MVMThreadContext *tc, MVMJitTileList *list);
void MVM_jit_tile_list_destroy(MVMThreadContext *tc, MVMJitTileList *list);

#define MVM_JIT_TILE_YIELDS_VALUE(t) ((t)->register_spec & 1)
