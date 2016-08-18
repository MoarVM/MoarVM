typedef struct {
    MVMint32 key;
    MVMint32 num_defs, num_uses;
    MVMint32 live_range_idx;
} UnionFind;

typedef struct {
    MVMint32 num_defs;
    MVMint32 *defs;
    MVMint32 num_uses;
    MVMint32 *uses;

    MVMint32 spilled_to; /* possibly-changing location of value in memory */
    /* unchanging location of value in register (otherwise we need
       more live ranges, or something...) */
    MVMJitStorageClass reg_cls;
    MVMint32 reg_num;
} LiveRange;

typedef struct {
    /* Sets of values */
    UnionFind *value_sets;
    /* single buffer for uses, definitions */
    MVMint32 *use_defs_buf;
    /* values produced ordered by first definition */
    MVM_VECTOR_DECL(LiveRange, values);

    /* values 'currently' live, an ordered stack.
     * we run three queries on this data structure:
     * - how many are in there, anyway?
     * - when is the first value due to expire (beyond its last use)
     * - what is the best value currently live to spill */
    MVMint32 live_set_top;
    LiveRange *live_set[MAX_LIVE];
    /* which live-set inhabits a constant register */
    MVMint32 prefered_register[NUM_GPR];

    /* Register handout ring */
    MVMint8 reg_buf[NUM_GPR];
    MVMint32 reg_give, reg_take;

    MVMint32 spill_top;


} RegisterAllocator;

UnionFind * value_set_find(UnionFind *sets, MVMint32 key) {
    while (sets[key].key != key) {
        key = sets[key].key;
    }
    return sets + key;
}

void value_set_union(UnionFind *sets, MVMint32 a, MVMint32 b) {
    if (sets[a].num_defs < sets[b].num_defs) {
        MVMint32 t = a; a = b; b = t;
    }
    sets[b].key = a; /* point b to a */
    sets[a].num_defs += sets[b].num_defs;
    sets[a].num_uses += sets[b].num_uses;
}

static void determine_live_ranges(MVMThreadContext *tc, MVMJitTileList *list, RegisterAllocator *alc) {
    MVMint32 i, j, k;
    MVMint32 num_use = 0, num_def = 0, num_live_range = 0;
    MVMint32 *use_buf, *def_buf;

    for (i = 0; i < list->items_num; i++) {
        MVMJitTile *tile = list->items[i];
        MVMint32 node = list->tree[tile->node];
        if (node == MVM_JIT_COPY || node == MVM_JIT_IF ||
            (node == MVM_JIT_DO && tile->template->vtype == MVM_JIT_REG)) { /* assuming template is defined, needs some better thoughts... */
            value_set_union(i, ref_node[i]); /* or something like that... IF requires two value_unions */
            num_live_range--; /* in case of IF only we actually destroy a live range */
        } else if (tile->vtype == MVM_JIT_REG) { /* yields value! needs to be shorter, really */
            /* define this value */
            alc->sets[node].num_defs = 1;
            alc->sets[node].num_uses = 0;
            alc->sets[node].key = node;
            alc->sets[node].live_range = -1;

            /* count totals so we can correctly allocate the buffers */
            num_def++;
            num_use += tile->num_values;
            num_live_range++;
        }
        /* TODO figure out where I store the tile nodes (or, if at all) */
        for (j = 0; j < tile->num_values; j++) {
            used_node = tile_nodes[j];
            /* account its use */
            value_set_find(alc->sets, used_tile)->num_uses++;
        }
    }
    /* initialize buffers */
    alc->values = MVM_malloc(sizeof(LiveRange)*num_live_range);
    alc->use_defs_buf = MVM_malloc(sizeof(MVMint32) * (num_defs + num_uses));

    /* split buf in two */
    use_buf = alc->use_defs_buf;
    def_buf = alc->use_defs_buf + num_use;

    /* live range allocation cursor */
    k = 0;
    /* second pass, translate the found sets and used nodes to live
     * ranges.  because we iterate in ascending order over tiles, uses
     * and defs are automatically ordered too. TODO: figure out a way
     * to represent register preferences! */
    for (i = 0; i < list->items_num; i++) {
        MVMJitTile * tile;
        /* TODO figure out a cleanish way to implement yields_value() */
        if (yields_value(tile)) {
            UnionFind *value_set = value_set_find(alc->sets, tile->node);
            LiveRange *value_range;
            if (value_set->live_range < 0) {
                /* first definition, allocate the live range for this block */
                value_set->live_range = k++;
                value_range = alc->values[value_set->live_range];
                value_range->defs = def_buf;
                value_range->uses = use_buf;
                /* bump pointers */
                def_buf += value_set->num_defs;
                use_buf += value_set->num_uses;
                /* set cursor/counts */
                value_range->num_defs = 0;
                value_range->num_uses = 0;
            } else {
                value_range = alc->values[value_set->live_range];
            }
            /* add definition */
            value_range->defs[value_range->num_defs++] = i;
        }
        /* Add uses */
        for (j = 0; j < tile->num_values; j++) {
            UnionFind *use_set = value_set_find(alc->sets, tile_nodes[j]);
            LiveRange *use_range = alc->values[use_set->live_range];
            use_range[use_range->num_uses++] = i;
        }
        /* We kind of do need to maintain a tile->live range mapping,
           if only to point to inserted tiles.... OR we handle that
           differently by making a SPILLS array, which may actually be
           more cleverer, and saves us from maintaining two more
           buffers (at the cost of having to read the tile nodes again
           and again) */
    }
}

