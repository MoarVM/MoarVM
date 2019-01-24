#include "moar.h"
#include "internal.h"



#define NYI(x) MVM_oops(tc, #x  "not yet implemented")
#define _ASSERT(b, msg) if (!(b)) do { MVM_panic(1, msg); } while (0)

#if MVM_JIT_DEBUG
#define _DEBUG(fmt, ...) do { fprintf(stderr, fmt "%s", __VA_ARGS__, "\n"); } while(0)
#else
#define _DEBUG(fmt, ...) do {} while(0)
#endif


typedef struct {
    MVMint32 key;
    MVMint32 idx;
} UnionFind;

typedef struct ValueRef ValueRef;
struct ValueRef {
    MVMint32  tile_idx;
    MVMint32  value_idx;
    ValueRef *next;
};

struct Hole {
    MVMint32 start, end;
    struct Hole *next;
};

typedef struct {
    /* double-ended queue of value refs */
    ValueRef *first, *last;
    /* order number of first and last refs */
    MVMint32 start, end;

    /* list of holes in ascending order */
    struct Hole *holes;

    /* We can have at most two synthetic tiles, one attached to the first
     * definition and one to the last use... we could also point directly into
     * the values array of the tile, but it is not directly necessary */
    MVMJitTile *synthetic[2];

    MVMint8            register_spec;
    MVMJitStorageClass reg_cls;
    MVMint32           reg_num;
    MVMint8            reg_type;


    MVMint32           spill_pos;
    MVMint32           spill_idx;
} LiveRange;


typedef struct {
    MVMJitCompiler *compiler;

    /* Sets of values */
    UnionFind *sets;

    /* single buffer for uses, definitions */
    ValueRef *refs;
    MVMint32  refs_num;

    /* single buffer for number of holes */
    struct Hole *holes;
    MVMint32 holes_top;

    /* All values ever defined by the register allcoator */
    MVM_VECTOR_DECL(LiveRange, values);

    /* 'Currently' active values */
    MVMint32 active_top;
    MVMint32 active[MVM_JIT_ARCH_NUM_REG];

    /* Values still left to do (heap) */
    MVM_VECTOR_DECL(MVMint32, worklist);
    /* Retired values (to be assigned registers) (heap) */
    MVM_VECTOR_DECL(MVMint32, retired);
    /* Spilled values */
    MVM_VECTOR_DECL(MVMint32, spilled);

    /* Currently free registers */
    MVMBitmap reg_free;
} RegisterAllocator;


/* For first/last ref comparison, the tile indexes are doubled, and the indexes
 * of synthetics are biased with +1/-1. We use this extra space on the number
 * line to ensure consistent ordering and expiring behavior for 'synthetic' live
 * ranges that either start before an instruction (loading a required value) or
 * end just after one (storing the produced value). Without this, ordering
 * problems can cause two 'atomic' live ranges to be allocated and expired
 * before their actual last use */
MVM_STATIC_INLINE MVMint32 order_nr(MVMint32 tile_idx) {
    return tile_idx * 2;
}


MVM_STATIC_INLINE MVMint32 is_definition(ValueRef *v) {
    return (v->value_idx == 0);
}

MVM_STATIC_INLINE MVMint32 is_arglist_ref(MVMJitTileList *list, ValueRef *v) {
    return (list->items[v->tile_idx]->op == MVM_JIT_ARGLIST);
}


MVM_STATIC_INLINE MVMint32 live_range_is_empty(LiveRange *range) {
    return (range->first == NULL &&
            range->synthetic[0] == NULL &&
            range->synthetic[1] == NULL);
}

/* allocate a new live range value by pointer-bumping */
MVMint32 live_range_init(RegisterAllocator *alc) {
    LiveRange *range;
    MVMint32 idx = alc->values_num++;
    MVM_VECTOR_ENSURE_SIZE(alc->values, idx);
    alc->values[idx].spill_idx = INT32_MAX;
    alc->values[idx].start     = INT32_MAX;
    return idx;
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

    range->start = MIN(order_nr(tile_idx), range->start);
    range->end   = MAX(order_nr(tile_idx), range->end);
}


/* merge value ref sets */
static void live_range_merge(LiveRange *a, LiveRange *b) {
    ValueRef *head = NULL, *tail = NULL;
    MVMint32 i;
    _DEBUG("Merging live ranges (%d-%d) and (%d-%d)",
           (a)->start, (a)->end, (b)->start, (b)->end);
    if (a->start <= b->start) {
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
    }
    a->start = MIN(a->start, b->start);
    a->end   = MAX(a->end, b->end);
    /* deinitialize the live range */
    b->start = INT32_MAX;
    b->end   = 0;
}

static struct Hole * live_range_has_hole(LiveRange *value, MVMint32 order_nr) {
    struct Hole *h;
    /* By construction these are in linear ascending order, and never overlap */
    for (h = value->holes; h != NULL && h->start <= order_nr; h = h->next) {
        if (h->end >= order_nr)
            return h;
    }
    return NULL;
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
    if (values[sets[b].idx].start < values[sets[a].idx].start) {
        /* ensure we're picking the first one to start so that we maintain the
         * first-definition heap order */
        MVMint32 t = a; a = b; b = t;
    }
    sets[b].key = a; /* point b to a */
    live_range_merge(values + sets[a].idx, values + sets[b].idx);
    return a;
}


MVM_STATIC_INLINE void heap_swap(MVMint32 *heap, MVMint32 a, MVMint32 b) {
    MVMint32 t = heap[a];
    heap[a]    = heap[b];
    heap[b]    = t;
}

