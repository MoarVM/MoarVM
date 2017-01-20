#include "moar.h"
#include "internal.h"

#define __COMMA__ ,
static MVMint8 available_gpr[] = {
    MVM_JIT_ARCH_AVAILABLE_GPR(MVM_JIT_REG)
};
static MVMint8 available_num[] = {
    MVM_JIT_ARCH_NUM(MVM_JIT_REG)
};
/* bitmap, so make it '|' to combine the shifted register numbers */
#undef __COMMA__
#define __COMMA__ |
#define SHIFT(x) (1 << (MVM_JIT_REG(x)))
static const MVMint64 NVR_GPR_BITMAP = MVM_JIT_ARCH_NONVOLATILE_GPR(SHIFT);
#undef SHIFT
#undef __COMMA__


#define MAX_ACTIVE sizeof(available_gpr)
#define NYI(x) MVM_oops(tc, #x  "not yet implemented")

typedef struct {
    MVMint32 key;
    MVMint32 idx;
} UnionFind;


#ifdef MVM_JIT_DEBUG
#define _DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define _DEBUG(...) do {} while(0)
#endif


typedef struct ValueRef ValueRef;
struct ValueRef {
    MVMint32  tile_idx;
    MVMint32  value_idx;
    ValueRef *next;
};

typedef struct {
    /* double-ended queue of value refs */
    ValueRef *first, *last;

    /* We can have at most two synthetic tiles, one attached to the first
     * definition and one to the last use... we could also point directly into
     * the values array of the tile, but it is not directly necessary */
    MVMint32    synth_pos[2];
    MVMJitTile *synthetic[2];

    MVMint8            register_spec;
    MVMJitStorageClass reg_cls;
    MVMint32           reg_num;

    MVMint32           spill_pos;
} LiveRange;




typedef struct {
    MVMJitCompiler *compiler;

    /* Sets of values */
    UnionFind *sets;

    /* single buffer for uses, definitions */
    ValueRef *refs;
    MVMint32  refs_num;

    /* All values ever defined by the register allcoator */
    MVM_VECTOR_DECL(LiveRange, values);

    /* 'Currently' active values */
    MVMint32 active_top;
    MVMint32 active[MAX_ACTIVE];

    /* Values still left to do (heap) */
    MVM_VECTOR_DECL(MVMint32, worklist);
    /* Retired values (to be assigned registers) (heap) */
    MVM_VECTOR_DECL(MVMint32, retired);
    /* Spilled values */
    MVM_VECTOR_DECL(MVMint32, spilled);


    /* Register handout ring */
    MVMint8   reg_ring[MAX_ACTIVE];
    MVMint32  reg_give, reg_take;

} RegisterAllocator;


/* For first/last ref comparison, the tile indexes are doubled, and the indexes
 * of synthetics are biased with +1/-1. We use this extra space on the number
 * line to ensure consistent ordering and expiring behavior for 'synthetic' live
 * ranges that either start before an instruction (loading a required value) or
 * end just after one (storing the produced value). Without this, ordering
 * problems can cause two 'atomic' live ranges to be allocated and expired
 * before their actual last use */
static inline MVMint32 order_nr(MVMint32 tile_idx) {
    return tile_idx * 2;
}

/* quick accessors for common checks */
static inline MVMint32 first_ref(LiveRange *r) {
    MVMint32 a = r->first == NULL        ? INT32_MAX : order_nr(r->first->tile_idx);
    MVMint32 b = r->synthetic[0] == NULL ? INT32_MAX : order_nr(r->synth_pos[0]) - 1;
    return MIN(a,b);
}

static inline MVMint32 last_ref(LiveRange *r) {
    MVMint32 a = r->last == NULL         ? -1 : order_nr(r->last->tile_idx);
    MVMint32 b = r->synthetic[1] == NULL ? -1 : order_nr(r->synth_pos[1]) + 1;
    return MAX(a,b);
}

static inline MVMint32 is_definition(ValueRef *v) {
    return (v->value_idx == 0);
}

/* allocate a new live range value by pointer-bumping */
MVMint32 live_range_init(RegisterAllocator *alc) {
    LiveRange *range;
    MVMint32 idx = alc->values_num++;
    MVM_VECTOR_ENSURE_SIZE(alc->values, idx);
    return idx;
}

