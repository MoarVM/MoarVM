#include "moar.h"

typedef struct {
    MVMint32 key;
    MVMint32 num_defs, num_uses;
    MVMint32 live_range_idx;
} UnionFind;

typedef struct {
    MVMint32 tile_idx;
    MVMint32 value_idx;
} ValueRef;


typedef struct {
    MVMint32 num_defs;
    ValueRef *defs;
    MVMint32 num_uses;
    ValueRef *uses;

    MVMJitStorageClass reg_cls;
    MVMint32 reg_num;
    MVMint32 spilled_to; /* location of value in memory, if any */
} LiveRange;


typedef struct {
    /* Sets of values */
    UnionFind *value_sets;
    /* single buffer for uses, definitions */
    ValueRef *use_defs_buf;

    /* All values ever defined by the register allcoator */
    MVM_VECTOR_DECL(LiveRange, values);

    /* 'Currently' active values */
    MVMint32 active_top;
    MVMint32 active[MAX_ACTIVE];

    /* which live-set inhabits a constant register */
    MVMint32 prefered_register[NUM_GPR];

    /* Values still left to do (heap) */
    MVM_VECTOR_DECL(MVMint32, worklist);
    /* Retired values (to be assigned registers) (heap) */
    MVM_VECTOR_DECL(MVMint32, retired);

    /* Register handout ring */
    MVMint8  reg_ring[NUM_GPR];
    MVMint32 reg_give, reg_take;

    MVMint32 spill_top;
} RegisterAllocator;


UnionFind * value_set_find(UnionFind *sets, MVMint32 key) {
    while (sets[key].key != key) {
        key = sets[key].key;
    }
    return sets + key;
}


MVMint32 value_set_union(UnionFind *sets, MVMint32 a, MVMint32 b) {
    if (sets[a].num_defs < sets[b].num_defs) {
        MVMint32 t = a; a = b; b = t;
    }
    sets[b].key = a; /* point b to a */
    sets[a].num_defs += sets[b].num_defs;
    sets[a].num_uses += sets[b].num_uses;
    return a;
}


/* quick accessors for common checks */
static inline MVMint32 first_def(LiveRange *range) {
    return range->defs[0];
}

static inline MVMint32 last_use(LiveRange *v) {
    return (v->uses[v->num_uses-1]);
}

/* create a new live range object and return a reference */
MVMint32 live_range_init(RegisterAllocator *alc, ValueRef *defs, ValueRef *uses) {
    LiveRange *range;
    MVMint32 idx = alc->values_top++;
    MVM_VECTOR_ENSURE_SIZE(alc->values, idx);
    range = &alc->values[idx];
    range->defs = defs;
    range->uses = uses;
    range->num_defs = 0;
    range->num_uses = 0;
    return idx;
}


/* Functions to maintain a heap of references to the live ranges */
void live_range_heap_down(LiveRange *values, MVMint32 *heap, MVMint32 top, MVMint32 item) {
    while (item < top) {
        MVMint32 left = item * 2 + 1;
        MVMint32 right = left + 1;
        MVMint32 swap;
        if (right < top) {
            swap = first_def(&values[heap[left]]) < first_def(&values[heap[right]]) ? left : right;
        } else if (left < top) {
            swap = left;
        } else {
            break;
        }
        if (first_def(&values[heap[swap]]) < first_def(&values[heap[item]])) {
            MVMint32 temp = heap[swap];
            heap[swap] = heap[item];
            heap[item] = temp;
            item = swap;
        } else {
            break;
        }
    }
}

void live_range_heap_up(LiveRange *values, MVMint32 *heap, MVMint32 item) {
    while (item > 0) {
        MVMint32 parent = (item-1)/2;
        if (first_def(&values[heap[parent]]) < first_def(&values[heap[item]])) {
            MVMint32 temp = heap[parent];
            heap[parent] = heap[item];
            heap[item]   = temp;
            item = parent;
        } else {
            break;
        }
    }
}

MVMint32 live_range_heap_pop(LiveRange *values, MVMint32 *heap, MMVint32 *top) {
    MVMint32 v = heap[0];
    MVMint32 t = --(*top);
    /* pop by swap and heap-down */
    heap[0]    = heap[t];
    live_range_heap_down(values, heap, t, 0);
    return v;
}

void live_range_heap_push(LiveRange *values, MVMint32 *heap, MVMint32 *top, MVMint32 v) {
    /* NB, caller should use MVM_ENSURE_SPACE prior to calling */
    MVMint32 t = (*top)++;
    heap[t] = v;
    live_range_heap_up(values, heap, t);
}

void live_range_heapify(LiveRange *values, MVMint32 *heap, MVMint32 top) {
    MVMint32 i = top, mid = top/2;
    while (i-- > mid) {
        live_range_heap_up(values, heap, i);
    }
}