/* Functions to maintain a heap of references to the live ranges */
void live_range_heap_down(LiveRange *values, MVMint32 *heap, MVMint32 top, MVMint32 item,
                          MVMint32 (*cmp)(LiveRange *values, MVMint32 a, MVMint32 b)) {
    while (item < top) {
        MVMint32 left = item * 2 + 1;
        MVMint32 right = left + 1;
        MVMint32 swap;
        if (right < top) {
            swap = cmp(values, heap[left], heap[right]) < 0 ? left : right;
        } else if (left < top) {
            swap = left;
        } else {
            break;
        }
        if (cmp(values, heap[swap], heap[item]) < 0) {
            heap_swap(heap, swap, item);
            item       = swap;
        } else {
            break;
        }
    }
}

void live_range_heap_up(LiveRange *values, MVMint32 *heap, MVMint32 item,
                        MVMint32 (*cmp)(LiveRange* values, MVMint32 a, MVMint32 b)) {
    while (item > 0) {
        MVMint32 parent = (item-1)/2;
        if (cmp(values, heap[parent], heap[item]) > 0) {
            heap_swap(heap, item, parent);
            item = parent;
        } else {
            break;
        }
    }
}

MVMint32 live_range_heap_pop(LiveRange *values, MVMint32 *heap, size_t *top,
                             MVMint32 (*cmp)(LiveRange* values, MVMint32 a, MVMint32 b)) {
    MVMint32 v = heap[0];
    MVMint32 t = --(*top);
    /* pop by swap and heap-down */
    heap[0]    = heap[t];
    live_range_heap_down(values, heap, t, 0, cmp);
    return v;
}

void live_range_heap_push(LiveRange *values, MVMint32 *heap, size_t *top, MVMint32 v,
                          MVMint32 (*cmp)(LiveRange* values, MVMint32 a, MVMint32 b)) {
    /* NB, caller should use MVM_ENSURE_SPACE prior to calling */
    MVMint32 t = (*top)++;
    heap[t] = v;
    live_range_heap_up(values, heap, t, cmp);
}

MVMint32 live_range_heap_peek(LiveRange *values, MVMint32 *heap) {
    return values[heap[0]].start;
}

void live_range_heapify(LiveRange *values, MVMint32 *heap, MVMint32 top,
                        MVMint32 (*cmp)(LiveRange* values, MVMint32 a, MVMint32 b)) {
    MVMint32 i = top, mid = top/2;
    while (i-- > mid) {
        live_range_heap_up(values, heap, i, cmp);
    }
}


MVMint32 values_cmp_first_ref(LiveRange *values, MVMint32 a, MVMint32 b) {
    return values[a].start - values[b].start;
}

MVMint32 values_cmp_last_ref(LiveRange *values, MVMint32 a, MVMint32 b) {
    return values[a].end - values[b].end;
}

/* register assignment logic */
MVMint8 get_register(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitStorageClass reg_cls) {
    /* ignore storage class for now */
    MVMint8 reg_num = MVM_FFS(alc->reg_free) - 1;
    if (reg_num >= 0) {
        MVM_bitmap_delete(&alc->reg_free, reg_num);
    }
    return reg_num;
}

void free_register(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitStorageClass reg_cls, MVMint8 reg_num) {
    if (MVM_bitmap_get_low(alc->reg_free, reg_num)) {
        MVM_oops(tc, "Register %d is already free", reg_num);
    }
    MVM_bitmap_set_low(&alc->reg_free, reg_num);
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
        if (is_arglist_ref(list, ref)) {
            /* don't assign registers to ARGLIST references, that will never
             * work */
            continue;
        } else {
            MVMJitTile *tile = list->items[ref->tile_idx];
            tile->values[ref->value_idx] = reg_num;
        }
    }

    for (i = 0; i < 2; i++) {
        MVMJitTile *tile = range->synthetic[i];
        if (tile != NULL) {
            tile->values[i] = reg_num;
        }
    }
}


MVM_STATIC_INLINE void close_hole(RegisterAllocator *alc, MVMint32 ref, MVMint32 tile_idx) {
    LiveRange *v = alc->values + ref;
    if (v->holes && v->holes->start < order_nr(tile_idx)) {
        v->holes->start = order_nr(tile_idx);
        _DEBUG("Closed hole in live range %d at %d", ref, order_nr(tile_idx));
    }
}

MVM_STATIC_INLINE void open_hole(RegisterAllocator *alc, MVMint32 ref, MVMint32 tile_idx) {
    LiveRange *v = alc->values + ref;
    if (v->start < order_nr(tile_idx) &&
        (v->holes == NULL || v->holes->start > order_nr(tile_idx))) {
        struct Hole *hole = alc->holes + alc->holes_top++;
        hole->next  = v->holes;
        hole->start = 0;
        hole->end   = order_nr(tile_idx);
        v->holes    = hole;
        _DEBUG("Opened hole in live range %d at %d", ref, order_nr(tile_idx));
    }
}

/* Find holes in live ranges, as per Wimmer (2010). This is required only
 * because the spill-strategy arround CALLs is (sometimes) to load-and-restore,
 * rather than do a full spill, in the not-so-rare case that many of the live
 * values will be temporaries and the call is only necessary in a branch */

