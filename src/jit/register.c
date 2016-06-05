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


struct RegisterAllocator {
    MVM_DYNAR_DECL(MVMJitValueDescriptor*, active);

    /* Values by node */
    MVMJitValueDescriptor **values_by_node;

    MVMJitCompiler *compiler;

    /* Register giveout ring */
    MVMint8 free_reg[NUM_GPR];
    MVMuint8 reg_use[MVM_JIT_MAX_GPR];

    MVMint32 reg_give, reg_take;

    /* topmost spill location used */
    MVMint32 spill_top;
    /* Bitmap of used registers */
    MVMint32 reg_lock;
};


void MVM_jit_register_allocator_init(MVMThreadContext *tc, struct RegisterAllocator *allocator,
                                     MVMJitCompiler *compiler, MVMJitTileList *list) {
    /* Store live ranges */
    MVM_DYNAR_INIT(allocator->active, NUM_GPR);
    /* Initialize free register buffer */
    memcpy(allocator->free_reg, free_gpr, NUM_GPR);
    memset(allocator->reg_use, 0, sizeof(allocator->reg_use));

    allocator->reg_give  = 0;
    allocator->reg_take  = 0;

    allocator->spill_top = 1;
    allocator->reg_lock  = 0;

    allocator->values_by_node = MVM_calloc(list->tree->nodes_num, sizeof(void*));

    allocator->compiler       = compiler;
}

void MVM_jit_register_allocator_deinit(MVMThreadContext *tc, struct RegisterAllocator *allocator) {
    MVM_free(allocator->active);
    MVM_free(allocator->values_by_node);
}

#define NYI(x) MVM_oops(tc, #x " NYI");


MVMint8 MVM_jit_register_alloc(MVMThreadContext *tc,  struct RegisterAllocator *allocator, MVMint32 reg_cls) {
    MVMint8 reg_num;
    if (reg_cls == MVM_JIT_STORAGE_FPR) {
        NYI(numeric_regs);
    } else {
        if (NEXT_REG(allocator->reg_take) == allocator->reg_give) {
            /* Out of registers, spill something */
            NYI(spill_something);
        }
        /* Use a circular handout scheme for the 'fair use' of registers */
        reg_num       = allocator->free_reg[allocator->reg_take];
        allocator->free_reg[allocator->reg_take] = 0xff; /* mark it for debugging purposes */
        allocator->reg_take = NEXT_REG(allocator->reg_take);
    }
    /* MVM_jit_log(tc, "Allocated register %d at order nr %d\n", reg_num, cl->order_nr); */
    return reg_num;
}


/* Freeing a register makes it available again */
void MVM_jit_register_free(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMint32 reg_cls, MVMint8 reg_num) {
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
        if (allocator->reg_give == allocator->reg_take) {
            MVM_oops(tc, "Trying to free too many registers");
        }
        allocator->free_reg[allocator->reg_give] = reg_num;
        allocator->reg_give                = NEXT_REG(allocator->reg_give);
    }
}





/* Assign a register to a value */
void MVM_jit_register_assign(MVMThreadContext *tc,  struct RegisterAllocator *allocator, MVMJitValueDescriptor *value, MVMJitStorageClass st_cls, MVMint8 reg_num) {
    value->st_cls   = st_cls;
    value->st_pos        = reg_num;

    MVM_DYNAR_PUSH(allocator->active, value);
    allocator->reg_use[reg_num]++;
}


/* Expiring a value marks it dead and possibly releases its register */
void MVM_jit_register_expire(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMJitValueDescriptor *value) {

    MVMint8 reg_num = value->st_pos;
    MVMint32 i;
    if (value->st_cls == MVM_JIT_STORAGE_FPR) {
        NYI(numeric_regs);
    }
    /* Remove value from active */
    i = 0;
    while (i < allocator->active_num) {
        if (allocator->active[i] == value) {
            /* splice it out */
            allocator->active_num--;
            allocator->active[i] = allocator->active[allocator->active_num];
        } else {
            i++;
        }
    }

    if (value->st_cls == MVM_JIT_STORAGE_GPR) {
        /* Decrease register number count and free if possible */
        allocator->reg_use[reg_num]--;
        if (allocator->reg_use[reg_num] == 0) {
            MVM_jit_register_free(tc, allocator, value->st_cls, reg_num);
        }
    }
}




/* Expire values that are no longer useful */
void MVM_jit_expire_values(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMint32 order_nr) {
    MVMint32 i = 0;
    while (i < allocator->active_num) {
        MVMJitValueDescriptor *value = allocator->active[i];
        if (value->range_end <= order_nr) {
            MVM_jit_register_expire(tc, allocator, value);
        } else {
            i++;
        }
    }
}


static MVMJitValueDescriptor* node_value(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMint32 node) {
    MVMJitValueDescriptor **v = allocator->values_by_node + node;
    if (*v == NULL) {
        *v = MVM_spesh_alloc(tc, allocator->compiler->graph->sg, sizeof(MVMJitValueDescriptor));
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



static void MVM_jit_get_values(MVMThreadContext *tc, struct RegisterAllocator *allocator,
                               MVMJitExprTree *tree, MVMJitTile *tile) {
    MVMJitExprNode node = tile->node;
    MVMJitExprNode buffer[16];
    const MVMJitTileTemplate *template = tile->template;

    tile->values[0]       = node_value(tc, allocator, node);
    tile->values[0]->size = tree->info[node].size;

    switch (tree->nodes[node]) {
    case MVM_JIT_IF:
    {
        MVMint32 left = tree->nodes[node+2], right = tree->nodes[node+3];
        /* assign results of IF to values array */
        tile->values[1]  = node_value(tc, allocator, left);
        tile->values[2]  = node_value(tc, allocator, right);
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
            tile->values[i+1] = node_value(tc, allocator, buffer[i]);
        }
        break;
    }
    case MVM_JIT_DO:
    {
        MVMint32 nchild     = tree->nodes[node+1];
        MVMint32 last_child = tree->nodes[node+1+nchild];
        tile->values[1]   = node_value(tc, allocator, last_child);
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
                tile->values[j++] = node_value(tc, allocator, buffer[i]);
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
    struct RegisterAllocator allocator;
    MVMJitExprTree *tree = list->tree;
    MVMJitTile *tile;
    MVMJitValueDescriptor *value;
    MVMint32 i, j;
    MVMint8 reg;

    /* Allocate tables used in register */
    MVM_jit_register_allocator_init(tc, &allocator, compiler, list);

    /* Get value descriptors and calculate live ranges */
    for (i = 0; i < list->items_num; i++) {
        tile = list->items[i];
        if (tile->template == NULL) /* pseudotiles */
            continue;
        MVM_jit_get_values(tc, &allocator, tree, tile);
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
            MVM_jit_register_assign(tc, &allocator, value, tile->values[1]->st_cls, tile->values[1]->st_pos);
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
                    MVM_jit_register_assign(tc, &allocator, value, tile->values[1]->st_cls, tile->values[1]->st_pos);
                } else {
                    reg = MVM_jit_register_alloc(tc, &allocator, MVM_JIT_STORAGE_GPR);
                    MVM_jit_register_assign(tc, &allocator, value, MVM_JIT_STORAGE_GPR, reg);
                }
            }
            break;
        }
        /* Expire dead values */
        MVM_jit_expire_values(tc, &allocator, i);
    }
    MVM_jit_register_allocator_deinit(tc, &allocator);
}