static inline MVMint32 last_use(LiveRange *v) {
    return (v->uses[v->num_uses-1]);
}

static void live_set_add(MVMThreadContext *tc, RegisterAllocator *alc, LiveRange *value) {
    /* the original linear-scan heuristic for spilling is to take the
       * last value in the set to expire, freeeing up the largest
       * extent of code... that is a reasonably good heuristic, albeit
       * not essential to the concept of linear scan. It makes sense
       * to keep the stack ordered at all times (simplest by use of
       * insertion sort). Although insertion sort is O(n^2), n is
       * never large in this case (32 for RISC architectures, maybe,
       * if we ever support them; 7 for x86-64. So the time spent on
       * insertion sort is always small and bounded by a constant,
       * hence O(1). Yes, algorithmics works this way :-) */
    MVMint32 i;
    for (i = 0; i < alc->live_set_top; i++) {
        LiveRange *b = alc->live_set[i];
        if (last_use(b) > last_use(value)) {
            /* insert before b */
            memmove(alc->live_set + i + 1, alc->live_set + i, sizeof(LiveRange*)*(live_set_top - i));
            alc->live_set[i] = value;
            alc->live_set_top++;
            return;
        }
    }
    /* append at the end */
    alc->live_set[alc->live_set_top++] = value;
}

static void live_set_spill(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 position) {
    /* Use linear scan basic heuristic of last-used value... which is easy and good enough */
    LiveRange *to_spill = alc->values[--alc->values_top];
    /* TODO push on spill stack? decide if we want to split? */
}

static void live_set_expire(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 position) {

    MVMint32 i;
    for (i = 0; i < alc->live_set_top; i++) {
        if (last_use(alc->live_set[i]) < first_def(value)) {
            break;
        }
        /* TODO remove register preferences from the table */
    }

    /* shift off the first x values from the live set. */
    if (x > 0) {
        memmove(alc->live_set, alc->live_set + x, (alc->live_set_top -= x));
    }
}

static void live_range_split(MVMThreadContext *tc, RegisterAllocator *alc, LiveRange *to_spill, MVMint32 position, LiveRange *out) {
    MVMint32 i;
    for (i = 0; i < to_spill->num_defs; i++) {
        if (to_spill->defs[i] >= position) {
            /* cut in two pieces */
            out->defs = to_spill->defs + i;
            out->num_defs = to_spill->num_defs - i;
            to_spill->num_defs = i;
            break;
        }
    }
    for (i = 0; i < to_spill->num_uses; i++) {
        if (to_spill->uses[i] >= position) {
            out->uses = to_spill->uses + i;
            out->num_uses = to_spill->num_uses - i;
            to_spill->num_uses = i;
            break;
        }
    }
    for (i = 0; i < out->num_uses; i++) {
        /* TODO update use to the new live range..., but that means we
           need to maintain a mapping from tile values -> live range
           anyway... */
    }
}

static inline MVMint32 first_def(LiveRange *v) {
    return (v->defs[0]);
}

static void linear_scan(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMint32 i, j;
    for (i = 0; i < alc->values_num; i++) {
        LiveRange *value = alc->values[i];
        /* find old values we can expire */
        live_set_expire(tc, alc, first_def(value));
        /* add to live set */
        if (alc->values_top == MAX_LIVE) {
            live_set_spill(tc, alc, first_def(value));
        }
        live_set_add(tc, alc, value);
    }
}
