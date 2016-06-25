#include "moar.h"
#include "internal.h"

/**
 * MoarVM JIT register allocator.
 *
 * This may be a confusing file, because there are quite a few parts, which do
 * separate things, but which are yet too small to be separated. These things are
 * (in the order in which they appear in this file)
 *
 * - register assignment via a ring buffer
 * - single-pass tile list (IR) editing
 * - management of value descriptors
 * - linear scan register allocation
 */

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

/* UNUSED - Register lock bitmap macros */
#define REGISTER_IS_LOCKED(a, n) ((a)->reg_lock &  (1 << (n)))
#define LOCK_REGISTER(a, n)   ((a)->reg_lock |=  (1 << (n)))
#define UNLOCK_REGISTER(a,n)  ((a)->reg_lock &= ~(1 << (n)))
/* it appears MAX is already defined in libtommath */
#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif




/* Live range of nodes */
struct LiveRange {
    MVMint32 range_start, range_end;
    MVMint32 num_use;
    /* Index into the buffer */
    MVMint32 buf_idx;
};

/* Local structure for now, may be promoted to its own file in the future */
struct InsertTile {
    MVMint32    position;
    MVMJitTile *tile;
};


struct RegisterAllocator {
    /* Live range of nodes */
    struct LiveRange *live_ranges;
    /* Nodes refered to by tiles */
    MVM_DYNAR_DECL(MVMJitExprNode, tile_nodes);

    /* Tiles inserted while allocating */
    MVM_DYNAR_DECL(struct InsertTile, tile_inserts);

    /* Lookup tables */
    MVMJitValue **values_by_node;
    MVMJitValue  *values_by_register[MVM_JIT_MAX_GPR];

    MVMJitCompiler *compiler;

    /* Register giveout ring */
    MVMint8 free_reg[NUM_GPR];
    MVMint32 reg_give, reg_take;

    /* Last use of each register */
    MVMint32 last_use[MVM_JIT_MAX_GPR];

    MVMint32 spill_top;
};



void MVM_jit_register_allocator_init(MVMThreadContext *tc, struct RegisterAllocator *allocator,
                                     MVMJitCompiler *compiler, MVMJitTileList *list) {
    /* Store live ranges */
    MVM_DYNAR_INIT(allocator->tile_nodes, list->items_num * 4);

    /* And inserted tiles */
    MVM_DYNAR_INIT(allocator->tile_inserts, 8);

    /* Initialize free register ring */
    memcpy(allocator->free_reg, free_gpr, NUM_GPR);
    allocator->reg_give  = 0;
    allocator->reg_take  = 0;

    /* last use table */
    memset(allocator->last_use, -1, sizeof(allocator->last_use));

    /* create lookup tables */
    allocator->values_by_node = MVM_calloc(list->tree->nodes_num, sizeof(void*));
    memset(allocator->values_by_register, 0, sizeof(allocator->values_by_register));
    allocator->live_ranges    = MVM_calloc(list->tree->nodes_num, sizeof(struct LiveRange));

    allocator->compiler       = compiler;
}

void MVM_jit_register_allocator_deinit(MVMThreadContext *tc, struct RegisterAllocator *allocator) {
    MVM_free(allocator->values_by_node);
    MVM_free(allocator->live_ranges);
    MVM_free(allocator->tile_inserts);
    MVM_free(allocator->tile_nodes);
}


#define NYI(x) MVM_oops(tc, #x " NYI");

static void spill_register(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMint32 order_nr);

/** PART ONE: Register assignment */
static MVMint8 alloc_register(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMJitStorageClass reg_cls, MVMint32 order_nr) {
    MVMint8 reg_num;
    if (reg_cls == MVM_JIT_STORAGE_FPR) {
        NYI(numeric_regs);
    } else {
        if (NEXT_REG(allocator->reg_take) == allocator->reg_give) {
            /* Out of registers, spill something */
            spill_register(tc, allocator, order_nr);
        }
        /* Use a circular handout scheme for the 'fair use' of registers */
        reg_num       = allocator->free_reg[allocator->reg_take];
        /* mark it for debugging purposes */
        allocator->free_reg[allocator->reg_take] = 0xff;
        allocator->reg_take = NEXT_REG(allocator->reg_take);
    }
    /* MVM_jit_log(tc, "Allocated register %d at order nr %d\n", reg_num, cl->order_nr); */
    return reg_num;
}



/* Freeing a register makes it available again */
static void free_register(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMJitStorageClass reg_cls, MVMint8 reg_num) {
    if (reg_cls == MVM_JIT_STORAGE_FPR) {
        NYI(numeric_regs);
    } else {
        MVMint32 i;
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
        allocator->reg_give                      = NEXT_REG(allocator->reg_give);
        allocator->values_by_register[reg_num]   = NULL;
    }
}

