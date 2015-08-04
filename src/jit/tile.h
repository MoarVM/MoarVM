
typedef void (*MVMJitTileRule)(MVMThreadContext *tc, Dst_DECL);

struct MVMJitTile {
    MVMJitTileRule rule;
    MVMint32      state;
};
typedef struct MVMJitTile MVMJitTile;