static void determine_live_ranges(MVMThreadContext *tc, MVMJitTileList *list, RegisterAllocator *alc) {
    MVMint32 i, j;
    MVMint32 num_use = 0, num_def = 0, num_live_range = 0;
    MVMint32 tile_nodes[16];
    ValueRef *use_buf, *def_buf;

    for (i = 0; i < list->items_num; i++) {
        MVMJitTile *tile = list->items[i];
        MVMint32 node = list->tree[tile->node];
        /* Each of the following counts as either a copy or as a PHI (in case of
         * IF), and thus these are not actual definitions */
        if (node == MVM_JIT_COPY) {
            MVMint32 ref        = list->tree[tile->node + 1];
            alc->sets[node].key = ref; /* point directly to actual definition */
        } else if (node == MVM_JIT_DO && TILE_YIELDS_VALUE(tile)) {
            MVMint32 nchild     = list->tree[tile->node + 1];
            MVMint32 ref        = list->tree[tile->node + nchild];
            alc->sets[node].key = ref;
        } else if (node == MVM_JIT_IF) {
            MVMint32 left_cond   = list->tree[tile->node + 2];
            MVMint32 right_cond  = list->tree[tile->node + 3];
            alc->sets[node].key  = value_set_union(alc->sets, left_cond, right_cont);
            num_live_range--;      /* the union of the left and right side
                                    * reduces the number of live ranges */
        } else if (TILE_YIELDS_VALUE(tile)) {
            /* define this value */
            alc->sets[node].num_defs   = 1;
            alc->sets[node].num_uses   = 0;
            alc->sets[node].key        = node;
            alc->sets[node].live_range = -1;

            /* count totals so we can correctly allocate the buffers */
            num_def++;
            num_use += tile->num_values;
            num_live_range++;
        }
        /* NB - if the live range has definitions or uses with conflicting
         * register preferences, we *must* split them into different live ranges
         * here */
        /* TODO: remove tile->template, let tiles contain their own nodes, not
         * necessarily true for ARGLIST */
        if (tile->template) {
            /* read the tile */
            MVM_jit_expr_tree_get_nodes(tc, list->tree, tile->node, tile->template->path, tile_nodes);
            for (j = 0; j < tile->num_values; j++) {
                if ((tile->template->value_bitmap & (1 << j)) == 0)
                    continue; /* is a constant parameter to the tile, not a reference */
                used_node = tile->nodes[j];
                /* account its use */
                value_set_find(alc->sets, used_tile)->num_uses++;
            }
        }
        /* I don't think we have inserted things before that actually refer to
         * tiles, just various jumps to implement IF/WHEN/ANY/ALL handling */
    }

    /* Initialize buffers. Live range buffer can grow, uses-and-definitions
     * buffer never needs to, because any split can just reuse the buffers */
    MVM_VECTOR_INIT(alc->values, num_live_range);
    MVM_VECTOR_INIT(alc->worklist, num_live_range);
    MVM_VECTOR_INIT(alc->retired, num_live_range);
    alc->use_defs_buf = MVM_calloc(num_defs + num_uses, sizeof(ValueRef));

    /* split buf in two */
    use_buf = alc->use_defs_buf;
    def_buf = alc->use_defs_buf + num_use;

    /* second pass, translate the found sets and used nodes to live ranges.
     * because we iterate in ascending order over tiles, uses and defs are
     * automatically ordered too. TODO: figure out a way to represent register
     * preferences! */
    for (i = 0; i < list->items_num; i++) {
        MVMJitTile * tile;
        if (TILE_YIELDS_VALUE(tile)) {
            UnionFind *value_set = value_set_find(alc->sets, tile->node);
            LiveRange *value_range;
            MVMint32  def_num    = value_range->num_defs++;
            if (value_set->live_range < 0) {
                /* first definition, allocate the live range for this block */
                value_set->live_range = live_range_init(alc, def_buf, use_buf);
                /* bump pointers */
                def_buf += value_set->num_defs;
                use_buf += value_set->num_uses;
                /* add to the work list (which is automatically in first-definition order) */
                MVM_VECTOR_PUSH(alc->worklist, value_set->live_range);
            }
            value_range = alc->values[value_set->live_range];
            /* add definition */
            value_range->defs[value_range->num_defs++] = { i, 0 };
        }
        /* Add uses (shouldn't this start from one?) */
        for (j = 0; j < tile->num_values; j++) {
            UnionFind *use_set = value_set_find(alc->sets, tile->nodes[j]);
            LiveRange *use_range = alc->values[use_set->live_range];
            use_ranges->uses[use_range->num_uses++] = { i, j };
        }
    }
}

/* The code below needs some thinking... */
static void active_set_add(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 a) {
    /* the original linear-scan heuristic for spilling is to take the last value
     * in the set to expire, freeeing up the largest extent of code... that is a
     * reasonably good heuristic, albeit not essential to the concept of linear
     * scan. It makes sense to keep the stack ordered at all times (simplest by
     * use of insertion sort). Although insertion sort is O(n^2), n is never
     * large in this case (32 for RISC architectures, maybe, if we ever support
     * them; 7 for x86-64. So the time spent on insertion sort is always small
     * and bounded by a constant, hence O(1). Yes, algorithmics works this way
     * :-) */
    MVMint32 i;
    for (i = 0; i < alc->live_set_top; i++) {
        MVMint32 b = alc->active[i];
        if (last_use(&alc->values[b]) > last_use(&alc->values[a])) {
            /* insert a before b */
            memmove(alc->active + i + 1, alc->active + i, sizeof(MVMint32)*(alc->active_top - i));
            alc->active[i] = b;
            alc->active_top++;
            return;
        }
    }
    /* append at the end */
    alc->active[alc->active_top++] = value;
}