static void expire_registers(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMint32 order_nr) {
    MVMint32 i;
    for (i = 0; i < NUM_GPR; i++) {
        MVMint8 reg_num = free_gpr[i];
        if (allocator->last_use[reg_num] == order_nr) {
            free_register(tc, allocator, MVM_JIT_STORAGE_GPR, reg_num);
        }
    }
}





/** PART TWO: Editing the tile list (i.e, inserting code) */
static void insert_tile_after(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMJitTile *tile, MVMint32 position) {
    struct InsertTile i = { position, tile };
    MVM_DYNAR_PUSH(allocator->tile_inserts, i);
}

static void insert_tile_before(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMJitTile *tile, MVMint32 position) {
    struct InsertTile i = { position - 1, tile };
    MVM_DYNAR_PUSH(allocator->tile_inserts, i);
}

static int cmp_tile_insert(const void *p1, const void *p2) {
    return ((struct InsertTile*)p1)->position - ((struct InsertTile*)p2)->position;
}

static void edit_tilelist(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMJitTileList *list) {
    MVMJitTile **worklist;
    MVMint32 i, j, k;
    if (allocator->tile_inserts_num == 0)
        return;
    /* sort inserted tiles in ascending order */
    qsort(allocator->tile_inserts, allocator->tile_inserts_num,
          sizeof(struct InsertTile), cmp_tile_insert);

    /* create a new array for the tiles */
    worklist = MVM_malloc((list->items_num + allocator->tile_inserts_num) * sizeof(MVMJitTile*));

    i = 0;
    j = 0;
    k = 0;

    while (i < list->items_num) {
        while (j < allocator->tile_inserts_num &&
               allocator->tile_inserts[j].position < i) {
            worklist[k++] = allocator->tile_inserts[j++].tile;
        }
        worklist[k++] = list->items[i++];
    }
    /* insert all tiles after the last one, if any */
    while (j < allocator->tile_inserts_num) {
        worklist[k++] = allocator->tile_inserts[j++].tile;
    }

    /* swap old and new list */
    MVM_free(list->items);
    list->items = worklist;
    list->items_num = k;
    list->items_alloc = k;
}


/** PART THREE: Management of value descriptors */
static MVMJitValue* value_create(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMint32 node) {
    MVMJitValue *value = MVM_spesh_alloc(tc, allocator->compiler->graph->sg, sizeof(MVMJitValue));
    value->node = node;
    value->next_by_node = allocator->values_by_node[node];
    allocator->values_by_node[node] = value;
    return value;
}

static void value_assign_storage(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMJitValue *value, MVMJitStorageClass st_cls, MVMint16 st_pos) {
    value->st_cls = st_cls;
    value->st_pos = st_pos;

    if (st_cls == MVM_JIT_STORAGE_GPR) {
        value->next_by_position     = allocator->values_by_register[st_pos];
        allocator->values_by_register[st_pos] = value;
    }
}

static void value_assign_range(MVMThreadContext *tc, struct RegisterAllocator *allocator,
                               MVMJitValue *value, MVMint32 range_start, MVMint32 range_end) {
    value->range_start  = range_start;
    value->range_end    = range_end;

    if (value->st_cls == MVM_JIT_STORAGE_GPR) {
        allocator->last_use[value->st_pos] = MAX(allocator->last_use[value->st_pos], range_end);
    }
}

static void spill_value(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMJitValue *value, MVMint32 order_nr) {
    MVMJitTile *tile;
    MVMJitValue *spilled;
    /* acquire a spillable position, TODO make this more clever and extract it from here */
    MVMint32 spill_pos = allocator->spill_top;
    allocator->spill_top += sizeof(MVMRegister);

    /* create and insert tile for a store */
    tile = MVM_jit_tile_make(tc, allocator->compiler, MVM_jit_compile_store, value->node, 1, spill_pos);
    tile->values[0] = value;
    insert_tile_after(tc, allocator, tile, value->range_start);

    /* create a value and assign it to the newly spilled location */
    spilled = value_create(tc, allocator, value->node);
    value_assign_storage(tc, allocator, spilled, MVM_JIT_STORAGE_LOCAL, spill_pos);
    value_assign_range(tc, allocator, spilled, value->range_start, value->range_end);

    /* reassign the range of the spilled value (to note that it is dead here) */
    value_assign_range(tc, allocator, value, value->range_start, order_nr);
}

static MVMJitValue * load_value(MVMThreadContext *tc, struct RegisterAllocator *allocator,
                                MVMJitValue *spilled, MVMint16 gpr_pos, MVMint32 order_nr) {
    MVMJitValue *live;
    MVMJitTile *tile = MVM_jit_tile_make(tc, allocator->compiler, MVM_jit_compile_load, spilled->node, 1, spilled->st_pos);
    insert_tile_before(tc, allocator, tile, order_nr);

    live = value_create(tc, allocator, spilled->node);
    value_assign_storage(tc, allocator, live, MVM_JIT_STORAGE_GPR, gpr_pos);
    value_assign_range(tc, allocator, live, order_nr, spilled->range_end);

    tile->values[0] = live;
    return live;
}


