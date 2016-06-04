#include "moar.h"
#include "internal.h"

#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64
static MVMint8 free_gpr[] = {
    X64_FREE_GPR(MVM_JIT_REGNAME)
};
static MVMint8 free_num[] = {
    X64_SSE(MVM_JIT_REGNAME)
};
#else
static MVMint8 free_gpr[] = { -1 };
static MVMint8 free_num[] = { -1 };
#endif

#define NUM_GPR sizeof(free_gpr)
#define NEXT_REG(x) (((x)+1)%NUM_GPR)


/* Register lock bitmap macros */
#define REGISTER_IS_LOCKED(a, n) ((a)->reg_lock &  (1 << (n)))
#define LOCK_REGISTER(a, n)   ((a)->reg_lock |=  (1 << (n)))
#define UNLOCK_REGISTER(a,n)  ((a)->reg_lock &= ~(1 << (n)))



void MVM_jit_register_allocator_init(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                     MVMJitRegisterAllocator *alc, MVMJitTileList *list) {
    /* Store live ranges */
    MVM_DYNAR_INIT(alc->active, NUM_GPR);
    /* Initialize free register buffer */
    alc->free_reg = MVM_malloc(sizeof(free_gpr));
    memcpy(alc->free_reg, free_gpr, NUM_GPR);

    alc->reg_use   = MVM_calloc(16, sizeof(MVMint8));

    alc->reg_give  = 0;
    alc->reg_take  = 0;

    alc->spill_top = 1;
    alc->reg_lock  = 0;

    alc->values_by_node = MVM_calloc(list->tree->nodes_num, sizeof(void*));

    compiler->allocator = alc;
}

void MVM_jit_register_allocator_deinit(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                       MVMJitRegisterAllocator *alc) {
    MVM_free(alc->active);
    MVM_free(alc->free_reg);
    MVM_free(alc->reg_use);
    MVM_free(alc->values_by_node);
    compiler->allocator = NULL;
}

#define NYI(x) MVM_oops(tc, #x " NYI");


MVMint8 MVM_jit_register_alloc(MVMThreadContext *tc, MVMJitCompiler *cl, MVMint32 reg_cls) {
    MVMJitRegisterAllocator *alc = cl->allocator;
    MVMint8 reg_num;
    if (reg_cls == MVM_JIT_STORAGE_FPR) {
        NYI(numeric_regs);
    } else {
        if (NEXT_REG(alc->reg_take) == alc->reg_give) {
            /* Out of registers, spill something */
            NYI(spill_something);
        }
        /* Use a circular handout scheme for the 'fair use' of registers */
        reg_num       = alc->free_reg[alc->reg_take];
        alc->free_reg[alc->reg_take] = 0xff; /* mark it for debugging purposes */
        alc->reg_take = NEXT_REG(alc->reg_take);
    }
    /* MVM_jit_log(tc, "Allocated register %d at order nr %d\n", reg_num, cl->order_nr); */
    return reg_num;
}


/* Freeing a register makes it available again */
void MVM_jit_register_free(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    if (reg_cls == MVM_JIT_STORAGE_FPR) {
        NYI(numeric_regs);
    } else {
        MVMint32 i;
        /* MVM_jit_log(tc, "Trying to free register %d\n", reg_num); */

        for (i = 0; i < sizeof(free_gpr); i++) {
            if (free_gpr[i] == reg_num)
                goto ok;
        }
        MVM_oops(tc, "This is not a free register!");
    ok:
        if (alc->reg_give == alc->reg_take) {
            MVM_oops(tc, "Trying to free too many registers");
        }
        alc->free_reg[alc->reg_give] = reg_num;
        alc->reg_give                = NEXT_REG(alc->reg_give);
    }
}





/* Assign a register to a value */
void MVM_jit_register_assign(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitValueDescriptor *value, MVMJitStorageClass st_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = cl->allocator;
    value->st_cls   = st_cls;
    value->st_pos        = reg_num;

    MVM_DYNAR_PUSH(alc->active, value);
    alc->reg_use[reg_num]++;
}


/* Expiring a value marks it dead and possibly releases its register */
void MVM_jit_register_expire(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitValueDescriptor *value) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    MVMint8 reg_num = value->st_pos;
    MVMint32 i;
    if (value->st_cls == MVM_JIT_STORAGE_FPR) {
        NYI(numeric_regs);
    }
    /* Remove value from active */
    i = 0;
    while (i < alc->active_num) {
        if (alc->active[i] == value) {
            /* splice it out */
            alc->active_num--;
            alc->active[i] = alc->active[alc->active_num];
        } else {
            i++;
        }
    }

    if (value->st_cls == MVM_JIT_STORAGE_GPR) {
        /* Decrease register number count and free if possible */
        alc->reg_use[reg_num]--;
        if (alc->reg_use[reg_num] == 0) {
            MVM_jit_register_free(tc, compiler, value->st_cls, reg_num);
        }
    }
}




/* Expire values that are no longer useful */
void MVM_jit_expire_values(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 order_nr) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    MVMint32 i = 0;
    while (i < alc->active_num) {
        MVMJitValueDescriptor *value = alc->active[i];
        if (value->range_end <= order_nr) {
            MVM_jit_register_expire(tc, compiler, value);
        } else {
            i++;
        }
    }
}


static MVMJitValueDescriptor* node_value(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 node) {
    MVMJitValueDescriptor **v = compiler->allocator->values_by_node + node;
    if (*v == NULL) {
        *v = MVM_spesh_alloc(tc, compiler->graph->sg, sizeof(MVMJitValueDescriptor));
    }
    return *v;
}