static void find_holes(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMint32 i, j, k;

    MVMJitExprTree *tree = list->tree;
    MVMint32 bitmap_size = (alc->values_num >> 5) + 1;

 /* convenience macros */
#define _BITMAP(_a)   (bitmaps + (_a)*bitmap_size)
#define _SUCC(_a, _z) (list->blocks[(_a)].succ[(_z)])

    MVMBitmap *bitmaps = MVM_calloc(list->blocks_num + 1, sizeof(MVMBitmap) * bitmap_size);
    /* last bitmap is allocated to hold diff, which is how we know which live
     * ranges holes potentially need to be closed */
    MVMBitmap *diff    = _BITMAP(list->blocks_num);

    for (j = list->blocks_num - 1; j >= 0; j--) {
        MVMBitmap *live_in = _BITMAP(j);
        MVMint32 start = list->blocks[j].start, end = list->blocks[j].end;
        if (list->blocks[j].num_succ == 2) {
            /* live out is union of successors' live_in */
            MVMBitmap *a = _BITMAP(_SUCC(j, 0)), *b =  _BITMAP(_SUCC(j, 1));

            MVM_bitmap_union(live_in, a, b, bitmap_size);
            MVM_bitmap_difference(diff, a, b, bitmap_size);

            for (k = 0; k < bitmap_size; k++) {
                MVMBitmap additions = diff[k];
                while (additions) {
                    MVMint32 bit = MVM_FFS(additions) - 1;
                    MVMint32 val = (k << 6) + bit;
                    close_hole(alc, val, end);
                    MVM_bitmap_delete(&additions, bit);
                }
            }
        } else if (list->blocks[j].num_succ == 1) {
            memcpy(live_in, _BITMAP(_SUCC(j, 0)),
                   sizeof(MVMBitmap) * bitmap_size);
        }

        for (i = end - 1; i >= start; i--) {
            MVMJitTile *tile = list->items[i];
            if (tile->op == MVM_JIT_ARGLIST) {
                /* list of uses, all very real */
                MVMint32 nchild = MVM_JIT_EXPR_NCHILD(tree, tile->node);
                MVMint32 *args  = MVM_JIT_EXPR_LINKS(tree, tile->node);
                for (k = 0; k < nchild; k++) {
                    MVMint32 ref  = value_set_find(alc->sets, MVM_JIT_EXPR_LINKS(tree, args[k])[0])->idx;
                    if (!MVM_bitmap_get(live_in, ref)) {
                        MVM_bitmap_set(live_in, ref);
                        close_hole(alc, ref, i);
                    }
                }
            } else if (tile->op == MVM_JIT_IF || tile->op == MVM_JIT_DO || tile->op == MVM_JIT_COPY) {
                /* not a real use, no work needed here (we already merged them) */
            } else {
                /* If a value is used and defined by the same tile, then the
                 * 'hole' only covers that single tile. The definitions must
                 * therefore be handled before the uses */

                if (MVM_JIT_TILE_YIELDS_VALUE(tile)) {
                    MVMint32 ref = value_set_find(alc->sets, tile->node)->idx;
                    open_hole(alc, ref, i);
                    MVM_bitmap_delete(live_in, ref);
                }
                for (k = 0; k < tile->num_refs; k++) {
                    if (MVM_JIT_REGISTER_IS_USED(MVM_JIT_REGISTER_FETCH(tile->register_spec, k+1))) {
                        MVMint32 ref = value_set_find(alc->sets, tile->refs[k])->idx;
                        if (!MVM_bitmap_get(live_in, ref)) {
                            MVM_bitmap_set(live_in, ref);
                            close_hole(alc, ref, i);
                        }
                    }
                }
            }
        }
    }
    MVM_free(bitmaps);

#undef _BITMAP
#undef _SUCC
}


