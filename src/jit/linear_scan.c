#include "moar.h"
#include "internal.h"


static MVMint8 available_registers[] = {
    X64_FREE_GPR(MVM_JIT_REG)
};

static MVMint8 non_volatile_registers[] = {
    X64_NVR(MVM_JIT_REG)
};

#define MAX_ACTIVE 9
#define NUM_GPR   16
#define NYI(x) MVM_oops(tc, #x  "not yet implemented")

typedef struct {
    MVMint32 key;
    MVMint32 num_defs, num_uses;
    MVMint32 live_range_idx;
} UnionFind;


/* In an interesting way, this is equivalent to a pointer to the 'values' memory
 * area, and it is just as large anyway! */
typedef struct {
    MVMint32 tile_idx;
    MVMint32 value_idx;
} ValueRef;


typedef struct {
    MVMint32 num_defs;
    ValueRef *defs;
    MVMint32 num_uses;
    ValueRef *uses;

    /* We can have at most two synthetic tiles, one attached to the first
     * definition and one to the last use... we could also point directly into
     * the values array of the tile, but it is not directly necessary */
    MVMint32    synth_pos[2];
    MVMJitTile *synthetic[2];

    MVMint8 register_spec;
    MVMJitStorageClass reg_cls;
    MVMint32 reg_num;
    MVMint32 spilled_to; /* location of value in memory, if any */
} LiveRange;


typedef struct {
    /* Sets of values */
    UnionFind *sets;
    /* single buffer for uses, definitions */
    ValueRef *use_defs_buf;

    /* All values ever defined by the register allcoator */
    MVM_VECTOR_DECL(LiveRange, values);

    /* 'Currently' active values */
    MVMint32 active_top;
    MVMint32 active[MAX_ACTIVE];

    /* Values still left to do (heap) */
    MVM_VECTOR_DECL(MVMint32, worklist);
    /* Retired values (to be assigned registers) (heap) */
    MVM_VECTOR_DECL(MVMint32, retired);

    /* Register handout ring */
    MVMint8   reg_ring[MAX_ACTIVE];
    MVMint32  reg_give, reg_take;
    MVMuint32 is_nvr;

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
static inline MVMint32 first_def(LiveRange *v) {
    MVMint32 a = v->defs == NULL         ? INT32_MAX : v->defs[0].tile_idx;
    MVMint32 b = v->synthetic[0] == NULL ? INT32_MAX : v->synth_pos[0];
    return MIN(a,b);
}

static inline MVMint32 last_use(LiveRange *v) {
    MVMint32 a = v->uses == NULL         ? -1 : v->uses[v->num_uses-1].tile_idx;
    MVMint32 b = v->synthetic[1] == NULL ? -1 : v->synth_pos[1];
    return MAX(a,b);
}

/* create a new live range object and return a reference */
MVMint32 live_range_init(RegisterAllocator *alc, ValueRef *defs, ValueRef *uses) {
    LiveRange *range;
    MVMint32 idx = alc->values_num++;
    MVM_VECTOR_ENSURE_SIZE(alc->values, idx);
    range = &alc->values[idx];
    range->defs = defs;
    range->uses = uses;
    range->num_defs = 0;
    range->num_uses = 0;
    range->synthetic[0] = NULL;
    range->synthetic[1] = NULL;
    return idx;
}

static inline void heap_swap(MVMint32 *heap, MVMint32 a, MVMint32 b) {
    MVMint32 t = heap[a];
    heap[a]    = heap[b];
    heap[b]    = t;
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
            heap_swap(heap, swap, item);
            item       = swap;
        } else {
            break;
        }
    }
}

void live_range_heap_up(LiveRange *values, MVMint32 *heap, MVMint32 item) {
    while (item > 0) {
        MVMint32 parent = (item-1)/2;
        if (first_def(&values[heap[parent]]) < first_def(&values[heap[item]])) {
            heap_swap(heap, item, parent);
            item = parent;
        } else {
            break;
        }
    }
}