static inline MVMint32 live_range_is_empty(LiveRange *range) {
    return (range->first == NULL &&
            range->synthetic[0] == NULL &&
            range->synthetic[1] == NULL);
}


/* append ref to end of queue */
static void live_range_add_ref(RegisterAllocator *alc, LiveRange *range, MVMint32 tile_idx, MVMint32 value_idx) {
    ValueRef *ref = alc->refs + alc->refs_num++;

    ref->tile_idx  = tile_idx;
    ref->value_idx = value_idx;

    if (range->first == NULL) {
        range->first = ref;
    }
    if (range->last != NULL) {
        range->last->next = ref;
    }
    range->last = ref;
    ref->next   = NULL;
}

/* merge value ref sets */
static void live_range_merge(LiveRange *a, LiveRange *b) {
    ValueRef *head = NULL, *tail = NULL;
    MVMint32 i;
    _DEBUG("Merging live ranges (%d-%d) and (%d-%d)\n",
           first_ref(a), last_ref(a), first_ref(b), last_ref(b));
    if (first_ref(a) <= first_ref(b)) {
        head = a->first;
        a->first = a->first->next;
    } else {
        head = b->first;
        b->first = b->first->next;
    }
    tail = head;
    while (a->first != NULL && b->first != NULL) {
        if (a->first->tile_idx <= b->first->tile_idx) {
            tail->next  = a->first;
            a->first    = a->first->next;
        } else {
            tail->next  = b->first;
            b->first    = b->first->next;
        }
        tail = tail->next;
    }
    while (a->first != NULL) {
        tail->next = a->first;
        a->first   = a->first->next;
        tail       = tail->next;
    }
    while (b->first != NULL) {
        tail->next  = b->first;
        b->first    = b->first->next;
        tail        = tail->next;
    }

    a->first = head;
    a->last  = tail;

    for (i = 0; i < 2; i++) {
        if (b->synthetic[i] == NULL) {
            continue;
        }
        if (a->synthetic[i] != NULL) {
            MVM_panic(1, "Can't merge the same synthetic!");
        }
        a->synthetic[i] = b->synthetic[i];
        a->synth_pos[i] = b->synth_pos[i];
    }
}



UnionFind * value_set_find(UnionFind *sets, MVMint32 key) {
    while (sets[key].key != key) {
        key = sets[key].key;
    }
    return sets + key;
}