static void determine_live_ranges(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMint32 i, j, n;
    MVMint32 num_phi = 0; /* pessimistic but correct upper bound of number of holes */
    MVMJitExprTree *tree = list->tree;

    alc->sets = MVM_calloc(tree->nodes_num, sizeof(UnionFind));
    /* up to 4 refs per tile (1 out, 3 in) plus the number of refs per arglist */
    alc->refs = MVM_calloc(list->items_num * 4 + list->num_arglist_refs, sizeof(ValueRef));
    alc->refs_num = 0;

    MVM_VECTOR_INIT(alc->values,   list->items_num);
    MVM_VECTOR_INIT(alc->worklist, list->items_num);

    for (i = 0; i < list->items_num; i++) {
        MVMJitTile *tile = list->items[i];
        MVMint32    node = tile->node;
        /* Each of the following counts as either an alias or as a PHI (in case
         * of IF), and thus these are not actual definitions */
        if (tile->op == MVM_JIT_COPY) {
            MVMint32 ref        = MVM_JIT_EXPR_LINKS(tree, node)[0];
            _DEBUG("Unify COPY node (%d -> %d)", tile->node, ref);
            alc->sets[node].key = ref; /* point directly to actual definition */
        } else if (tile->op == MVM_JIT_DO) {
            MVMint32 last_child = MVM_JIT_EXPR_FIRST_CHILD(tree, node) + MVM_JIT_EXPR_NCHILD(tree, node) - 1;
            MVMint32 ref        = tree->nodes[last_child];
            _DEBUG("Unify COPY DO (%d -> %d)", tile->node, ref);
            alc->sets[node].key = ref;
        } else if (tile->op == MVM_JIT_IF) {
            MVMint32 *links     = MVM_JIT_EXPR_LINKS(tree, node);
            /* NB; this may cause a conflict in register requirements, in which
             * case we should resolve it by creating a new live range or inserting
             * a copy */
            alc->sets[node].key  = value_set_union(alc->sets, alc->values, links[1], links[2]);
            _DEBUG("Merging nodes %d and %d to %d (result key = %d)", links[1], links[2], node, alc->sets[node].key);
            num_phi++;
        } else if (tile->op == MVM_JIT_ARGLIST) {
            MVMint32 num_args = MVM_JIT_EXPR_NCHILD(tree, node);
            MVMint32 *refs = MVM_JIT_EXPR_LINKS(tree, node);
            _DEBUG("Adding %d references to ARGLIST node", num_args);
            for (j = 0; j < num_args; j++) {
                MVMint32 value = MVM_JIT_EXPR_LINKS(tree, refs[j])[0];
                MVMint32 idx   = value_set_find(alc->sets, value)->idx;
                _DEBUG("  Reference %d", idx);
                live_range_add_ref(alc, alc->values + idx, i, j + 1);
                /* include the CALL node into the arglist child range, so we
                 * don't release them too early */
                alc->values[idx].end = MAX(alc->values[idx].end, order_nr(i + 1));
            }
        } else {
            /* create a live range if necessary */
            if (MVM_JIT_TILE_YIELDS_VALUE(tile)) {
                MVMint8 register_spec = MVM_JIT_REGISTER_FETCH(tile->register_spec, 0);
                MVMint32 idx          = live_range_init(alc);
                alc->sets[node].key   = node;
                alc->sets[node].idx   = idx;
                _DEBUG("Create live range %d (tile=%d, node=%d)", idx,i, node);
                live_range_add_ref(alc, alc->values + idx, i, 0);
                if (MVM_JIT_REGISTER_HAS_REQUIREMENT(register_spec)) {
                    alc->values[idx].register_spec = register_spec;
                }
                MVM_VECTOR_PUSH(alc->worklist, idx);
            }
            /* account for uses */
            for (j = 0; j < tile->num_refs; j++) {
                MVMint8  register_spec = MVM_JIT_REGISTER_FETCH(tile->register_spec, j+1);
                /* any 'use' register requirements are handled in the allocation step */
                if (MVM_JIT_REGISTER_IS_USED(register_spec)) {
                    MVMint32 idx = value_set_find(alc->sets, tile->refs[j])->idx;
                    _DEBUG("Adding reference to live range %d from tile %d", idx, i);
                    live_range_add_ref(alc, alc->values + idx, i, j + 1);
                }
            }
        }
        if (MVM_JIT_TILE_YIELDS_VALUE(tile) && MVM_JIT_EXPR_INFO(tree, node)->type != 0) {
            LiveRange *range = alc->values + value_set_find(alc->sets, node)->idx;
            /* compare only the lower bits, because (for storage purposes) we
             * don't care about the signed/unsigned disticntion */
            _ASSERT(range->reg_type == 0 || (range->reg_type & 0xf) == (MVM_JIT_EXPR_INFO(tree, node)->type & 0xf),
                    "Register types do not match between value and node");
            /* shift to match MVM_reg_types. should arguably be a macro maybe */
            range->reg_type = MVM_JIT_EXPR_INFO(tree, node)->type;
            _DEBUG( "Assigned type: %d to live range %d\n", range->reg_type, range - alc->values);
        }
    }
    if (num_phi > 0) {
        /* If there are PHI nodes, there will be holes.
         * The array allocated here will be used to construct linked lists */
        alc->holes     = MVM_malloc(num_phi * sizeof(struct Hole));
        alc->holes_top = 0;
        find_holes(tc, alc, list);

        /* eliminate empty values from the worklist */
        for (i = 0, j = 0; j < alc->worklist_num; j++) {
            if (!live_range_is_empty(alc->values + alc->worklist[j])) {
                alc->worklist[i++] = alc->worklist[j];
            }
        }
        alc->worklist_num = i;

    } else {
        alc->holes = NULL;
        alc->holes_top = 0;
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
        if (alc->values[b].end > alc->values[a].end) {
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
static void active_set_expire(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 order_nr) {
    MVMint32 i;
    for (i = 0; i < alc->active_top; i++) {
        MVMint32 v = alc->active[i];
        MVMint8 reg_num = alc->values[v].reg_num;
        if (alc->values[v].end > order_nr) {
            break;
        } else {
            _DEBUG("Live range %d is out of scope (last ref %d, %d) and releasing register %d",
                   v, alc->values[v].end, order_nr, reg_num);
            free_register(tc, alc, MVM_JIT_STORAGE_GPR, reg_num);
        }
    }
    /* shift off the first x values from the live set. */
    if (i > 0) {
        MVM_VECTOR_APPEND(alc->retired, alc->active, i);
        alc->active_top -= i;
        if (alc->active_top > 0) {
            memmove(alc->active, alc->active + i,
                    sizeof(alc->active[0]) * alc->active_top);
        }
    }
}

/* Compute the earliest live range that is still active. */
static MVMint32 earliest_active_value(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 tile_order_nr) {
    /* can we cache this, and does it make sense to do so? */
    int i;
    for (i = 0; i < alc->active_top; i++) {
        tile_order_nr = MIN(tile_order_nr, alc->values[alc->active[i]].start);
    }
    return tile_order_nr;
}

static void active_set_splice(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 to_splice) {
    MVMint32 i ;
    /* find (reverse, because it's usually the last); predecrement alc->active_top because we're removing one item */
    for (i = --alc->active_top; i >= 0; i--) {
        if (alc->active[i] == to_splice)
            break;
    }
    if (i >= 0 && i < alc->active_top) {
        /* shift out */
        memmove(alc->active + i, alc->active + i + 1,
                sizeof(alc->active[0]) * alc->active_top - i);
    }
}

static void spill_set_release(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 order_nr) {
    while (alc->spilled_num > 0 && alc->values[alc->spilled[0]].end <= order_nr) {
        MVMint32 spilled = live_range_heap_pop(alc->values, alc->spilled, &alc->spilled_num,
                                               values_cmp_last_ref);
        LiveRange *value = alc->values + spilled;
        _DEBUG("VM Register %d for live range %d can be released",
               value->spill_pos / sizeof(MVMRegister), spilled);
        MVM_jit_spill_memory_release(tc, alc->compiler, value->spill_pos, value->reg_type);
    }
}

static MVMint32 insert_load_before_use(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                                       ValueRef *ref, MVMint32 load_pos) {
    MVMint32 n = live_range_init(alc);
    MVMJitTile *tile = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_load, 2, 1,
                                         MVM_JIT_STORAGE_LOCAL, load_pos, 0);
    LiveRange *range = alc->values + n;
    tile->debug_name = "#load-before-use";
    MVM_jit_tile_list_insert(tc, list, tile, ref->tile_idx - 1, +1); /* insert just prior to use */
    range->synthetic[0] = tile;
    range->first = range->last = ref;

    range->start = order_nr(ref->tile_idx) - 1;
    range->end   = order_nr(ref->tile_idx);
    return n;
}

static MVMint32 insert_store_after_definition(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                                              ValueRef *ref, MVMint32 store_pos) {
    MVMint32 n       = live_range_init(alc);
    MVMJitTile *tile = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_store, 2, 2,
                                         MVM_JIT_STORAGE_LOCAL, store_pos, 0, 0);
    LiveRange *range  = alc->values + n;
    tile->debug_name = "#store-after-definition";
    MVM_jit_tile_list_insert(tc, list, tile, ref->tile_idx, -1); /* insert just after storage */
    range->synthetic[1] = tile;
    range->first = range->last = ref;

    range->start = order_nr(ref->tile_idx);
    range->end   = order_nr(ref->tile_idx) + 1;
    return n;
}

static MVMint32 select_live_range_for_spill(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list, MVMint32 code_pos) {
    return alc->active[alc->active_top-1];
}


static void live_range_spill(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                             MVMint32 to_spill, MVMint32 spill_pos, MVMint32 code_pos) {

    MVMint8 reg_spilled = alc->values[to_spill].reg_num;
    /* loop over all value refs */
    _DEBUG("Spilling live range value %d to memory position %d at %d", to_spill, spill_pos, code_pos);

    while (alc->values[to_spill].first != NULL) {
        /* make a new live range */
        MVMint32 n;

        /* shift current ref */
        ValueRef *ref = alc->values[to_spill].first;
        alc->values[to_spill].first = ref->next;
        ref->next = NULL;

        if (is_arglist_ref(list, ref) && order_nr(ref->tile_idx) > code_pos) {
            /* Never insert a load before a future ARGLIST; ARGLIST may easily
             * consume more registers than we have available. Past ARGLISTs have
             * already been handled, so we do need to insert a load a before
             * them (or modify in place, but, complex!). */
            continue;
        } else if (is_definition(ref)) {
            n = insert_store_after_definition(tc, alc, list, ref, spill_pos);
        } else {
            n = insert_load_before_use(tc, alc, list, ref, spill_pos);
        }

        if (order_nr(ref->tile_idx) < code_pos) {
            /* in the past, which means we can safely use the spilled register
             * and immediately retire this live range */
            assign_register(tc, alc, list, n, MVM_JIT_STORAGE_GPR, reg_spilled);
            MVM_VECTOR_PUSH(alc->retired, n);
        } else {
            /* in the future, which means we need to add it to the worklist */
            MVM_VECTOR_ENSURE_SPACE(alc->worklist, 1);
            live_range_heap_push(alc->values, alc->worklist, &alc->worklist_num, n,
                                 values_cmp_first_ref);
        }
    }

    /* clear value references */
    alc->values[to_spill].last = NULL;

    /* mark as spilled and store the spill position */
    alc->values[to_spill].spill_pos = spill_pos;
    alc->values[to_spill].spill_idx = code_pos;
    free_register(tc, alc, MVM_JIT_STORAGE_GPR, reg_spilled);
    MVM_VECTOR_ENSURE_SPACE(alc->spilled, 1);
    live_range_heap_push(alc->values, alc->spilled, &alc->spilled_num,
                         to_spill, values_cmp_last_ref);
}


static void prepare_arglist_and_call(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                                     MVMint32 arglist_idx, MVMint32 call_idx) {
    MVMJitTile *arglist_tile = list->items[arglist_idx],
                  *call_tile = list->items[call_idx];
    MVMJitExprTree *tree = list->tree;
    MVMint32 num_args = MVM_JIT_EXPR_NCHILD(tree, arglist_tile->node);
    MVMint32 *args    = MVM_JIT_EXPR_LINKS(tree, arglist_tile->node);
    MVMint32           arg_values[16];
    MVMJitStorageRef storage_refs[16];

    struct {
        MVMint8 in_reg; /* register number that is to be moved in */
        MVMint8 num_out; /* how many values need to make room for me */
    } topological_map[MVM_JIT_ARCH_NUM_REG]; /* reverse map for topological sort of moves */

    struct {
        MVMint8 reg_num;
        MVMint8 stack_pos;
    } stack_transfer[16];
    MVMint32 stack_transfer_top = 0;

    MVMint8 transfer_queue[16];
    MVMint32 transfer_queue_idx, transfer_queue_top = 0, transfers_required = 0;

    MVMint8 spilled_args[16];
    MVMint32 spilled_args_top = 0;

    MVMBitmap call_bitmap = 0, arg_bitmap = 0;

    MVMint8 spare_register;

    MVMint32 i, j, ins_pos = 2;

    /* get storage positions for arglist */
    MVM_jit_arch_storage_for_arglist(tc, alc->compiler, tree, arglist_tile->node, storage_refs);

    /* get value refs for arglist */
    for (i = 0; i < num_args; i++) {
        /* may refer to spilled live range */
        arg_values[i] = value_set_find(alc->sets, MVM_JIT_EXPR_LINKS(tree, args[i])[0])->idx;
    }

    _DEBUG("prepare_call: Got %d args", num_args);

    /* initialize topological map, use -1 as 'undefined' inboud value */
    for (i = 0; i < MVM_ARRAY_SIZE(topological_map); i++) {
        topological_map[i].num_out =  0;
        topological_map[i].in_reg  = -1;
    }

    for (i = 0, j = 0; i < alc->active_top; i++) {
        LiveRange *v = alc->values + alc->active[i];
        MVMint32 code_pos = order_nr(call_idx);
        if (v->end > code_pos && live_range_has_hole(v, code_pos) == NULL) {
            /* surviving values need to be spilled */
            MVMint32 spill_pos = MVM_jit_spill_memory_select(tc, alc->compiler, v->reg_type);
            /* spilling at the CALL idx will mean that the spiller inserts a
             * LOAD at the current register before the ARGLIST, meaning it
             * remains 'live' for this ARGLIST */
            _DEBUG("Spilling %d to %d at %d", alc->active[i], spill_pos, code_pos);
            live_range_spill(tc, alc, list, alc->active[i], spill_pos, code_pos);
        } else {
            /* compact the active set */
            alc->active[j++] = alc->active[i];
        }
    }
    alc->active_top = j;

    for (i = 0; i < num_args; i++) {
        LiveRange *v = alc->values + arg_values[i];
        if (v->spill_idx < order_nr(call_idx)) {
            /* spilled prior to the ARGLIST/CALL */
            spilled_args[spilled_args_top++] = i;
        } else if (storage_refs[i]._cls == MVM_JIT_STORAGE_GPR) {
            MVMint8 reg_num = storage_refs[i]._pos;
            if (reg_num != v->reg_num) {
                _DEBUG("Transfer Rq(%d) -> Rq(%d)", reg_num, v->reg_num);
                topological_map[reg_num].in_reg = v->reg_num;
                topological_map[v->reg_num].num_out++;
                transfers_required++;
            } else {
                _DEBUG("Transfer Rq(%d) not required", reg_num);
            }
        } else if (storage_refs[i]._cls == MVM_JIT_STORAGE_STACK) {
            /* enqueue for stack transfer */
            stack_transfer[stack_transfer_top].reg_num = v->reg_num;
            stack_transfer[stack_transfer_top].stack_pos = storage_refs[i]._pos;
            stack_transfer_top++;
            /* count the outbound edge */
            topological_map[v->reg_num].num_out++;
        } else {
            NYI(this_storage_class);
        }

        /* set bitmap */
        if (storage_refs[i]._cls == MVM_JIT_STORAGE_GPR) {
            MVMint8 reg_num = storage_refs[i]._pos;
            MVM_bitmap_set(&arg_bitmap, reg_num);
        }
    }

    _DEBUG("%d transfers required", transfers_required);
    /* NB: tile is inserted after tile at position, relative to the order. */
#define INSERT_TILE(_tile, _pos, _order) MVM_jit_tile_list_insert(tc, list, _tile, _pos, _order)
#define INSERT_NEXT_TILE(_tile) INSERT_TILE(_tile, arglist_idx, ins_pos++)
#define MAKE_TILE(_code, _narg, _nval, ...) MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_ ## _code, _narg, _nval, __VA_ARGS__)

#define INSERT_MOVE(_a, _b)          INSERT_NEXT_TILE(MAKE_TILE(move, 0, 2, _a, _b))
#define INSERT_COPY_TO_STACK(_s, _r) INSERT_NEXT_TILE(MAKE_TILE(store, 2, 2, MVM_JIT_STORAGE_STACK, _s, 0, _r))
#define INSERT_LOAD_LOCAL(_r, _l)    INSERT_NEXT_TILE(MAKE_TILE(load, 2, 1, MVM_JIT_STORAGE_LOCAL, _l, _r))
#define INSERT_LOCAL_STACK_COPY(_s, _l) \
    INSERT_NEXT_TILE(MAKE_TILE(memory_copy, 4, 2, MVM_JIT_STORAGE_STACK, _s, MVM_JIT_STORAGE_LOCAL, _l, 0, spare_register))

#define ENQUEUE_TRANSFER(_r) do { \
         MVMint8 _c = (_r); \
         if (--(topological_map[(_c)].num_out) == 0 &&  \
                topological_map[(_c)].in_reg   >= 0) { \
             transfer_queue[transfer_queue_top++] = _c; \
         } \
     } while(0)


    /* resolve conflicts for CALL; since we're spilling any surviving bits,
     * we can just move it to any free registers. */
    for (i = 0; i < call_tile->num_refs; i++) {
        MVMint8 spec = MVM_JIT_REGISTER_FETCH(call_tile->register_spec, i + 1);
        if (MVM_JIT_REGISTER_IS_USED(spec)) {
            MVMint8 reg = call_tile->values[i+1];
            MVM_bitmap_set(&call_bitmap, reg);
        }
    }

    while (call_bitmap & arg_bitmap) {
        /* we need the spare register for the cycle breaking */
        MVMBitmap free_reg = ~(call_bitmap | arg_bitmap | MVM_JIT_RESERVED_REGISTERS | MVM_JIT_SPARE_REGISTERS);
        /* FFS counts registers starting from 1 */
        MVMuint8 src = MVM_FFS(call_bitmap & arg_bitmap) - 1;
        MVMuint8 dst = MVM_FFS(free_reg) - 1;

        _ASSERT(free_reg != 0, "JIT: need to move a register but nothing is free");

        /* add edge */
        topological_map[dst].in_reg = src;
        topological_map[src].num_out++;

        /* update bitmap */
        MVM_bitmap_delete(&call_bitmap, src);
        MVM_bitmap_set(&call_bitmap, dst);

        /* update CALL args */
        for (i = 0; i < call_tile->num_refs; i++) {
            if (call_tile->values[i+1] == src) {
                call_tile->values[i+1] = dst;
            }
        }
    }


    /* at this point, all outbound edges have been created, and none have been
     * processed yet, so we can eqnueue all 'free' transfers */
    for (i = 0; i < MVM_ARRAY_SIZE(topological_map); i++) {
        if (topological_map[i].num_out == 0 &&
            topological_map[i].in_reg >= 0) {
            _DEBUG("Directly transfer %d -> %d", topological_map[i].in_reg, i);
            transfer_queue[transfer_queue_top++] = i;
        }
    }

    /* with call_bitmap and arg_bitmap given, we can determine the spare
     * register used for allocation; NB this may only be necessary in some
     * cases */
    spare_register = MVM_FFS(MVM_JIT_SPARE_REGISTERS) - 1;
    _ASSERT(spare_register >= 0, "JIT: No spare register for moves");

    for (i = 0; i < stack_transfer_top; i++) {
        MVMint8 reg_num = stack_transfer[i].reg_num;
        MVMint8 stk_pos = stack_transfer[i].stack_pos;
        INSERT_COPY_TO_STACK(stk_pos, reg_num);
        _DEBUG("Insert stack parameter: Rq(%d) -> [rsp+%d]", reg_num, stk_pos);
        ENQUEUE_TRANSFER(reg_num);
    }


    for (transfer_queue_idx = 0; transfer_queue_idx < transfer_queue_top; transfer_queue_idx++) {
        MVMint8 dst = transfer_queue[transfer_queue_idx];
        MVMint8 src = topological_map[dst].in_reg;
        _ASSERT(src >= 0, "No inboud edge (sort)");
        _DEBUG("Insert move (toposort): Rq(%d) -> Rq(%d)", src, dst);
        INSERT_MOVE(dst, src);
        ENQUEUE_TRANSFER(src);
    }

    if (transfer_queue_top < transfers_required) {
        /* suppose we have a cycle of transfers: a -> b -> c -> a;
         * since we only keep the one inbound register as a reference, the chain
         * is really:
         * a -> c -> b -> a
         * We can 'break' this cycle by walking the chain in this order, first
         * moving 'a' out of thee way into a spare register, then moving c to a,
         * b to c, and finally moving the spare register to 'b'
         */
        for (i = 0; i < MVM_ARRAY_SIZE(topological_map); i++) {
            if (topological_map[i].num_out > 0) {
                MVMint8 src, dst;
                INSERT_MOVE(spare_register, i);
                _ASSERT(--(topological_map[i].num_out) == 0,
                        "More than one outbound edge in cycle");
                for (dst = i, src = topological_map[i].in_reg; src != i;
                     dst = src, src = topological_map[src].in_reg) {
                    _ASSERT(src >= 0, "No inbound edge (cycle breaking)");
                    INSERT_MOVE(dst, src);
                    _ASSERT(--(topological_map[src].num_out) == 0,
                            "More than one outbound edge in cycle");
                }
                INSERT_MOVE(dst, spare_register);
            }
        }
    }

    /* now all that remains is to deal with spilled values */
    for (i = 0; i < spilled_args_top; i++) {
        MVMint32 arg = spilled_args[i];
        LiveRange *v = alc->values + arg_values[arg];
        if (storage_refs[arg]._cls == MVM_JIT_STORAGE_GPR) {
            _DEBUG("Loading spilled value to Rq(%d) from [rbx+%d]", storage_refs[arg]._pos, v->spill_pos);
            INSERT_LOAD_LOCAL(storage_refs[arg]._pos, v->spill_pos);
        } else if (storage_refs[arg]._cls == MVM_JIT_STORAGE_STACK) {
            INSERT_LOCAL_STACK_COPY(storage_refs[arg]._pos, v->spill_pos);
        } else {
            NYI(storage_class);
        }
    }
}

