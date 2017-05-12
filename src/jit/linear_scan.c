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
static const MVMint64 AVAILABLE_GPR_BITMAP = MVM_JIT_ARCH_AVAILABLE_GPR(SHIFT);
#undef SHIFT
#undef __COMMA__


#define MAX_ACTIVE sizeof(available_gpr)
#define NYI(x) MVM_oops(tc, #x  "not yet implemented")
#define _ASSERT(b, msg) if (!(b)) do { MVM_panic(1, msg); } while (0)

#if MVM_JIT_DEBUG
#define _DEBUG(fmt, ...) do { fprintf(stderr, fmt "%s", __VA_ARGS__, "\n"); } while(0);
#else
#define _DEBUG(fmt, ...) do {} while(0)
#endif

/* We need max and min macro's */
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b));
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b));
#endif


/* Efficient find-first-set; on x86, using `bsf` primitive operation; something
 * else on other architectures. */
#ifdef __GNUC__
/* also works for clang and friends */
#define FFS(x) __builtin_ffs(x)
#elif defined(_MSC_VER)
MVM_STATIC_INLINE MVMuint32 FFS(MVMuint32 x) {
    MVMuint32 i = 0;
    if (_BitScanForward(&i, x) == 0)
        return 0;
    return i + 1;
}
#else
/* fallback, note that i=0 if no bits are set */
MVM_STATIC_INLINE MVMuint32 FFS(MVMuint32 x) {
    MVMuint32 i = 0;
    while (x) {
        if (x & (1 << i++))
            break;
    }
    return i;
}
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

    /* list of holes in ascending order */
    struct Hole *holes;

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

    /* single buffer for number of holes */
    struct Hole *holes;
    MVMint32 holes_top;

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
MVM_STATIC_INLINE MVMint32 order_nr(MVMint32 tile_idx) {
    return tile_idx * 2;
}


/* quick accessors for common checks */
MVM_STATIC_INLINE MVMint32 first_ref(LiveRange *r) {
    MVMint32 a = r->first == NULL        ? INT32_MAX : order_nr(r->first->tile_idx);
    MVMint32 b = r->synthetic[0] == NULL ? INT32_MAX : order_nr(r->synth_pos[0]) - 1;
    return MIN(a,b);
}

MVM_STATIC_INLINE MVMint32 last_ref(LiveRange *r) {
    MVMint32 a = r->last == NULL         ? -1 : order_nr(r->last->tile_idx);
    MVMint32 b = r->synthetic[1] == NULL ? -1 : order_nr(r->synth_pos[1]) + 1;
    return MAX(a,b);
}

MVM_STATIC_INLINE MVMint32 is_definition(ValueRef *v) {
    return (v->value_idx == 0);
}

MVM_STATIC_INLINE MVMint32 is_arglist_ref(MVMJitTileList *list, ValueRef *v) {
    return (list->items[v->tile_idx]->op == MVM_JIT_ARGLIST);
}

/* allocate a new live range value by pointer-bumping */
MVMint32 live_range_init(RegisterAllocator *alc) {
    LiveRange *range;
    MVMint32 idx = alc->values_num++;
    MVM_VECTOR_ENSURE_SIZE(alc->values, idx);
    return idx;
}