static void spill_register(MVMThreadContext *tc, struct RegisterAllocator *allocator, MVMint32 order_nr) {
    MVMJitValue *value, *head;
    MVMint8 spill_reg = free_gpr[0];
    MVMint32 i, node, first_created;

    for (i = 1; i < NUM_GPR; i++) {
        if (allocator->last_use[free_gpr[i]] > allocator->last_use[spill_reg]) {
            spill_reg = free_gpr[i];
        }
    }

    for (head = allocator->values_by_register[spill_reg];
         head != NULL; head = head->next_by_position) {
        for (value = allocator->values_by_node[head->node];
             value != NULL; value = value->next_by_node) {
            if (value->st_cls == MVM_JIT_STORAGE_LOCAL ||
                value->st_cls == MVM_JIT_STORAGE_NVR)
                goto have_spilled;
        }
        /* No nonvolatile value descriptor for this node, hence */
        spill_value(tc, allocator, head, order_nr);
    have_spilled:
        continue;
    }
    /* all the necessary spills have been inserted, so now we can */
    free_register(tc, allocator, MVM_JIT_STORAGE_GPR, spill_reg);
}


/* PART FOUR: Linear scan register allocation */

/* Get nodes and arguments refered to by a tile (the 'scan' part of linear scan) */
static void get_tile_nodes(MVMThreadContext *tc, struct RegisterAllocator *allocator,
                           MVMJitTile *tile, MVMJitExprTree *tree) {
    MVMJitExprNode node = tile->node;
    MVMJitExprNode buffer[16];
    const MVMJitTileTemplate *template = tile->template;

    /* Assign tile arguments and compute the refering nodes */
    switch (tree->nodes[node]) {
    case MVM_JIT_IF:
    {
        buffer[0] = tree->nodes[node+2];
        buffer[1] = tree->nodes[node+3];
        tile->num_values = 2;
        break;
    }
    case MVM_JIT_ARGLIST:
    {
        /* NB, arglist can conceivably use more than 7 values, although it can
         * safely overflow into args, we may want to find a better solution */
        MVMint32 i;
        tile->num_values = tree->nodes[node+1];
        for (i = 0; i < tile->num_values; i++) {
            MVMint32 carg = tree->nodes[node+2+i];
            buffer[i]     = tree->nodes[carg+1];
        }
        break;
    }
    case MVM_JIT_DO:
    {
        MVMint32 nchild  = tree->nodes[node+1];
        buffer[0]        = tree->nodes[node+1+nchild];
        tile->num_values = 1;
        break;
    }
    default:
    {
        MVMint32 i, j, k, num_nodes, value_bitmap;
        num_nodes        = MVM_jit_expr_tree_get_nodes(tc, tree, node, tile->template->path, buffer);
        value_bitmap     = tile->template->value_bitmap;
        tile->num_values = template->num_values;
        j = 0;
        k = 0;
        /* splice out args from node refs */
        for (i = 0; i < num_nodes; i++) {
            if (value_bitmap & 1) {
                buffer[j++]     = buffer[i];
            } else {
                tile->args[k++] = buffer[i];
            }
            value_bitmap >>= 1;
        }
        break;
    }
    }
    /* copy to tile-node buffer */
    MVM_DYNAR_APPEND(allocator->tile_nodes, buffer, tile->num_values);
}

#define VALUE_LIVE_AT(v,i) ((v)->range_start < (i) && (v)->range_end >= (i))