MVM_STATIC_INLINE void process_tile(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list, MVMint32 tile_cursor) {
    MVMJitTile *tile = list->items[tile_cursor];

    if (tile->op == MVM_JIT_ARGLIST) {
        MVMint32 arglist_idx = tile_cursor;
        MVMint32 call_idx    = tile_cursor + 1;
        _ASSERT(call_idx < list->items_num &&
                (list->items[call_idx]->op == MVM_JIT_CALL ||
                 list->items[call_idx]->op == MVM_JIT_CALLV),
                "ARGLIST tiles must be followed by CALL");
    } else if (tile->op == MVM_JIT_CALL || tile->op == MVM_JIT_CALLV) {
        MVMint32 arglist_idx = tile_cursor - 1;
        MVMint32 call_idx    = tile_cursor;
        _ASSERT(tile_cursor > 0 && list->items[tile_cursor - 1]->op == MVM_JIT_ARGLIST,
                "CALL must be preceded by ARGLIST");
        /*
         * CALL nodes can use values in registers, for example for dynamic
         * calls. These registers may conflict with registers used in ARGLIST,
         * in which case prepare_arglist_and_call will move the values to a free
         * register and update the call tile.
         *
         * However, as regular register-allocated values, the selected register
         * may be allocated for a synthetic LOAD tile after it had previously
         * been spilled. To ensure that allocation for the synthetic tile does
         * not overwrite the free register picked by the resolution code, we
         * must be sure that prepare_arglist_and_call will be run *after* all
         * registers have been allocated for the values used by the CALL tile.
         *
         * That is why prepare_arglist_and_call, which handles BOTH tiles, is
         * called for the CALL tile and not for the ARGLIST tile.
         */

        prepare_arglist_and_call(tc, alc, list, arglist_idx, call_idx);
    } else {
        MVMint32 i;
        /* deal with 'use' registers */
        for  (i = 1; i < tile->num_refs; i++) {
            MVMint8 spec = MVM_JIT_REGISTER_FETCH(tile->register_spec, i);
            if (MVM_JIT_REGISTER_IS_USED(spec) && MVM_JIT_REGISTER_HAS_REQUIREMENT(spec)) {
                /* we could use the register map here, but let's wait and see */
                NYI(tile_use_requirements);
            }
        }
    }
}