MVMint32 value_set_union(UnionFind *sets, LiveRange *values, MVMint32 a, MVMint32 b) {

    /* dereference the sets to their roots */
    a = value_set_find(sets, a)->key;
    b = value_set_find(sets, b)->key;
    if (a == b) {
        /* secretly the same set anyway, could happen in some combinations of
         * IF, COPY, and DO. */
        return a;
    }
    if (first_ref(values + sets[b].idx) < first_ref(values + sets[a].idx)) {
        /* ensure we're picking the first one to start so that we maintain the
         * first-definition heap order */
        MVMint32 t = a; a = b; b = t;
    }
    sets[b].key = a; /* point b to a */
    live_range_merge(values + sets[a].idx, values + sets[b].idx);
    return a;
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
            swap = first_ref(&values[heap[left]]) < first_ref(&values[heap[right]]) ? left : right;
        } else if (left < top) {
            swap = left;
        } else {
            break;
        }
        if (first_ref(&values[heap[swap]]) < first_ref(&values[heap[item]])) {
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
        if (first_ref(&values[heap[parent]]) > first_ref(&values[heap[item]])) {
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

void live_range_heap_push(LiveRange *values, MVMint32 *heap, size_t *top, MVMint32 v) {
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
    if (alc->reg_ring[alc->reg_give] != -1) {
        MVM_oops(tc, "No space to release register %d to ring", reg_num);
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
    ValueRef *ref;
    MVMint32 i;

    range->reg_cls   = reg_cls;
    range->reg_num   = reg_num;
    for (ref = range->first; ref != NULL; ref = ref->next) {
        MVMJitTile *tile = list->items[ref->tile_idx];
        tile->values[ref->value_idx] = reg_num;
    }

    for (i = 0; i < 2; i++) {
        MVMJitTile *tile = range->synthetic[i];
        if (tile != NULL) {
            tile->values[i] = reg_num;
        }
    }
}


static void determine_live_ranges(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMJitExprTree *tree = list->tree;
    MVMint32 i, j;

    alc->sets = MVM_calloc(tree->nodes_num, sizeof(UnionFind));
    /* TODO: add count for ARGLIST refs, which can be > 3 per 'tile' */
    alc->refs = MVM_calloc(list->items_num * 4, sizeof(ValueRef));
    alc->refs_num = 0;

    MVM_VECTOR_INIT(alc->values,   list->items_num);
    MVM_VECTOR_INIT(alc->worklist, list->items_num);

    for (i = 0; i < list->items_num; i++) {
        MVMJitTile *tile = list->items[i];
        MVMint32    node = tile->node;
        /* Each of the following counts as either an alias or as a PHI (in case
         * of IF), and thus these are not actual definitions */
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
            /* NB; this may cause a conflict, in which case we can resolve it by
             * creating a new live range or inserting a copy */
            alc->sets[node].key  = value_set_union(alc->sets, alc->values, left_cond, right_cond);
        } else {
            /* create a live range if necessary */
            if (MVM_JIT_TILE_YIELDS_VALUE(tile)) {
                MVMint8 register_spec = MVM_JIT_REGISTER_FETCH(tile->register_spec, 0);
                MVMint32 idx          = live_range_init(alc);
                alc->sets[node].key   = node;
                alc->sets[node].idx   = idx;
                live_range_add_ref(alc, alc->values + idx, i, 0);
                if (MVM_JIT_REGISTER_HAS_REQUIREMENT(register_spec)) {
                    alc->values[idx].register_spec = register_spec;
                }
                MVM_VECTOR_PUSH(alc->worklist, idx);
            }
            /* account for uses */
            for (j = 0; j < tile->num_refs; j++) {
                MVMint8  register_spec = MVM_JIT_REGISTER_FETCH(tile->register_spec, j+1);
                if (MVM_JIT_REGISTER_HAS_REQUIREMENT(register_spec)) {
                    /* TODO - this may require resolving conflicting register
                     * specifications */
                    NYI(use_register_spec);
                }
                if (MVM_JIT_REGISTER_IS_USED(register_spec)) {
                    MVMint32 idx = value_set_find(alc->sets, tile->refs[j])->idx;
                    live_range_add_ref(alc, alc->values + idx, i, j + 1);
                }
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
        if (last_ref(&alc->values[b]) > last_ref(&alc->values[a])) {
            /* insert a before b */
            memmove(alc->active + i + 1, alc->active + i, sizeof(MVMint32)*(alc->active_top - i));
            alc->active[i] = a;
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
        MVMint8 reg_num = alc->values[v].reg_num;
        if (last_ref(&alc->values[v]) > position) {
            break;
        } else {
            _DEBUG("Live range %d is out of scope (last ref %d, %d) and releasing register %d\n",
                    v, last_ref(alc->values + v), position, reg_num);
            free_register(tc, alc, MVM_JIT_STORAGE_GPR, reg_num);
        }
    }

    /* shift off the first x values from the live set. */
    if (i > 0) {
        MVM_VECTOR_APPEND(alc->retired, alc->active, i);
        MVM_VECTOR_SHIFT(alc->active, i);
    }
}


static void spill_register(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list, MVMint32 code_position) {
    /* choose a live range, a register to spill, and a spill location */
    MVMint32 v          = alc->active[--alc->active_top];
    MVMint32 spill_pos  = alc->compiler->spill_top;
    MVMint8 reg_spilled = alc->values[v].reg_num;

    /* loop over all value refs */
    ValueRef *head            = alc->values[v].first;
    alc->compiler->spill_top += sizeof(MVMRegister);
    _DEBUG("Starting spill of register %d starting from %d\n", reg_spilled, code_position);
    _DEBUG("Live range from %d to %d\n", first_ref(alc->values + v), last_ref(alc->values + v));
    while (head != NULL) {
        /* make a new live range */
        MVMint32 n = live_range_init(alc);
        LiveRange *range = alc->values + n;

        MVMint32 insert_pos = head->tile_idx, insert_order = 0;
        MVMJitTile *synth;
        if (is_definition(head)) {
            _DEBUG("Adding a store to position %d after the definition (tile %d)\n", spill_pos, head->tile_idx);
            /* insert a store after a definition */
            synth = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_store, -1, 1, spill_pos);
            range->synthetic[1] = synth;
            range->synth_pos[1] = insert_pos;
            /* directly after the instruction */
            insert_order        = -1;
        } else {
            /* insert a load prior to the use */
            _DEBUG("Adding a load from position %d before use (tile %d)\n", spill_pos, head->tile_idx);
            synth = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_load, -1, 1, spill_pos);
            range->synthetic[0] = synth;
            /* decrement insert_pos and assign to synth_pos so that it is properly ordered */
            range->synth_pos[0] = insert_pos--;
            /* last thing before the op */
            insert_order        = 1;
        }
        synth->args[1] = insert_pos;
        MVM_jit_tile_list_insert(tc, list, synth, insert_pos, insert_order);

        if (order_nr(head->tile_idx) < code_position) {
            /* in the past, which means we can safely use the spilled register
             * and immediately retire this live range */
            if (is_definition(head)) {
                synth->values[1] = reg_spilled;
            } else {
                synth->values[0] = reg_spilled;
            }
            _DEBUG("Retiring newly created live range %d for pos %d\n", n, insert_pos);
            MVM_VECTOR_PUSH(alc->retired, n);
        } else {
            /* in the future, which means we need to add it to the worklist */
            _DEBUG("Adding newly created live range %d to worklist for pos %d\n", n, insert_pos);
            MVM_VECTOR_ENSURE_SPACE(alc->worklist, 1);
            live_range_heap_push(alc->values, alc->worklist, &alc->worklist_num, n);
        }

        /* continue, and split off the node */
        range->first = range->last = head;
        head = head->next;
        range->first->next = NULL;
    }
    alc->values[v].spill_pos = spill_pos;
    free_register(tc, alc, MVM_JIT_STORAGE_GPR, reg_spilled);
    MVM_VECTOR_PUSH(alc->spilled, v);
}

static void split_live_range(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 which, MVMint32 from, MVMint32 to) {
}


/* not sure if this is sufficiently general-purpose and unconfusing */
#define MVM_VECTOR_ASSIGN(a,b) do {             \
        a = b;                                  \
        a ## _top = b ## _top;                  \
        a ## _alloc = b ## _alloc;              \
    } while (0);


static void linear_scan(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVM_VECTOR_INIT(alc->retired, alc->worklist_num);
    MVM_VECTOR_INIT(alc->spilled, 8);
    _DEBUG("STARTING LINEAR SCAN\n\n");
    while (alc->worklist_num > 0) {
        MVMint32 v   = live_range_heap_pop(alc->values, alc->worklist, &alc->worklist_num);
        MVMint32 pos = first_ref(alc->values + v);
        MVMint8 reg;
        _DEBUG("Processing live range %d (first ref %d, last ref %d)\n", v, first_ref(alc->values + v), last_ref(alc->values + v));
        /* NB: Should I have a compaction step to remove these? */
        if (live_range_is_empty(alc->values + v))
            continue;

        /* assign registers in loop */
        active_set_expire(tc, alc, pos);
        if (MVM_JIT_REGISTER_HAS_REQUIREMENT(alc->values[v].register_spec)) {
            reg = MVM_JIT_REGISTER_REQUIREMENT(alc->values[v].register_spec);
            if (NVR_GPR_BITMAP & (1 << reg)) {
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
    _DEBUG("END OF LINEAR SCAN\n\n");
}


void MVM_jit_linear_scan_allocate(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTileList *list) {
    RegisterAllocator alc;
    /* initialize allocator */
    alc.compiler = compiler;
    /* restart spill stack */

    alc.active_top = 0;
    memset(alc.active, -1, sizeof(alc.active));

    alc.reg_give = alc.reg_take = 0;
    memcpy(alc.reg_ring, available_gpr,
           sizeof(available_gpr));

    /* run algorithm */
    determine_live_ranges(tc, &alc, list);
    linear_scan(tc, &alc, list);

    /* deinitialize allocator */
    MVM_free(alc.sets);
    MVM_free(alc.refs);
    MVM_free(alc.values);

    MVM_free(alc.worklist);
    MVM_free(alc.retired);
    MVM_free(alc.spilled);


    /* make edits effective */
    MVM_jit_tile_list_edit(tc, list);

}