static void arglist_get_nodes(MVMThreadContext *tc, MVMJitExprTree *tree,
                              MVMint32 arglist, MVMJitExprNode *nodes) {
    MVMint32 i, nchild = tree->nodes[arglist+1];
    for (i = 0; i < nchild; i++) {
        MVMint32 carg = tree->nodes[arglist+2+i];
        *nodes++      = tree->nodes[carg+1];
    }
}



static void MVM_jit_get_values(MVMThreadContext *tc, MVMJitCompiler *compiler,
                               MVMJitExprTree *tree, MVMJitTile *tile) {
    MVMJitExprNode node = tile->node;
    MVMJitExprNode buffer[16];
    const MVMJitTileTemplate *template = tile->template;

    tile->values[0]       = node_value(tc, compiler, node);
    tile->values[0]->size = tree->info[node].size;

    switch (tree->nodes[node]) {
    case MVM_JIT_IF:
    {
        MVMint32 left = tree->nodes[node+2], right = tree->nodes[node+3];
        /* assign results of IF to values array */
        tile->values[1]  = node_value(tc, compiler, left);
        tile->values[2]  = node_value(tc, compiler, right);
        tile->num_values = 2;
        break;
    }
    case MVM_JIT_ARGLIST:
    {
        /* NB, arglist can conceivably use more than 7 values, although it can
         * safely overflow into args, we may want to find a better solution */
        MVMint32 i;
        tile->num_values = tree->nodes[node+1];
        arglist_get_nodes(tc, tree, node, buffer);
        for (i = 0; i < tile->num_values; i++) {
            tile->values[i+1] = node_value(tc, compiler, buffer[i]);
        }
        break;
    }
    case MVM_JIT_DO:
    {
        MVMint32 nchild     = tree->nodes[node+1];
        MVMint32 last_child = tree->nodes[node+1+nchild];
        tile->values[1]   = node_value(tc, compiler, last_child);
        tile->num_values  = 1;
        break;
    }
    default:
    {
        MVMint32 i, j, k, num_nodes, value_bitmap;
        num_nodes        = MVM_jit_expr_tree_get_nodes(tc, tree, node, tile->template->path, buffer);
        value_bitmap     = tile->template->value_bitmap;
        tile->num_values = template->num_values;
        j = 1;
        k = 0;
        for (i = 0; i < num_nodes; i++) {
            if (value_bitmap & 1) {
                tile->values[j++] = node_value(tc, compiler, buffer[i]);
            } else {
                tile->args[k++]   = buffer[i];
            }
            value_bitmap >>= 1;
        }
        break;
    }
    }
}



void MVM_jit_register_allocate(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTileList *list) {
    MVMJitRegisterAllocator allocator;
    MVMJitExprTree *tree = list->tree;
    MVMJitTile *tile;
    MVMJitValueDescriptor *value;
    MVMint32 i, j;
    MVMint8 reg;

    /* Allocate tables used in register */
    MVM_jit_register_allocator_init(tc, compiler, &allocator, list);

    /* Get value descriptors and calculate live ranges */
    for (i = 0; i < list->items_num; i++) {
        tile = list->items[i];
        if (tile->template == NULL) /* pseudotiles */
            continue;
        MVM_jit_get_values(tc, compiler, tree, tile);
        tile->values[0]->range_start = i;
        for (j = 0; j < tile->num_values; j++) {
            tile->values[j+1]->range_end = i;
        }
    }

    /* Assign registers */
    for (i = 0; i < list->items_num; i++) {
        tile = list->items[i];
        if (tile->template == NULL)
            continue;
        /* TODO; ensure that register values are live */

        /* allocate input register if necessary */
        value = tile->values[0];
        switch(tree->nodes[tile->node]) {
        case MVM_JIT_COPY:
            /* use same register as input  */
            MVM_jit_register_assign(tc, compiler, value, tile->values[1]->st_cls, tile->values[1]->st_pos);
            break;
        case MVM_JIT_TC:
            /* TODO, this isn't really portable, we should have register
             * attributes assigned to the tile itself */
            value->st_cls = MVM_JIT_STORAGE_NVR;
            value->st_pos = MVM_JIT_REG_TC;
            break;
        case MVM_JIT_CU:
            value->st_cls = MVM_JIT_STORAGE_NVR;
            value->st_pos = MVM_JIT_REG_CU;
            break;
        case MVM_JIT_LOCAL:
            value->st_cls = MVM_JIT_STORAGE_NVR;
            value->st_pos = MVM_JIT_REG_LOCAL;
            break;
        case MVM_JIT_STACK:
            value->st_cls = MVM_JIT_STORAGE_NVR;
            value->st_pos = MVM_JIT_REG_STACK;
            break;
        default:
            if (value != NULL && tile->template && tile->template->vtype == MVM_JIT_REG) {
                /* allocate a register for the result */
                if (tile->num_values > 0 &&
                    tile->values[1]->st_cls == MVM_JIT_STORAGE_GPR &&
                    tile->values[1]->range_end == j) {
                    /* First register expires immediately, therefore we can safely cross-assign */
                    MVM_jit_register_assign(tc, compiler, value, tile->values[1]->st_cls, tile->values[1]->st_pos);
                } else {
                    reg = MVM_jit_register_alloc(tc, compiler, MVM_JIT_STORAGE_GPR);
                    MVM_jit_register_assign(tc, compiler, value, MVM_JIT_STORAGE_GPR, reg);
                }
            }
            break;
        }
        /* Expire dead values */
        MVM_jit_expire_values(tc, compiler, i);
    }
    MVM_jit_register_allocator_deinit(tc, compiler, &allocator);
}