static void process_live_range(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list, MVMint32 v) {
    MVMint8 reg;
    MVMint32 tile_order_nr = alc->values[v].start;
    if (MVM_JIT_REGISTER_HAS_REQUIREMENT(alc->values[v].register_spec)) {
        reg = MVM_JIT_REGISTER_REQUIREMENT(alc->values[v].register_spec);
        if (MVM_bitmap_get_low(MVM_JIT_RESERVED_REGISTERS, reg)) {
            /* directly assign a reserved register */
            assign_register(tc, alc, list, v, MVM_JIT_STORAGE_GPR, reg);
        } else {
            /* TODO; might require swapping / spilling */
            NYI(general_purpose_register_spec);
        }
    } else {
        while ((reg = get_register(tc, alc, MVM_JIT_STORAGE_GPR)) < 0) {
            /* choose a live range, a register to spill, and a spill location */
            MVMint32 to_spill   = select_live_range_for_spill(tc, alc, list, tile_order_nr);
            MVMint32 spill_pos  = MVM_jit_spill_memory_select(tc, alc->compiler, alc->values[to_spill].reg_type);
            active_set_splice(tc, alc, to_spill);
            _DEBUG("Spilling live range %d at %d to %d to free up a register",
                   to_spill, tile_order_nr, spill_pos);
            live_range_spill(tc, alc, list, to_spill, spill_pos, tile_order_nr);
        }
        assign_register(tc, alc, list, v, MVM_JIT_STORAGE_GPR, reg);
        active_set_add(tc, alc, v);
    }

}