MVMint32 live_range_heap_pop(LiveRange *values, MVMint32 *heap, size_t *top) {
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


static void determine_live_ranges(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMint32 i, j;
    MVMint32 num_use = 0, num_def = 0, num_live_range = 0;
    ValueRef *use_buf, *def_buf;
    MVMJitExprTree *tree = list->tree;

    for (i = 0; i < list->items_num; i++) {
        MVMJitTile *tile = list->items[i];
        MVMint32    node = tile->node;
        /* Each of the following counts as either a copy or as a PHI (in case of
         * IF), and thus these are not actual definitions */
        if (tile->op == MVM_JIT_COPY) {
            MVMint32 ref        = tree->nodes[tile->node + 1];
            alc->sets[node].key = ref; /* point directly to actual definition */
        } else if (tile->op == MVM_JIT_DO && MVM_JIT_TILE_YIELDS_VALUE(tile)) {
            MVMint32 nchild     = tree->nodes[tile->node + 1];
            MVMint32 ref        = tree->nodes[tile->node + nchild];
            alc->sets[node].key = ref;
        } else if (tile->op == MVM_JIT_IF) {
            MVMint32 left_cond   = tree->nodes[tile->node + 2];
            MVMint32 right_cond  = tree->nodes[tile->node + 3];
            alc->sets[node].key  = value_set_union(alc->sets, left_cond, right_cond);
            num_live_range--;      /* the union of the left and right side
                                    * reduces the number of live ranges */
        } else if (MVM_JIT_TILE_YIELDS_VALUE(tile)) {
            /* define this value */
            alc->sets[node].num_defs       = 1;
            alc->sets[node].num_uses       = 0;
            alc->sets[node].key            = node;
            alc->sets[node].live_range_idx = -1;

            /* count totals so we can correctly allocate the buffers */
            num_def++;
            num_use += tile->num_refs;
            num_live_range++;
        }

        for (j = 0; j < tile->num_refs; j++) {
            /* account its use */
            value_set_find(alc->sets, tile->refs[j])->num_uses++;
        }

        /* I don't think we have inserted things before that actually refer to
         * tiles, just various jumps to implement IF/WHEN/ANY/ALL handling */
    }

    /* Initialize buffers. Live range buffer can grow, uses-and-definitions
     * buffer never needs to, because any split can just reuse the buffers */
    MVM_VECTOR_INIT(alc->values,   num_live_range);
    MVM_VECTOR_INIT(alc->worklist, num_live_range);
    MVM_VECTOR_INIT(alc->retired,  num_live_range);
    alc->use_defs_buf = MVM_calloc(num_def + num_use, sizeof(ValueRef));

    /* split buf in two */
    use_buf = alc->use_defs_buf;
    def_buf = alc->use_defs_buf + num_use;

    /* second pass, translate the found sets and used nodes to live ranges.
     * because we iterate in ascending order over tiles, uses and defs are
     * automatically ordered too. TODO: figure out a way to represent register
     * preferences! */
    for (i = 0; i < list->items_num; i++) {
        MVMJitTile * tile;
        if (MVM_JIT_TILE_YIELDS_VALUE(tile)) {
            UnionFind *value_set = value_set_find(alc->sets, tile->node);
            MVMint8  register_spec = MVM_JIT_REGISTER_FETCH(tile->register_spec, 0);
            LiveRange *value_range;
            if (value_set->live_range_idx < 0) {
                /* first definition, allocate the live range for this block */
                value_set->live_range_idx = live_range_init(alc, def_buf, use_buf);
                /* bump pointers */
                def_buf += value_set->num_defs;
                use_buf += value_set->num_uses;
                /* add to the work list (which is automatically in first-definition order) */
                MVM_VECTOR_PUSH(alc->worklist, value_set->live_range_idx);
            }
            value_range = &alc->values[value_set->live_range_idx];
            /* add definition */
            {
                ValueRef def = { i, 0 };
                value_range->defs[value_range->num_defs++] = def;
            }
            if (MVM_JIT_REGISTER_HAS_REQUIREMENT(register_spec)) {
                /* TODO - this may require resolving conflicting register specifications */
                value_range->register_spec = register_spec;
            }
        }
        /* Add uses (shouldn't this start from one?) */
        for (j = 0; j < tile->num_refs; j++) {
            UnionFind *use_set   = value_set_find(alc->sets, tile->refs[j]);
            LiveRange *use_range = &alc->values[use_set->live_range_idx];
            MVMint8 register_spec = MVM_JIT_REGISTER_FETCH(tile->register_spec, j+1);
            ValueRef use = { i, j + 1 };
            use_range->uses[use_range->num_uses++] = use;
            if (MVM_JIT_REGISTER_HAS_REQUIREMENT(register_spec)) {
                /* TODO - this may require resolving conflicting register
                 * specifications */
                NYI(use_register_spec);
            }
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
    for (i = 0; i < alc->active_top; i++) {
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
    alc->active[alc->active_top++] = a;
}

/* Take live ranges from active_set whose last use was after position and append them to the retired list */
static void active_set_expire(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 position) {
    MVMint32 i;
    for (i = 0; i < alc->active_top; i++) {
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

static void spill_live_range(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 which) {
}

static void split_live_range(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 which, MVMint32 from, MVMint32 to) {
}

/* register assignment logic */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define NEXT_IN_RING(a,x) (((x)+1) == ARRAY_SIZE(a) ? 0 : ((x)+1))
MVMint8 get_register(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitStorageClass reg_cls) {
    /* ignore storage class for now */
    MVMint8 reg_num;
    reg_num       = alc->reg_ring[alc->reg_take];
    if (reg_num >= 0) {
        /* not empty */
        alc->reg_ring[alc->reg_take] = -1; /* mark used */
        alc->reg_take = NEXT_IN_RING(alc->reg_ring, alc->reg_take);
    }
    return reg_num;
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
        MVMJitTile *tile = list->items[ref->tile_idx];
        tile->values[ref->value_idx] = reg_num;
    }

    /* Not sure if we need to store the position of synthetic tiles for the
     * purposes of first_def/last_use */
    for (i = 0; i < 2; i++) {
        MVMJitTile *tile = range->synthetic[i];
        if (tile != NULL) {
            tile->values[i] = reg_num;
        }
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
    while (alc->worklist_num > 0) {
        MVMint32 v = live_range_heap_pop(alc->values, alc->worklist, &alc->worklist_num);
        MVMint32 pos = first_def(&alc->values[v]);
        MVMint8 reg;
        /* assign registers in loop */
        active_set_expire(tc, alc, pos);
        if (MVM_JIT_REGISTER_HAS_REQUIREMENT(alc->values[v].register_spec)) {
            reg = MVM_JIT_REGISTER_REQUIREMENT(alc->values[v].register_spec);
            if (alc->is_nvr & (1 << reg)) {
                assign_register(tc, alc, list, v, MVM_JIT_STORAGE_NVR, reg);
            } else {
                /* TODO; might require swapping / spilling */
                NYI(general_purpose_register_spec);
            }
        } else {
            while ((reg = get_register(tc, alc, MVM_JIT_STORAGE_GPR)) < 0) {
                spill_register(tc, alc, list, pos);
            }
            assign_register(tc, alc, list, v, MVM_JIT_STORAGE_GPR, reg);
            active_set_add(tc, alc, v);
        }
    }
    /* flush active live ranges */
    active_set_expire(tc, alc, list->items_num + 1);
}


void MVM_jit_linear_scan_allocate(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTileList *list) {
    RegisterAllocator alc;
    MVMint32 i;
    /* initialize allocator */
    alc.sets = MVM_calloc(list->items_num, sizeof(UnionFind));
    alc.is_nvr = 0;
    for (i = 0; i < sizeof(non_volatile_registers); i++) {
        alc.is_nvr |= (1 << non_volatile_registers[i]);
    }
    memcpy(alc.reg_ring, available_registers,
           sizeof(available_registers));

    /* run algorithm */
    determine_live_ranges(tc, &alc, list);
    linear_scan(tc, &alc, list);

    /* deinitialize allocator */
    MVM_free(alc.sets);
    MVM_free(alc.use_defs_buf);
    MVM_free(alc.worklist);
    MVM_free(alc.retired);
    /* make edits effective */
    MVM_jit_tile_list_edit(tc, list);

}