void MVM_jit_register_allocate(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTileList *list) {
    struct RegisterAllocator allocator;
    MVMJitExprTree *tree = list->tree;
    MVMint32 i, j;
    MVMint8 reg;

    /* Allocate tables used in register */
    MVM_jit_register_allocator_init(tc, &allocator, compiler, list);

    /* Compute live ranges for each node */
    for (i = 0; i < list->items_num; i++) {
        MVMint32 buf_idx = allocator.tile_nodes_num;
        MVMJitTile *tile = list->items[i];
        if (tile->template == NULL) /* pseudotiles */
            continue;
        allocator.live_ranges[tile->node].range_start = i;
        allocator.live_ranges[tile->node].range_end   = i;
        allocator.live_ranges[tile->node].buf_idx     = buf_idx;
        get_tile_nodes(tc, &allocator, tile, tree);
        for (j = 0; j < tile->num_values; j++) {
            MVMint32 ref_node = allocator.tile_nodes[buf_idx + j];
            allocator.live_ranges[ref_node].range_end = i;
            allocator.live_ranges[ref_node].num_use++;
        }
    }

    /* Assign registers */
    for (i = 0; i < list->items_num; i++) {
        MVMJitTile *tile = list->items[i];
        MVMint32 buf_idx = allocator.live_ranges[tile->node].buf_idx;
        MVMJitValue *value;
        if (tile->template == NULL)
            continue;
        /* TODO; ensure that register values are live */
        for (j = 0; j < tile->num_values; j++) {
            MVMint32 ref_node = allocator.tile_nodes[buf_idx + j];
            MVMint32 tile_nr = allocator.live_ranges[ref_node].range_start;
            MVMJitValue *live, *spill = NULL;
            /* skip pseudotiles and non-register yielding tiles */
            if (list->items[tile_nr]->template == NULL ||
                list->items[tile_nr]->template->vtype != MVM_JIT_REG)
                continue;

            for (live = allocator.values_by_node[ref_node];
                 live != NULL; live = live->next_by_node) {
                if (!VALUE_LIVE_AT(live, i))
                    continue;
                if (live->st_cls == MVM_JIT_STORAGE_NVR || live->st_cls == MVM_JIT_STORAGE_GPR)
                    break;
                if (live->st_cls == MVM_JIT_STORAGE_LOCAL && spill == NULL)
                    spill = live;
            }
            if (live == NULL) {
                MVMint16 reg_pos;
                if (spill == NULL)
                    MVM_oops(tc, "JIT: Could not find value for node %d", ref_node);
                /* TODO: restore the locking mechanism to ensure we don't
                 * actually spill a value which is in use; give relative
                 * ordering to tile insert */
                NYI(load_spilled_register);
                reg_pos = alloc_register(tc, &allocator, MVM_JIT_STORAGE_GPR, i);
                live = load_value(tc, &allocator, spill, reg_pos, i);
            }
            tile->values[j+1] = live;
        }

        /* allocate input register if necessary */

        switch(tree->nodes[tile->node]) {
        case MVM_JIT_COPY:
        {
            /* use same register as input  */
            MVMint32 ref_node = allocator.tile_nodes[buf_idx];
            MVMJitValue *ref_value = allocator.values_by_node[ref_node];
            value_assign_storage(tc, &allocator, value_create(tc, &allocator, tile->node),
                                 ref_value->st_cls, ref_value->st_pos);
            break;
        }
        case MVM_JIT_TC:
            /* TODO, this isn't really portable, we should have register
             * attributes assigned to the tile itself */
            value_assign_storage(tc, &allocator,
                                 value_create(tc, &allocator, tile->node),
                                 MVM_JIT_STORAGE_NVR, MVM_JIT_REG_TC);
            break;
        case MVM_JIT_CU:
            value_assign_storage(tc, &allocator,
                                 value_create(tc, &allocator, tile->node),
                                 MVM_JIT_STORAGE_NVR, MVM_JIT_REG_CU);
            break;
        case MVM_JIT_LOCAL:
            value_assign_storage(tc, &allocator,
                                 value_create(tc, &allocator, tile->node),
                                 MVM_JIT_STORAGE_NVR, MVM_JIT_REG_LOCAL);
            break;
        case MVM_JIT_STACK:
            value_assign_storage(tc, &allocator,
                                 value_create(tc, &allocator, tile->node),
                                 MVM_JIT_STORAGE_NVR, MVM_JIT_REG_STACK);
            break;
        default:
            if (tile->template->vtype == MVM_JIT_REG) {
                /* allocate a register for the result */
                if (tile->num_values > 0 &&
                    tile->values[1]->st_cls == MVM_JIT_STORAGE_GPR &&
                    tile->values[1]->range_end == i) {
                    /* First register expires immediately, therefore we can safely cross-assign */
                    value_assign_storage(tc, &allocator,
                                         value_create(tc, &allocator, tile->node),
                                         tile->values[1]->st_cls, tile->values[1]->st_pos);
                } else {
                    reg = alloc_register(tc, &allocator, MVM_JIT_STORAGE_GPR, i);
                    value_assign_storage(tc, &allocator,
                                         value_create(tc, &allocator, tile->node),
                                         MVM_JIT_STORAGE_GPR, reg);
                }
            }
            break;
        }
        tile->values[0] = allocator.values_by_node[tile->node];
        if (tile->values[0] != NULL) {
            tile->values[0]->size = tree->info[tile->node].size;
            value_assign_range(tc, &allocator, tile->values[0],
                               allocator.live_ranges[tile->node].range_start,
                               allocator.live_ranges[tile->node].range_end);
        }
        expire_registers(tc, &allocator, i);
    }
    /* Insert tiles into the list */
    edit_tilelist(tc, &allocator, list);
    MVM_jit_register_allocator_deinit(tc, &allocator);
}