static void linear_scan(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMint32 tile_cursor = 0;
    MVM_VECTOR_INIT(alc->retired, alc->worklist_num);
    MVM_VECTOR_INIT(alc->spilled, 8);
    _DEBUG("STARTING LINEAR SCAN: %d/%d", tc->instance->jit_seq_nr, list->tree->seq_nr);
    /* loop over all tiles and peek on the value heap */
    while (tile_cursor < list->items_num) {

        if (alc->worklist_num > 0 && live_range_heap_peek(alc->values, alc->worklist) <= order_nr(tile_cursor)) {
            /* we've processed all tiles prior to this live range, but not the one that thie live range starts with */
            MVMint32 live_range = live_range_heap_pop(alc->values, alc->worklist, &alc->worklist_num, values_cmp_first_ref);
            MVMint32 tile_order_nr = alc->values[live_range].start;
            _DEBUG("Processing live range %d (first ref %d, last ref %d)",
                   live_range, alc->values[live_range].start, alc->values[live_range].end);
            active_set_expire(tc, alc, tile_order_nr);
            /* We can release the spill slot only if there is no more active
             * live range overlapping with its extent. Otherwise, when we reuse
             * the slot, we risk overwriting a useful value.
             *
             * We pass the current tile_order_nr as the upper bound (e.g. when
             * there may be no active live ranges, slots will still be useful if
             * they have later uses */
            spill_set_release(tc, alc,  earliest_active_value(tc,alc, tile_order_nr));
            process_live_range(tc, alc, list, live_range);
        } else {
            /* still have tiles to process, increment cursor */
            process_tile(tc, alc, list, tile_cursor++);
        }
    }

    /* flush active live ranges */
    active_set_expire(tc, alc, order_nr(list->items_num) + 1);
    spill_set_release(tc, alc, order_nr(list->items_num) + 1);
    _DEBUG("END OF LINEAR SCAN%s","\n");
}