MVM_STATIC_INLINE MVMint32 live_range_is_empty(LiveRange *range) {
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
    _DEBUG("Merging live ranges (%d-%d) and (%d-%d)",
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
    if (first_ref(values + sets[b].idx) < first_ref(values + sets[a].idx)) {
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

/* Witness the elegance of the bitmap for our purposes. */
MVM_STATIC_INLINE void bitmap_set(MVMuint64 *bits, MVMint32 idx) {
    bits[idx >> 6] |= (1 << (idx & 0x3f));
}

MVM_STATIC_INLINE MVMuint64 bitmap_get(MVMuint64 *bits, MVMint32 idx) {
    return bits[idx >> 6] & (1 << (idx & 0x3f));
}

MVM_STATIC_INLINE void bitmap_delete(MVMuint64 *bits, MVMint32 idx) {
    bits[idx >> 6] &= ~(1 << (idx & 0x3f));
}

MVM_STATIC_INLINE void bitmap_union_and_diff(MVMuint64 *u, MVMuint64 *d, MVMuint64 *a, MVMuint64 *b, MVMint32 n) {
    MVMint32 i;
    for (i = 0; i < n; i++) {
        u[i] = a[i] | b[i];
        d[i] = a[i] ^ b[i];
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
    if (first_ref(v) < order_nr(tile_idx) &&
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

    MVMint32 bitmap_size = (alc->values_num >> 5) + 1;

 /* convenience macros */
#define _BITMAP(_a)   (bitmaps + (_a)*bitmap_size)
#define _SUCC(_a, _z) (list->blocks[(_a)].succ[(_z)])

    MVMuint64 *bitmaps = MVM_calloc(list->blocks_num + 1, sizeof(MVMuint64) * bitmap_size);
    /* last bitmap is allocated to hold diff, which is how we know which live
     * ranges holes potentially need to be closed */
    MVMuint64 *diff    = _BITMAP(list->blocks_num);

    for (j = list->blocks_num - 1; j >= 0; j--) {
        MVMuint64 *live_in = _BITMAP(j);
        MVMint32 start = list->blocks[j].start, end = list->blocks[j].end;
        if (list->blocks[j].num_succ == 2) {
            /* live out is union of successors' live_in */
            bitmap_union_and_diff(live_in, diff, _BITMAP(_SUCC(j, 0)), _BITMAP(_SUCC(j, 1)), bitmap_size);
            for (k = 0; k < bitmap_size; k++) {
                MVMuint64 additions = diff[k];
                while (additions) {
                    MVMint32 bit = FFS(additions) - 1;
                    MVMint32 val = (k << 6) + bit;
                    additions &= ~(1 << bit);
                    close_hole(alc, val, end);
                }
            }
        } else if (list->blocks[j].num_succ == 1) {
            memcpy(live_in, _BITMAP(_SUCC(j, 0)),
                   sizeof(MVMuint64) * bitmap_size);
        }

        for (i = end - 1; i >= start; i--) {
            MVMJitTile *tile = list->items[i];
            if (tile->op == MVM_JIT_ARGLIST) {
                /* list of uses, all very real */
                MVMint32 nchild = list->tree->nodes[tile->node + 1];
                for (k = 0; k < nchild; k++) {
                    MVMint32 carg = list->tree->nodes[tile->node + 2 + k];
                    MVMint32 ref  = value_set_find(alc->sets, list->tree->nodes[carg + 1])->idx;
                    if (!bitmap_get(live_in, ref)) {
                        bitmap_set(live_in, ref);
                        close_hole(alc, ref, i);
                    }
                }
            } else if (tile->op == MVM_JIT_IF || tile->op == MVM_JIT_DO || tile->op == MVM_JIT_COPY) {
                /* not a real use, no work needed here (we already merged them) */
            } else {
                /* regular defintions and uses */
                for (k = 0; k < tile->num_refs; k++) {
                    if (MVM_JIT_REGISTER_IS_USED(MVM_JIT_REGISTER_FETCH(tile->register_spec, k+1))) {
                        MVMint32 ref = value_set_find(alc->sets, tile->refs[k])->idx;
                        if (!bitmap_get(live_in, ref)) {
                            bitmap_set(live_in, ref);
                            close_hole(alc, ref, i);
                        }
                    }
                }
                if (MVM_JIT_TILE_YIELDS_VALUE(tile)) {
                    MVMint32 ref = value_set_find(alc->sets, tile->node)->idx;
                    open_hole(alc, ref, i);
                    bitmap_delete(live_in, ref);
                }
            }
        }
    }
    MVM_free(bitmaps);

#undef _BITMAP
#undef _SUCC
}


static void determine_live_ranges(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMJitExprTree *tree = list->tree;
    MVMint32 i, j, n;
    MVMint32 num_phi = 0; /* pessimistic but correct upper bound of number of holes */

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
            MVMint32 ref        = tree->nodes[tile->node + 1];
            alc->sets[node].key = ref; /* point directly to actual definition */
        } else if (tile->op == MVM_JIT_DO) {
            MVMint32 nchild     = tree->nodes[tile->node + 1];
            MVMint32 ref        = tree->nodes[tile->node + 1 + nchild];
            alc->sets[node].key = ref;
        } else if (tile->op == MVM_JIT_IF) {
            MVMint32 left_cond   = tree->nodes[tile->node + 2];
            MVMint32 right_cond  = tree->nodes[tile->node + 3];
            /* NB; this may cause a conflict, in which case we can resolve it by
             * creating a new live range or inserting a copy */
            alc->sets[node].key  = value_set_union(alc->sets, alc->values, left_cond, right_cond);
            num_phi++;
        } else if (tile->op == MVM_JIT_ARGLIST) {
            MVMint32 num_args = list->tree->nodes[tile->node + 1];
            MVMJitExprNode *refs = list->tree->nodes + tile->node + 2;
            for (j = 0; j < num_args; j++) {
                MVMint32 carg  = refs[j];
                MVMint32 value = list->tree->nodes[carg+1];
                MVMint32 idx   = value_set_find(alc->sets, value)->idx;
                live_range_add_ref(alc, alc->values + idx, i, j + 1);
            }
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
                /* any 'use' register requirements are handled in the allocation step */
                if (MVM_JIT_REGISTER_IS_USED(register_spec)) {
                    MVMint32 idx = value_set_find(alc->sets, tile->refs[j])->idx;
                    live_range_add_ref(alc, alc->values + idx, i, j + 1);
                }
            }
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
static void active_set_expire(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 order_nr) {
    MVMint32 i;
    for (i = 0; i < alc->active_top; i++) {
        MVMint32 v = alc->active[i];
        MVMint8 reg_num = alc->values[v].reg_num;
        if (last_ref(&alc->values[v]) > order_nr) {
            break;
        } else {
            _DEBUG("Live range %d is out of scope (last ref %d, %d) and releasing register %d",
                    v, last_ref(alc->values + v), order_nr, reg_num);
            free_register(tc, alc, MVM_JIT_STORAGE_GPR, reg_num);
        }
    }

    /* shift off the first x values from the live set. */
    if (i > 0) {
        MVM_VECTOR_APPEND(alc->retired, alc->active, i);
        MVM_VECTOR_SHIFT(alc->active, i);
    }
}

static void active_set_splice(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 to_splice) {
    MVMint32 i ;
    /* find (reverse, because it's usually the last); predecrement alc->active_top */
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



static MVMint32 insert_load_before_use(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                                       ValueRef *ref, MVMint32 load_pos) {
    MVMint32 n = live_range_init(alc);
    MVMJitTile *tile = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_load, 2, 1,
                                         MVM_JIT_STORAGE_LOCAL, load_pos, 0);
    MVM_jit_tile_list_insert(tc, list, tile, ref->tile_idx - 1, +1); /* insert just prior to use */
    alc->values[n].synthetic[0] = tile;
    alc->values[n].synth_pos[0] = ref->tile_idx;
    alc->values[n].first = alc->values[n].last = ref;
    return n;
}

static MVMint32 insert_store_after_definition(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                                              ValueRef *ref, MVMint32 store_pos) {
    MVMint32 n       = live_range_init(alc);
    MVMJitTile *tile = MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_store, 2, 2,
                                         MVM_JIT_STORAGE_LOCAL, store_pos, 0, 0);
    MVM_jit_tile_list_insert(tc, list, tile, ref->tile_idx, -1); /* insert just after storage */
    alc->values[n].synthetic[1] = tile;
    alc->values[n].synth_pos[1] = ref->tile_idx;
    alc->values[n].first = alc->values[n].last = ref;
    return n;
}

static MVMint32 select_live_range_for_spill(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list, MVMint32 code_pos) {
    return alc->active[alc->active_top-1];
}

static MVMint32 select_memory_for_spill(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                                        MVMint32 code_pos, MVMuint32 size) {
    /* TODO: Implement a 'free list' of spillable locations */
    MVMint32 pos = alc->compiler->spill_top;
    alc->compiler->spill_top += size;
    return pos;
}


static void live_range_spill(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                             MVMint32 to_spill, MVMint32 spill_pos, MVMint32 code_pos) {
    LiveRange *spillee  = alc->values + to_spill;
    MVMint8 reg_spilled = spillee->reg_num;
    /* loop over all value refs */
    ValueRef **head     = &(spillee->first);
    while (*head != NULL) {
        /* make a new live range */
        MVMint32 n;
        /* shift current ref */
        ValueRef *ref = *head;
        *head         = ref->next;
        ref->next     = NULL;
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
            live_range_heap_push(alc->values, alc->worklist, &alc->worklist_num, n);
        }
    }
    /* mark as spilled and store the spill position */
    spillee->spill_pos = spill_pos;
    free_register(tc, alc, MVM_JIT_STORAGE_GPR, reg_spilled);
    MVM_VECTOR_PUSH(alc->spilled, to_spill);
}

static void spill_any_register(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list, MVMint32 code_position) {
    /* choose a live range, a register to spill, and a spill location */
    MVMint32 to_spill   = select_live_range_for_spill(tc, alc, list, code_position);
    MVMint32 spill_pos  = select_memory_for_spill(tc, alc, list, code_position, sizeof(MVMRegister));
    active_set_splice(tc, alc, to_spill);
    live_range_spill(tc, alc, list, to_spill, spill_pos, code_position);
}


/* not sure if this is sufficiently general-purpose and unconfusing */
#define MVM_VECTOR_ASSIGN(a,b) do {             \
        a = b;                                  \
        a ## _top = b ## _top;                  \
        a ## _alloc = b ## _alloc;              \
    } while (0);



static void prepare_arglist_and_call(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list,
                                     MVMint32 arglist_idx, MVMint32 call_idx) {
    MVMJitTile *arglist_tile = list->items[arglist_idx],
                  *call_tile = list->items[call_idx];
    MVMJitExprTree *tree = list->tree;
    MVMint32 num_args = tree->nodes[arglist_tile->node + 1];
    MVMint32           arg_values[16];
    MVMJitStorageRef storage_refs[16];

    MVMint8 register_map[MVM_JIT_ARCH_NUM_GPR];
    struct {
        MVMint8 in_reg; /* register number that is to be moved in */
        MVMint8 num_out; /* how many values need to make room for me */
    } topological_map[MVM_JIT_ARCH_NUM_GPR]; /* reverse map for topological sort of moves */

    struct {
        MVMint8 reg_num;
        MVMint8 stack_pos;
    } stack_transfer[16];
    MVMint32 stack_transfer_top = 0;

    MVMint8 transfer_queue[16];
    MVMint32 transfer_queue_idx, transfer_queue_top = 0, transfers_required = 0;

    MVMint8 spilled_args[16];
    MVMint32 spilled_args_top = 0;
    MVMint32 survivors[MAX_ACTIVE], survivors_num = 0;

    MVMuint32 call_bitmap = 0, arg_bitmap = 0;

    MVMint8 spare_register;

    MVMint32 i, ins_pos = 2;

    /* get storage positions for arglist */
    MVM_jit_arch_storage_for_arglist(tc, alc->compiler, tree, arglist_tile->node, storage_refs);

    /* get value refs for arglist */
    for (i = 0; i < num_args; i++) {
        MVMint32 carg  = tree->nodes[arglist_tile->node + 2 + i];
        arg_values[i] = value_set_find(alc->sets, tree->nodes[carg + 1])->idx; /* may refer to spilled live range */
    }

    _DEBUG("prepare_call: Got %d args", num_args);

    /* initialize topological map, use -1 as 'undefined' inboud value */
    for (i = 0; i < ARRAY_SIZE(topological_map); i++) {
        topological_map[i].num_out = 0;
        topological_map[i].in_reg      = -1;
    }
    memset(register_map, -1, sizeof(register_map));

    for (i = 0; i < alc->active_top; i++) {
        MVMint32 v = alc->active[i];
        MVMint8  r = alc->values[v].reg_num;
        LiveRange *l = alc->values + v;

        if (last_ref(l) >= order_nr(arglist_idx)) {
            register_map[r] = v;
        }
        if (last_ref(l) > order_nr(call_idx) &&
            /* if it has a hole arround CALL, it's not a survivor */
            live_range_has_hole(l, order_nr(call_idx)) == NULL) {
            survivors[survivors_num++] = v;
            /* add an outbound edge */
            topological_map[r].num_out++;
        }
    }

    for (i = 0; i < num_args; i++) {
        LiveRange *v = alc->values + arg_values[i];
        if (v->spill_pos != 0) {
            spilled_args[spilled_args_top++] = i;
        } else if (storage_refs[i]._cls == MVM_JIT_STORAGE_GPR) {
            MVMint8 reg_num = storage_refs[i]._pos;
            _DEBUG("Transfer Rq(%d) -> Rq(%d)", reg_num, v->reg_num);
            if (reg_num != v->reg_num) {
                topological_map[reg_num].in_reg = v->reg_num;
                topological_map[v->reg_num].num_out++;
                transfers_required++;
                if (register_map[reg_num] < 0) {
                    /* we can immediately queue a transfer, it's not used */
                    transfer_queue[transfer_queue_top++] = reg_num;
                }
            }
        } else if (storage_refs[i]._cls == MVM_JIT_STORAGE_STACK) {
            /* enqueue for stack transfer */
            stack_transfer[stack_transfer_top].reg_num = v->reg_num;
            stack_transfer[stack_transfer_top].stack_pos = storage_refs[i]._pos;
            stack_transfer_top++;
            transfers_required++;
            /* count the outbound edge */
            topological_map[v->reg_num].num_out++;
        } else {
            NYI(this_storage_class);
        }

        /* set bitmap */
        if (storage_refs[i]._cls == MVM_JIT_STORAGE_GPR) {
            MVMint8 reg_num = storage_refs[i]._pos;
            arg_bitmap |= (1 << reg_num);
        }
    }

#define INSERT_TILE(_tile, _pos, _order) MVM_jit_tile_list_insert(tc, list, _tile, _pos, _order)
#define INSERT_NEXT_TILE(_tile) INSERT_TILE(_tile, arglist_idx, ins_pos++)
#define MAKE_TILE(_code, _narg, _nval, ...) MVM_jit_tile_make(tc, alc->compiler, MVM_jit_compile_ ## _code, _narg, _nval, __VA_ARGS__)

#define INSERT_MOVE(_a, _b)          INSERT_NEXT_TILE(MAKE_TILE(move, 0, 2, _a, _b))
#define INSERT_COPY_TO_STACK(_s, _r) INSERT_NEXT_TILE(MAKE_TILE(store, 2, 2, MVM_JIT_STORAGE_STACK, _s, 0, _r))
#define INSERT_LOAD_LOCAL(_r, _l)    INSERT_NEXT_TILE(MAKE_TILE(load, 2, 1, MVM_JIT_STORAGE_LOCAL, _l, _r))
#define INSERT_LOCAL_STACK_COPY(_s, _l) \
    INSERT_NEXT_TILE(MAKE_TILE(memory_copy, 4, 1, MVM_JIT_STORAGE_STACK, _s, MVM_JIT_STORAGE_LOCAL, _l, 0, spare_register))

    /* Before any other thing, spill the surviving registers.
     * There are a number of correct strategies, e.g. a full spill, a split-and-spill or a restorative spill.
     * Full spill may be wasteful (although this could be optimized).
     * Split-and-spill, to do correctly, requires data flow analysis which I
     * don't have ready. (Where lies the 'split', in case you have multiple
     * basic blocks).
     * Restorative-spill (i.e. load directly after the CALL) may also be
     * wasteful, but at least it simple, and I predict it's useful for the
     * common case of a conditional branch.
     *
     * By the way, if you're wondering why LuaJIT2 is so amazingly fast, it's
     * also because it doesn't have to worry about such things, because 'jumping
     * out' of hot code is rare due to the tracing, and most primitives are
     * implemented in assembly.
     */
    for (i = 0; i < survivors_num; i++) {
        MVMint32 v    = survivors[i];
        MVMint8  src  = alc->values[v].reg_num;
        MVMint32 dst  = select_memory_for_spill(tc, alc, list, order_nr(call_idx), sizeof(MVMRegister));
        _DEBUG("Spilling Rq(%d) -> [rbx+%d]", src, dst);
        INSERT_NEXT_TILE(MAKE_TILE(store, 2, 2, MVM_JIT_STORAGE_LOCAL, dst, 0, src));
        /* directly after the CALL, restore the values */
        INSERT_TILE(MAKE_TILE(load, 2, 1, MVM_JIT_STORAGE_LOCAL, dst, src), call_idx, -2);
        /* decrease the outbound edges, and enqueue if possible */
        if (--(topological_map[src].num_out) == 0) {
            transfer_queue[transfer_queue_top++] = src;
        }
    }

    /* resolve conflicts for CALL; since we're spilling any surviving bits,
     * we can just move it to any free registers. */
    for (i = 1; i < call_tile->num_refs; i++) {
        MVMint8 spec = MVM_JIT_REGISTER_FETCH(call_tile->register_spec, i);
        if (MVM_JIT_REGISTER_IS_USED(spec)) {
            MVMint8 reg = call_tile->values[i];
            call_bitmap |= (1 << reg);
        }
    }

    while (call_bitmap & arg_bitmap) {
        MVMuint32 free_reg = ~(call_bitmap | arg_bitmap | NVR_GPR_BITMAP);
        /* FFS counts registers starting from 1 */
        MVMuint8 src = FFS(call_bitmap & arg_bitmap) - 1;
        MVMuint8 dst = FFS(free_reg) - 1;
        _ASSERT(free_reg != 0, "JIT: need to move a register but nothing is free");
        /* add edge */
        topological_map[dst].in_reg = src;
        topological_map[src].num_out++;
        /* enqueue directly (dst is free by definition) */
        transfer_queue[transfer_queue_top++] = dst;
        transfers_required++;

        /* update bitmap */
        call_bitmap = call_bitmap & ((~(1 << src)) | (1 << dst));

        /* update CALL args */
        for (i = 1; i < call_tile->num_refs; i++) {
            if (call_tile->args[i] == src) {
                call_tile->args[i] = dst;
            }
        }
    }

    /* with call_bitmap and arg_bitmap given, we can determine the spare
     * register used for allocation; NB this may only be necessary in some
     * cases */
    spare_register = FFS(~(call_bitmap | arg_bitmap | NVR_GPR_BITMAP)) - 1;
    _ASSERT(spare_register >= 0, "JIT: No spare register for moves");

    for (i = 0; i < stack_transfer_top; i++) {
        MVMint8 reg_num = stack_transfer[i].reg_num;
        MVMint8 stk_pos = stack_transfer[i].stack_pos;
        INSERT_COPY_TO_STACK(stk_pos, reg_num);
        _DEBUG("Insert stack parameter: Rq(%d) -> [rsp+%d]", reg_num, stk_pos);
        if (--(topological_map[reg_num].num_out) == 0) {
            transfer_queue[transfer_queue_top++] = reg_num;
        }
    }

    for (transfer_queue_idx = 0; transfer_queue_idx < transfer_queue_top; transfer_queue_idx++) {
        MVMint8 dst = transfer_queue[transfer_queue_idx];
        MVMint8 src = topological_map[dst].in_reg;
        if (src < 0) {
            _DEBUG("No inbound edge for Rq(%d)", dst);
            continue;
        }
        _DEBUG("Insert move (toposort): Rq(%d) -> Rq(%d)", src, dst);
        INSERT_MOVE(dst, src);
        if (--(topological_map[src].num_out) == 0) {
            transfer_queue[transfer_queue_top++] = src;
        }
    }

    if (transfer_queue_top < transfers_required) {
        /* rev_map points from a -> b, b -> c, c -> a, etc; which is the
         * direction of the moves. However, the direction of the cleanup
         * is necessarily reversed, first c -> a, then b -> c, then a -> c.
         * This suggest the use of a stack.
         */
        MVMint8 cycle_stack[16], cycle_stack_top = 0, c, n;
        for (i = 0; i < MVM_JIT_ARCH_NUM_GPR; i++) {
            if (topological_map[i].num_out > 0) {
                /* build a LIFO stack to reverse the cycle */
                for (c = i, n = topological_map[i].in_reg; n != i;
                     c = n, n = topological_map[n].in_reg) {
                    cycle_stack[cycle_stack_top++] = n;
                    _ASSERT(n >= 0, "no inbound edge");
                }
                _DEBUG("Insert move (cycle break): Rq(%d) -> Rq(%d)", spare_register, i);
                INSERT_MOVE(spare_register, i);
                topological_map[i].num_out--;
                /* pop stack and move insert transfers */
                while (cycle_stack_top--) {
                    c = cycle_stack[cycle_stack_top];
                    _DEBUG("Insert move (cycle break): Rq(%d) -> Rq(%d)", c, topological_map[c].in_reg);
                    INSERT_MOVE(topological_map[c].in_reg, c);
                    topological_map[c].num_out--;
                    _ASSERT(topological_map[c].num_out == 0, "num_out != 0 in breaking cycle");
                }
                _DEBUG("Insert move (cycle break): Rq(%d) -> Rq(%d)", topological_map[i].in_reg, spare_register);
                INSERT_MOVE(topological_map[i].in_reg, spare_register);
                _ASSERT(topological_map[i].num_out == 0, "Cycle is broken");
            }
        }
    }

    /* now all that remains is to deal with spilled values */
    for (i = 0; i < spilled_args_top; i++) {
        LiveRange *value = alc->values + arg_values[i];
        if (storage_refs[i]._cls == MVM_JIT_STORAGE_GPR) {
            INSERT_LOAD_LOCAL(storage_refs[i]._pos, value->spill_pos);
        } else if (storage_refs[i]._cls == MVM_JIT_STORAGE_STACK) {
            INSERT_LOCAL_STACK_COPY(storage_refs[i]._pos, value->spill_pos);
        } else {
            NYI(storage_class);
        }
    }
    /* Because we implement restorative loading, the values live after CALL are
     * live again, and the values last used for either ARGLIST or CALL will be
     * expired by other processes */

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
        prepare_arglist_and_call(tc, alc, list, arglist_idx, call_idx);
    } else if (tile->op == MVM_JIT_CALL || tile->op == MVM_JIT_CALLV) {
        /* Any CALL op requiremetns are handles by prepare_arglist_and_call,
         * which handles both tiles,  */
        _ASSERT(tile_cursor > 0 && list->items[tile_cursor - 1]->op == MVM_JIT_ARGLIST,
                "CALL must be preceeded by ARGLIST");
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

static void linear_scan(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMint32 i, tile_cursor = 0;
    MVM_VECTOR_INIT(alc->retired, alc->worklist_num);
    MVM_VECTOR_INIT(alc->spilled, 8);
    _DEBUG("STARTING LINEAR SCAN: %ld/%d", tc->instance->jit_seq_nr, list->tree->seq_nr);

    while (alc->worklist_num > 0) {
        MVMint32 v             = live_range_heap_pop(alc->values, alc->worklist, &alc->worklist_num);
        MVMint32 tile_order_nr = first_ref(alc->values + v);
        MVMint8 reg;
        _DEBUG("Processing live range %d (first ref %d, last ref %d)", v, first_ref(alc->values + v), last_ref(alc->values + v));

        /* deal with 'special' requirements */
        for (; order_nr(tile_cursor) <= tile_order_nr; tile_cursor++) {
            process_tile(tc, alc, list, tile_cursor);
        }

        /* assign registers in loop */
        active_set_expire(tc, alc, tile_order_nr);
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
                spill_any_register(tc, alc, list, tile_order_nr);
            }
            assign_register(tc, alc, list, v, MVM_JIT_STORAGE_GPR, reg);
            active_set_add(tc, alc, v);
        }
    }

    /* deal with final 'special tile' requirements */
    for (; tile_cursor < list->items_num; tile_cursor++) {
        process_tile(tc, alc, list, tile_cursor);
    }

    /* flush active live ranges */
    active_set_expire(tc, alc, list->items_num + 1);
    _DEBUG("END OF LINEAR SCAN%s","\n");
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
    MVM_free(alc.holes);
    MVM_free(alc.values);

    MVM_free(alc.worklist);
    MVM_free(alc.retired);
    MVM_free(alc.spilled);


    /* make edits effective */
    MVM_jit_tile_list_edit(tc, list);

}