/* Take live ranges from active_set whose last use was after position and append them to the retired list */
static void active_set_expire(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 position) {
    MVMint32 i;
    for (i = 0; i < alc->live_set_top; i++) {
        MVMint32 v = alc->active[i];
        if (last_use(&alc->values[v]) > position) {
            break;
        }
    }

    /* shift off the first x values from the live set. */
    if (i > 0) {
        MVM_VECTOR_APPEND(alc->retired, alc->active, i);
        MVM_VECTOR_SHIFT(alc->active, i);
    }
}


static void spill_register(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list, MVMint32 position) {
    /* Spilling involves the following:
       - choosing a live range from the active set to spill
       - finding a place where to spill it
       - choosing whether to split this live range in a pre-spill and post-spill part
          - potentially spill only part of it
       - for each definition (in the spilled range),
          - make a new live range that
          - reuses the use and def pointer for the definition
          - insert a store just after the defintion
          - and if it lies in the future, put it on worklist, if it lies in the past, put it on the retired list
          - and update the definition to point to the newly created live range
       - for each use (in the spilled range)
          - make a new live range that reuses the use and def pointer for the use
          - insert a load just before the use
          - if it lies in the future, put it on the worklist, if it lies in the past, put it on the retired list
          - update the using tile to point to the newly created live range
       - remove it from the active set
    */
    MVM_oops(tc, "spill_register NYI");
}

static void spill_live_range(MVMthreadContext *tc, RegisterAllocator *alc, MVMint32 which) {
}

static void split_live_range(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 which, MVMint32 from, MVMint32 to) {
}

/* register assignment logic */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define NEXT_IN_RING(a,x) (((x)+1) % ARRAY_SIZE(a))
MVMint8 get_register(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitStorageClass reg_cls) {
    /* ignore storage class for now */
    MVMint8 reg_num;
    if (NEXT_IN_RING(alc->reg_ring, alc->reg_take) == alc->reg_give) {
        MVM_oops(tc, "Linear scan has run out of registers (this should never happen)");
    }
    reg_num       = alc->reg_ring[alc->reg_take];
    alc->reg_ring[alc->reg_take] = -1; /* mark used */
    alc->reg_take = NEXT_IN_RING(alc->reg_ring, alc->reg_take);
    return reg;
}

void free_register(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitStorageClass reg_cls, MVMint8 reg_num) {
    if (alc->reg_give == alc->reg_take) {
        MVM_oops(tc, "Trying to release more registers than fit into the ring");
    }
    alc->reg_ring[alc->reg_give] = reg_num;
    alc->reg_give = NEXT_IN_RING(alc->reg_ring, alc->reg_give);
}

void assign_register(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                     MVMint32 lv, MVMJitStorageClass reg_cls,  MVMint8 reg_num) {
    /* What to do here:
     * - update tiles using this live range to refer to this register
     * - update allocator to mark this register as used by this live range */
    LiveRange *range = alc->values + lv;
    int i;
    range->reg_cls   = reg_cls;
    range->reg_num   = reg_num;

    for (i = 0; i < range->num_defs; i++) {
        ValueRef * ref   = range->defs + i;
        MVMJitTile *tile = list->items[ref->tile_idx];
        tile->values[ref->value_idx] = reg_num;
    }

    for (i = 0; i < range->num_uses; i++) {
        ValueRef *ref    = range->uses + i;
        MVMJitTile *tile = list->istems[ref->tile_idx];
        tile->values[ref->value_idx] = reg_num;
    }
}

/* not sure if this is sufficiently general-purpose and unconfusing */
#define MVM_VECTOR_ASSIGN(a,b) do {             \
        a = b;                                  \
        a ## _top = b ## _top;                  \
        a ## _alloc = b ## _alloc;              \
    } while (0);


static void linear_scan(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMint32 i, j;
    while (alc->worklist_top > 0) {
        MVMint32 v = live_range_heap_pop(alc->values, alc->worklist, &alc->worklist_top);
        MVMint32 pos = first_def(&alc->values[v]);
        MVMint8 reg;
        /* asign registers in first loop */
        active_set_expire(tc, alc, pos);
        while ((reg = get_register(tc, alc, MVM_JIT_STORAGE_CLASS_GPR)) < 0) {
            spill_register(tc, alc, list, pos);
        }
        assign_register(tc, alc, list, v, MVM_JIT_STORAGE_CLASS_GPR, reg);
        active_set_add(tc, alc, v);
    }
    /* flush active live ranges */
    MVM_VECTOR_APPEND(live_range->retired, alc->active, alc->active_top);
    alc->active_top = 0;
}