/* Edit the assigned registers and insert copies to enforce the register requirements */
static void enforce_operand_requirements(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
#define MVM_JIT_ARCH_X64 1
#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64
    MVMint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(list->items); i++) {
        MVMJitTile *tile = list->items[i];
        /* Ensure two-operand order: a=op(b,c) must become a=op(a,xb)
         * 4 options are possible:
         * a == b : done
         * a != b, a != c: copy b to a, use a as b
         * a != b, a == c, op is commutative: use c as b, a as c
         * a != b, a == c, op is not commutative:
         *     use temparary as a, copy b to a, use temporary as b, copy temporary to c
         * This is valid because we never change a tile once it's assigned. */
        if (MVM_jit_expr_op_is_binary(tile->op) && tile->values[0] != tile->values[1]) {
            assert(tile->values[0] != 0);
            if (tile->num_refs == 2 && tile->values[0] == tile->values[2]) {
                if (MVM_jit_expr_op_is_commutative(tile->op)) {
                    /* We can change the order of values, and get the same result */
                    tile->values[2] = tile->values[1];
                    tile->values[1] = tile->values[0];
                } else {
                    /* Move to a temporary */
                    MVMint8 spare_register = MVM_FFS(MVM_JIT_SPARE_REGISTERS) - 1;
                    MVMJitTile *before = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_move, 0, 2, spare_register, tile->values[1]);
                    MVMJitTile *after  = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_move, 0, 2, tile->values[0], spare_register);
                    before->debug_name = "#move tmp <- in";
                    after->debug_name = "#move out <- tmp";
                    MVM_jit_tile_list_insert(tc, list, before, i - 1, 1024); /* very last thing before the tile */
                    MVM_jit_tile_list_insert(tc, list, after, i, -1024); /* very first thing after the tile */
                    tile->values[0] = tile->values[1] = spare_register;
                }
            } else {
                /* insert a copy to the output register */
                MVMJitTile *move = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_move, 0, 2, tile->values[0], tile->values[1]);
                move->debug_name = "#move out <- in";
                MVM_jit_tile_list_insert(tc, list, move, i - 1, 1024);
                tile->values[1] = tile->values[0];
            }
            assert(tile->values[0] == tile->values[1]);
        } else if (MVM_jit_expr_op_is_unary(tile->op) && tile->values[0] != tile->values[1] && tile->num_refs == 1) {
            MVMJitTile *move = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_move, 0, 2, tile->values[0], tile->values[1]);
            move->debug_name = "#move out <- in";
            MVM_jit_tile_list_insert(tc, list, move, i - 1, 1024);
            tile->values[1] = tile->values[0];
        }
    }
#endif
}



void MVM_jit_linear_scan_allocate(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTileList *list) {
    RegisterAllocator alc;
    /* initialize allocator */
    alc.compiler = compiler;
    /* restart spill stack */

    alc.active_top = 0;
    memset(alc.active, -1, sizeof(alc.active));

    alc.reg_free = MVM_JIT_AVAILABLE_REGISTERS;
    /* run algorithm */
    determine_live_ranges(tc, &alc, list);
    linear_scan(tc, &alc, list);
    enforce_operand_requirements(tc, &alc, list);

    /* deinitialize allocator */
    MVM_free(alc.sets);
    MVM_free(alc.refs);
    MVM_free(alc.holes);
    MVM_free(alc.values);

    MVM_free(alc.worklist);
    MVM_free(alc.retired);
    MVM_free(alc.spilled);


    /* make edits effective */
    MVM_jit_tile_list_edit(tc, list);

}
