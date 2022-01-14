#include "moar.h"
#include "platform/io.h"

#ifndef MVM_HEAPSNAPSHOT_FORMAT
#define MVM_HEAPSNAPSHOT_FORMAT 3
#endif

#if MVM_HEAPSNAPSHOT_FORMAT == 3
#define MVM_HEAPSNAPSHOT_FORMAT_SUBVERSION 1
#endif

#define ZSTD_COMPRESSION_VALUE 9

/* In order to debug heap snapshot output, or to rapid-prototype new formats,
 * the DUMP_EVERYTHING_RAW define gets you a set of gzipped files into /tmp,
 * one for every property of the objects, and one set per heap snapshot.
 * You will have to make sure -lz lands in the linker options manually, tho */
#define DUMP_EVERYTHING_RAW 0

#if DUMP_EVERYTHING_RAW
#include "zlib.h"
#else
/* Just so we don't have to #if every single declaration all over the code */
typedef int (gzFile);
#endif

#if MVM_HEAPSNAPSHOT_FORMAT == 3
#include "zstd.h"
#endif

#ifndef MAX
    #define MAX(x, y) ((y) > (x) ? (y) : (x))
#endif
/* Check if we're currently taking heap snapshots. */
MVMint32 MVM_profile_heap_profiling(MVMThreadContext *tc) {
    return tc->instance->heap_snapshots != NULL;
}

static void filemeta_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col);
static void snapmeta_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col);

/* Start heap profiling. */
void MVM_profile_heap_start(MVMThreadContext *tc, MVMObject *config) {
    MVMHeapSnapshotCollection *col = MVM_calloc(1, sizeof(MVMHeapSnapshotCollection));
    char *path;
    MVMString *path_str;

    col->start_time = uv_hrtime();

    path_str = MVM_repr_get_str(tc,
        MVM_repr_at_key_o(tc, config, tc->instance->str_consts.path));

    if (MVM_is_null(tc, (MVMObject*)path_str)) {
        MVM_free(col);
        MVM_exception_throw_adhoc(tc, "Didn't specify a path for the heap snapshot profiler");
    }

    path = MVM_string_utf8_encode_C_string(tc, path_str);

    col->fh = MVM_platform_fopen(path, "w");

    if (!col->fh) {
        MVM_free(col);
        char *waste[2] = {path, NULL};
        MVM_exception_throw_adhoc_free(tc, waste, "Couldn't open heap snapshot target file %s: %s", path, strerror(errno));
    }
    MVM_free(path);

    fprintf(col->fh, "MoarHeapDumpv00%d", MVM_HEAPSNAPSHOT_FORMAT);

    {
#if MVM_HEAPSNAPSHOT_FORMAT == 2
        col->index = MVM_calloc(1, sizeof(MVMHeapDumpIndex));
        col->index->snapshot_sizes = MVM_calloc(1, sizeof(MVMHeapDumpIndexSnapshotEntry));
#endif
#if MVM_HEAPSNAPSHOT_FORMAT == 3
        MVMHeapDumpTableOfContents *toc = MVM_calloc(1, sizeof(MVMHeapDumpTableOfContents));
        col->toplevel_toc = toc;
        toc->toc_entry_alloc = 8;
        toc->toc_words = MVM_calloc(8, sizeof(char *));
        toc->toc_positions = (MVMuint64 *)MVM_calloc(8, sizeof(MVMuint64) * 2);

        filemeta_to_filehandle_ver3(tc, col);
#endif
    }

    tc->instance->heap_snapshots = col;
}

/* Grows storage if it's full, zeroing the extension. Assumes it's only being
 * grown for one more item. */
static void grow_storage(void *store_ptr, MVMuint64 *num, MVMuint64 *alloc, size_t size) {
    void **store = (void **)store_ptr;
    if (*num == *alloc) {
        *alloc = *alloc ? 2 * *alloc : 32;
        *store = MVM_recalloc(*store, *num * size, *alloc * size);
    }
}

/* Get a string heap index for the specified C string, adding it if needed. */
#define STR_MODE_OWN    0
#define STR_MODE_CONST  1
#define STR_MODE_DUP    2
static MVMuint64 get_string_index(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                                   char *str, char str_mode) {
     MVMuint64 i;

     /* Add a lookup hash here if it gets to be a hotspot. */
     MVMHeapSnapshotCollection *col = ss->col;
     for (i = 0; i < col->num_strings; i++) {
        if (strcmp(col->strings[i], str) == 0) {
            if (str_mode == STR_MODE_OWN)
                MVM_free(str);
            return i;
        }
    }

    grow_storage((void **)&(col->strings), &(col->num_strings),
        &(col->alloc_strings), sizeof(char *));
    grow_storage(&(col->strings_free), &(col->num_strings_free),
        &(col->alloc_strings_free), sizeof(char));
    col->strings_free[col->num_strings_free] = str_mode != STR_MODE_CONST;
    col->num_strings_free++;
    col->strings[col->num_strings] = str_mode == STR_MODE_DUP ? MVM_strdup(str) : str;
    return col->num_strings++;
}

/* Gets a string index in the string heap for a VM string. */
static MVMuint64 get_vm_string_index(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMString *str) {
    return str
        ? get_string_index(tc, ss, MVM_string_utf8_encode_C_string(tc, str), STR_MODE_OWN)
        : get_string_index(tc, ss, "<null>", STR_MODE_CONST);
}

/* Push a collectable to the list of work items, allocating space for it and
 * returning the collectable index. */
static MVMuint64 push_workitem(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                               MVMuint16 kind, void *target) {
    MVMHeapSnapshotWorkItem *wi;
    MVMuint64 col_idx;

    /* Mark space in collectables collection, and allocate an index. */
    grow_storage(&(ss->hs->collectables), &(ss->hs->num_collectables),
        &(ss->hs->alloc_collectables), sizeof(MVMHeapSnapshotCollectable));
    col_idx = ss->hs->num_collectables;
    ss->hs->num_collectables++;

    /* Add to the worklist. */
    grow_storage(&(ss->workitems), &(ss->num_workitems), &(ss->alloc_workitems),
        sizeof(MVMHeapSnapshotWorkItem));
    wi = &(ss->workitems[ss->num_workitems]);
    wi->kind = kind;
    wi->col_idx = col_idx;
    wi->target = target;
    ss->num_workitems++;

    return col_idx;
}

/* Pop a work item. */
static MVMHeapSnapshotWorkItem pop_workitem(MVMThreadContext *tc, MVMHeapSnapshotState *ss) {
    ss->num_workitems--;
    return ss->workitems[ss->num_workitems];
}

/* Sets the current reference "from" collectable. */
static void set_ref_from(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMuint64 col_idx) {
    /* The references should be contiguous, so if this collectable already
     * has any, something's wrong. */
    if (ss->hs->collectables[col_idx].num_refs)
        MVM_panic(1, "Heap snapshot corruption: can not add non-contiguous refs");

    ss->ref_from = col_idx;
    ss->hs->collectables[col_idx].refs_start = ss->hs->num_references;
}

/* Adds a reference. */
static void add_reference(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMuint16 ref_kind,
                          MVMuint64 index, MVMuint64 to) {
    /* Add to the references collection. */
    MVMHeapSnapshotReference *ref;
    MVMuint64 description = (index << MVM_SNAPSHOT_REF_KIND_BITS) | ref_kind;
    grow_storage(&(ss->hs->references), &(ss->hs->num_references),
        &(ss->hs->alloc_references), sizeof(MVMHeapSnapshotReference));
    ref = &(ss->hs->references[ss->hs->num_references]);
    ref->description = description;
    ref->collectable_index = to;
    ss->hs->num_references++;

    /* Increment collectable's number of references. */
    ss->hs->collectables[ss->ref_from].num_refs++;
}

/* Adds a reference with an integer description. */
static void add_reference_idx(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                               MVMuint64 idx,  MVMuint64 to) {
    add_reference(tc, ss, MVM_SNAPSHOT_REF_KIND_INDEX, idx, to);
}

/* Adds a reference with a C string description. */
static void add_reference_cstr(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                               char *cstr,  MVMuint64 to) {
    MVMuint64 str_idx = get_string_index(tc, ss, cstr, STR_MODE_OWN);
    add_reference(tc, ss, MVM_SNAPSHOT_REF_KIND_STRING, str_idx, to);
}

/* Adds a reference with a constant C string description. */
static void add_reference_const_cstr(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                                     const char *cstr,  MVMuint64 to) {
    MVMuint64 str_idx = get_string_index(tc, ss, (char *)cstr, STR_MODE_CONST);
    add_reference(tc, ss, MVM_SNAPSHOT_REF_KIND_STRING, str_idx, to);
}

static MVMuint64 get_const_string_index_cached(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                                               const char *cstr, MVMuint64 *cache, MVMuint64 str_mode) {
    MVMuint64 str_idx;
    if (cache && *cache < ss->col->num_strings) {
        if (strcmp(ss->col->strings[*cache], (char *)cstr) == 0) {
            /*fprintf(stderr, "hit! %d\n", *cache);*/
            assert(str_mode != STR_MODE_OWN);
            str_idx = *cache;
        }
        else {
            str_idx = get_string_index(tc, ss, (char *)cstr, str_mode);
            *cache = str_idx;
            /*fprintf(stderr, "miss! %d\n", str_idx);*/
        }
    }
    else {
        str_idx = get_string_index(tc, ss, (char *)cstr, str_mode);
        if (cache)
            *cache = str_idx;
    }
    return str_idx;
}

static void add_reference_const_cstr_cached(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                                     const char *cstr,  MVMuint64 to, MVMuint64 *cache) {
    MVMuint64 str_idx = get_const_string_index_cached(tc, ss, cstr, cache, STR_MODE_CONST);
    add_reference(tc, ss, MVM_SNAPSHOT_REF_KIND_STRING, str_idx, to);
}

/* Adds a reference with a VM string description. */
static void add_reference_vm_str(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                               MVMString *str,  MVMuint64 to) {
   MVMuint64 str_idx = get_vm_string_index(tc, ss, str);
   add_reference(tc, ss, MVM_SNAPSHOT_REF_KIND_STRING, str_idx, to);
}

/* Gets the index of a collectable, either returning an existing index if we've
 * seen it before or adding it if not. */
static MVMuint64 get_collectable_idx(MVMThreadContext *tc,
        MVMHeapSnapshotState *ss, MVMCollectable *collectable) {
    struct MVMPtrHashEntry *entry = MVM_ptr_hash_lvalue_fetch(tc, &ss->seen, collectable);

    if (entry->key) {
        return entry->value;
    }
    entry->key = collectable;

    MVMuint64 idx;
    if (collectable->flags1 & MVM_CF_STABLE) {
        idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_STABLE, collectable);
        ss->col->total_stables++;
    }
    else if (collectable->flags1 & MVM_CF_TYPE_OBJECT) {
        idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_TYPE_OBJECT, collectable);
        ss->col->total_typeobjects++;
    }
    else if (collectable->flags1 & MVM_CF_FRAME) {
        idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_FRAME, collectable);
        ss->col->total_frames++;
    }
    else {
        idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_OBJECT, collectable);
        ss->col->total_objects++;
    }

    entry->value = idx;
    return idx;
}

/* Gets the index of a frame, either returning an existing index if we've seen
 * it before or adding it if not. */
static MVMuint64 get_frame_idx(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
        MVMFrame *frame) {
    struct MVMPtrHashEntry *entry = MVM_ptr_hash_lvalue_fetch(tc, &ss->seen, frame);

    if (entry->key) {
        return entry->value;
    }
    entry->key = frame;

    MVMuint64 idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_FRAME, frame);
    ss->col->total_frames++;
    entry->value = idx;
    return idx;
}

/* Adds a type table index reference to the collectable snapshot entry, either
 * using an existing type table entry or adding a new one. */
static void set_type_index(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
       MVMHeapSnapshotCollectable *col, MVMSTable *st) {
    MVMuint64 repr_idx = get_const_string_index_cached(tc, ss, (char *)st->REPR->name, &ss->repr_str_idx_cache[st->REPR->ID], STR_MODE_CONST);
    char *debug_name = MVM_6model_get_stable_debug_name(tc, st);
    MVMuint64 type_idx;

    MVMuint64 i;
    MVMHeapSnapshotType *t;

    if (strlen(debug_name) != 0) {
        type_idx = get_const_string_index_cached(tc, ss, MVM_6model_get_stable_debug_name(tc, st), &ss->type_str_idx_cache[st->REPR->ID], STR_MODE_DUP);
    }
    else {
        char anon_with_repr[256] = {0};
        snprintf(anon_with_repr, 250, "<anon %s>", st->REPR->name);
        type_idx = get_string_index(tc, ss, anon_with_repr, STR_MODE_DUP);
    }

    for (i = 0; i < 8; i++) {
        if (type_idx == ss->type_of_type_idx_cache[i] && repr_idx == ss->repr_of_type_idx_cache[i]) {
            MVMuint64 result = ss->type_idx_cache[i];
            if (result < ss->col->num_types && ss->col->types[result].repr_name == repr_idx && ss->col->types[result].type_name == type_idx) {
                col->type_or_frame_index = ss->type_idx_cache[i];
                return;
            }
        }
    }

    for (i = 0; i < ss->col->num_types; i++) {
        t = &(ss->col->types[i]);
        if (t->repr_name == repr_idx && t->type_name == type_idx) {
            col->type_or_frame_index = i;
            ss->type_of_type_idx_cache[ss->type_idx_rotating_insert_slot] = type_idx;
            ss->repr_of_type_idx_cache[ss->type_idx_rotating_insert_slot] = repr_idx;
            ss->type_idx_cache[ss->type_idx_rotating_insert_slot] = i;
            ss->type_idx_rotating_insert_slot++;
            if (ss->type_idx_rotating_insert_slot == 8)
                ss->type_idx_rotating_insert_slot = 0;
            return;
        }
    }

    grow_storage(&(ss->col->types), &(ss->col->num_types),
        &(ss->col->alloc_types), sizeof(MVMHeapSnapshotType));
    t = &(ss->col->types[ss->col->num_types]);
    t->repr_name = repr_idx;
    t->type_name = type_idx;
    col->type_or_frame_index = ss->col->num_types;
    ss->col->num_types++;
}

/* Adds a static frame table index reference to the collectable snapshot entry,
 * either using an existing table entry or adding a new one. */
static void set_static_frame_index(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
       MVMHeapSnapshotCollectable *col, MVMStaticFrame *sf) {
    MVMuint64 name_idx = get_vm_string_index(tc, ss, sf->body.name);
    MVMuint64 cuid_idx = get_vm_string_index(tc, ss, sf->body.cuuid);

    MVMCompUnit *cu = sf->body.cu;
    MVMBytecodeAnnotation *ann = MVM_bytecode_resolve_annotation(tc, &(sf->body), 0);
    MVMuint64 line = ann ? ann->line_number : 1;
    MVMString *file_name = ann && ann->filename_string_heap_index < cu->body.num_strings
        ? MVM_cu_string(tc, cu, ann->filename_string_heap_index)
        : cu->body.filename;

    /* We're running as part of gc_finalize. By now, live objects have been marked
       but not yet collected. If the file name was just deserialized from the string
       heap by MVM_cu_string, it will have been allocated in gen2 directly, but not
       marked as live for this gc run. Do it here to prevent it from getting freed
       after taking this heap snapshot, while it's actually still referenced from the
       comp unit's strings list. But only do so if we're actually collecting gen2
       in this run. Otherwise the flag may still be set during global destruction. */
    if (
        tc->instance->gc_full_collect
        && file_name
        && file_name->common.header.flags2 & MVM_CF_SECOND_GEN
    )
        file_name->common.header.flags2 |= MVM_CF_GEN2_LIVE;

    MVMuint64 file_idx = get_vm_string_index(tc, ss, file_name);

    MVMuint64 i;
    MVMHeapSnapshotStaticFrame *s;
    for (i = 0; i < ss->col->num_static_frames; i++) {
        s = &(ss->col->static_frames[i]);
        if (s->name == name_idx && s->cuid == cuid_idx && s->line == line && s->file == file_idx) {
            col->type_or_frame_index = i;
            MVM_free(ann);
            return;
        }
    }

    MVM_free(ann);

    grow_storage(&(ss->col->static_frames), &(ss->col->num_static_frames),
        &(ss->col->alloc_static_frames), sizeof(MVMHeapSnapshotStaticFrame));
    s = &(ss->col->static_frames[ss->col->num_static_frames]);
    s->name = name_idx;
    s->cuid = cuid_idx;
    s->line = line;
    s->file = file_idx;
    col->type_or_frame_index = ss->col->num_static_frames;
    ss->col->num_static_frames++;
}

/* Processes the work items, until we've none left. */
static void process_collectable(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
        MVMHeapSnapshotCollectable *col, MVMCollectable *c, MVMuint64 *sc_cache) {
    MVMuint32 sc_idx = MVM_sc_get_idx_of_sc(c);
    if (sc_idx > 0)
        add_reference_const_cstr_cached(tc, ss, "<SC>",
            get_collectable_idx(tc, ss,
                (MVMCollectable *)tc->instance->all_scs[sc_idx]->sc), sc_cache);
    col->collectable_size = c->size;
}
static void process_gc_worklist(MVMThreadContext *tc, MVMHeapSnapshotState *ss, char *desc) {
    MVMCollectable **c_ptr;
    MVMuint16 ref_kind = desc
        ? MVM_SNAPSHOT_REF_KIND_STRING
        : MVM_SNAPSHOT_REF_KIND_UNKNOWN;
    MVMuint16 ref_index = desc
        ? get_string_index(tc, ss, desc, STR_MODE_CONST)
        : 0;
    while (( c_ptr = MVM_gc_worklist_get(tc, ss->gcwl) )) {
        MVMCollectable *c = *c_ptr;
        if (c)
            add_reference(tc, ss, ref_kind, ref_index,
                get_collectable_idx(tc, ss, c));
    }
}
static void incorporate_type_stats(MVMThreadContext *tc, MVMHeapSnapshotStats *stats, MVMHeapSnapshotCollectable *col) {
    if (col->type_or_frame_index >= stats->type_stats_alloc) {
        MVMuint32 prev_alloc = stats->type_stats_alloc;
        while (col->type_or_frame_index >= stats->type_stats_alloc) {
            stats->type_stats_alloc += 512;
        }
        stats->type_counts   = MVM_recalloc(stats->type_counts,
                sizeof(MVMuint32) * prev_alloc,
                sizeof(MVMuint32) * stats->type_stats_alloc);
        stats->type_size_sum = MVM_recalloc(stats->type_size_sum,
                sizeof(MVMuint64) * prev_alloc,
                sizeof(MVMuint64) * stats->type_stats_alloc);
    }
    stats->type_counts[col->type_or_frame_index]++;
    stats->type_size_sum[col->type_or_frame_index] += col->collectable_size + col->unmanaged_size;
}
static void incorporate_sf_stats(MVMThreadContext *tc, MVMHeapSnapshotStats *stats, MVMHeapSnapshotCollectable *col) {
    if (col->type_or_frame_index >= stats->sf_stats_alloc) {
        MVMuint32 prev_alloc = stats->sf_stats_alloc;
        while (col->type_or_frame_index >= stats->sf_stats_alloc) {
            stats->sf_stats_alloc += 512;
        }
        stats->sf_counts   = MVM_recalloc(stats->sf_counts,
                sizeof(MVMuint32) * prev_alloc,
                sizeof(MVMuint32) * stats->sf_stats_alloc);
        stats->sf_size_sum = MVM_recalloc(stats->sf_size_sum,
                sizeof(MVMuint64) * prev_alloc,
                sizeof(MVMuint64) * stats->sf_stats_alloc);
    }
    stats->sf_counts[col->type_or_frame_index]++;
    stats->sf_size_sum[col->type_or_frame_index] += col->collectable_size + col->unmanaged_size;
}
static void process_object(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
        MVMHeapSnapshotCollectable *col, MVMObject *obj, MVMuint64 *stable_cache, MVMuint64 *sc_cache) {
    process_collectable(tc, ss, col, (MVMCollectable *)obj, sc_cache);
    set_type_index(tc, ss, col, obj->st);
    add_reference_const_cstr_cached(tc, ss, "<STable>",
        get_collectable_idx(tc, ss, (MVMCollectable *)obj->st), stable_cache);
    if (IS_CONCRETE(obj)) {
        /* Use object's gc_mark function to find what it references. */
        /* XXX We'll also add an API for getting better information, e.g.
         * attribute names. */
        if (REPR(obj)->describe_refs)
            REPR(obj)->describe_refs(tc, ss, STABLE(obj), OBJECT_BODY(obj));
        else if (REPR(obj)->gc_mark) {
            REPR(obj)->gc_mark(tc, STABLE(obj), OBJECT_BODY(obj), ss->gcwl);
            process_gc_worklist(tc, ss, NULL);
        }
        if (REPR(obj)->unmanaged_size)
            col->unmanaged_size += REPR(obj)->unmanaged_size(tc, STABLE(obj),
                OBJECT_BODY(obj));
        if (ss->hs->stats) {
            incorporate_type_stats(tc, ss->hs->stats, col);
        }
    }
}
static void process_workitems(MVMThreadContext *tc, MVMHeapSnapshotState *ss) {
    MVMuint64 stable_cache = 0;
    MVMuint64 sc_cache = 0;

    MVMuint64 cache_1 = 0;
    MVMuint64 cache_2 = 0;
    MVMuint64 cache_3 = 0;

    while (ss->num_workitems > 0) {
        MVMHeapSnapshotWorkItem item = pop_workitem(tc, ss);

        /* We take our own working copy of the collectable info, since the
         * collectables array can grow and be reallocated. */
        MVMHeapSnapshotCollectable col;
        set_ref_from(tc, ss, item.col_idx);
        col = ss->hs->collectables[item.col_idx];
        col.kind = item.kind;

        switch (item.kind) {
            case MVM_SNAPSHOT_COL_KIND_OBJECT:
            case MVM_SNAPSHOT_COL_KIND_TYPE_OBJECT:
                process_object(tc, ss, &col, (MVMObject *)item.target, &stable_cache, &sc_cache);
                break;
            case MVM_SNAPSHOT_COL_KIND_STABLE: {
                MVMuint16 i;
                MVMSTable *st = (MVMSTable *)item.target;
                process_collectable(tc, ss, &col, (MVMCollectable *)st, &sc_cache);
                set_type_index(tc, ss, &col, st);

                for (i = 0; i < st->type_check_cache_length; i++)
                    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                        (MVMCollectable *)st->type_check_cache[i], "Type cache entry", &cache_1);

                if (st->container_spec && st->container_spec->gc_mark_data) {
                    st->container_spec->gc_mark_data(tc, st, ss->gcwl);
                    process_gc_worklist(tc, ss, "Container spec data item");
                }

                if (st->boolification_spec)
                    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                        (MVMCollectable *)st->boolification_spec->method,
                        "Boolification method", &cache_2);

                MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                    (MVMCollectable *)st->WHO, "WHO", &cache_3);
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)st->WHAT, "WHAT");
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)st->HOW, "HOW");
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)st->HOW_sc, "HOW serialization context");

                if (st->mode_flags & MVM_PARAMETRIC_TYPE) {
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->paramet.ric.parameterizer,
                        "Parametric type parameterizer");
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->paramet.ric.lookup,
                        "Parametric type intern table");
                }
                else if (st->mode_flags & MVM_PARAMETERIZED_TYPE) {
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->paramet.erized.parametric_type,
                        "Parameterized type's parametric type");
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->paramet.erized.parameters,
                        "Parameterized type's parameters");
                }

                if (st->REPR->gc_mark_repr_data) {
                    st->REPR->gc_mark_repr_data(tc, st, ss->gcwl);
                    process_gc_worklist(tc, ss, "REPR data item");
                }
                break;
            }
            case MVM_SNAPSHOT_COL_KIND_FRAME: {
                MVMFrame *frame = (MVMFrame *)item.target;
                col.collectable_size = sizeof(MVMFrame);
                set_static_frame_index(tc, ss, &col, frame->static_info);

                if (frame->caller && !MVM_FRAME_IS_ON_CALLSTACK(tc, frame))
                    add_reference_const_cstr(tc, ss, "Caller",
                        get_frame_idx(tc, ss, frame->caller));
                if (frame->outer)
                    add_reference_const_cstr(tc, ss, "Outer",
                        get_frame_idx(tc, ss, frame->outer));

                MVM_gc_root_add_frame_registers_to_worklist(tc, ss->gcwl, frame);
                process_gc_worklist(tc, ss, "Register");
                if (frame->work)
                    col.unmanaged_size += frame->allocd_work;

                if (frame->env) {
                    MVMuint16  i, count;
                    MVMuint16 *type_map;
                    MVMuint16  name_count = frame->static_info->body.num_lexicals;
                    MVMString **names = frame->static_info->body.lexical_names_list;
                    if (frame->spesh_cand && frame->spesh_cand->body.lexical_types) {
                        type_map = frame->spesh_cand->body.lexical_types;
                        count    = frame->spesh_cand->body.num_lexicals;
                    }
                    else {
                        type_map = frame->static_info->body.lexical_types;
                        count    = frame->static_info->body.num_lexicals;
                    }
                    for (i = 0; i < count; i++) {
                        if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj) {
                            if (i < name_count)
                                MVM_profile_heap_add_collectable_rel_vm_str(tc, ss,
                                    (MVMCollectable *)frame->env[i].o, names[i]);
                            else
                                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                                    (MVMCollectable *)frame->env[i].o, "Lexical (inlined)");
                        }
                    }
                    col.unmanaged_size += frame->allocd_env;
                }

                if (ss->hs->stats) {
                    incorporate_sf_stats(tc, ss->hs->stats, &col);
                }

                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)frame->code_ref, "Code reference");
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)frame->static_info, "Static frame");

                if (frame->extra) {
                    MVMFrameExtra *e = frame->extra;
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)e->dynlex_cache_name,
                        "Dynamic lexical cache name");
                }

                break;
            }
            case MVM_SNAPSHOT_COL_KIND_PERM_ROOTS:
                MVM_gc_root_add_permanents_to_worklist(tc, NULL, ss);
                break;
            case MVM_SNAPSHOT_COL_KIND_INSTANCE_ROOTS:
                MVM_gc_root_add_instance_roots_to_worklist(tc, NULL, ss);
                break;
            case MVM_SNAPSHOT_COL_KIND_CSTACK_ROOTS:
                MVM_gc_root_add_temps_to_worklist((MVMThreadContext *)item.target, NULL, ss);
                break;
            case MVM_SNAPSHOT_COL_KIND_THREAD_ROOTS: {
                MVMThreadContext *thread_tc = (MVMThreadContext *)item.target;
                MVM_gc_root_add_tc_roots_to_worklist(thread_tc, NULL, ss);
                if (thread_tc->cur_frame &&
                        !MVM_FRAME_IS_ON_CALLSTACK(thread_tc, thread_tc->cur_frame))
                    add_reference_const_cstr(tc, ss, "Current frame",
                        get_frame_idx(tc, ss, thread_tc->cur_frame));
                 break;
            }
            case MVM_SNAPSHOT_COL_KIND_ROOT: {
                MVMThread *cur_thread;

                add_reference_const_cstr(tc, ss, "Permanent Roots",
                    push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_PERM_ROOTS, NULL));
                add_reference_const_cstr(tc, ss, "VM Instance Roots",
                    push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_INSTANCE_ROOTS, NULL));

                cur_thread = tc->instance->threads;
                while (cur_thread) {
                    if (cur_thread->body.tc) {
                        add_reference_const_cstr(tc, ss, "C Stack Roots",
                            push_workitem(tc, ss,
                                MVM_SNAPSHOT_COL_KIND_CSTACK_ROOTS,
                                cur_thread->body.tc));
                        add_reference_const_cstr(tc, ss, "Thread Roots",
                            push_workitem(tc, ss,
                                MVM_SNAPSHOT_COL_KIND_THREAD_ROOTS,
                                cur_thread->body.tc));
                        add_reference_const_cstr(tc, ss, "Inter-generational Roots",
                            push_workitem(tc, ss,
                                MVM_SNAPSHOT_COL_KIND_INTERGEN_ROOTS,
                                cur_thread->body.tc));
                        add_reference_const_cstr(tc, ss, "Thread Call Stack Roots",
                            push_workitem(tc, ss,
                                MVM_SNAPSHOT_COL_KIND_CALLSTACK_ROOTS,
                                cur_thread->body.tc));
                    }
                    cur_thread = cur_thread->body.next;
                }

                break;
            }
            case MVM_SNAPSHOT_COL_KIND_INTERGEN_ROOTS: {
                MVMThreadContext *thread_tc = (MVMThreadContext *)item.target;
                MVM_gc_root_add_gen2s_to_snapshot(thread_tc, ss);
                break;
            }
            case MVM_SNAPSHOT_COL_KIND_CALLSTACK_ROOTS: {
                MVMThreadContext *thread_tc = (MVMThreadContext *)item.target;
                if (thread_tc->cur_frame && MVM_FRAME_IS_ON_CALLSTACK(tc, thread_tc->cur_frame)) {
                    MVMFrame *cur_frame = thread_tc->cur_frame;
                    MVMint32 idx = 0;
                    while (cur_frame && MVM_FRAME_IS_ON_CALLSTACK(tc, cur_frame)) {
                        add_reference_idx(tc, ss, idx,
                            push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_FRAME,
                                (MVMCollectable *)cur_frame));
                        idx++;
                        cur_frame = cur_frame->caller;
                    }
                }
                break;
            }
            default:
                MVM_panic(1, "Unknown heap snapshot worklist item kind %d", item.kind);
        }

        ss->col->total_heap_size += col.collectable_size + col.unmanaged_size;

        /* Store updated collectable info into array. Note that num_refs was
         * updated "at a distance". */
        col.num_refs = ss->hs->collectables[item.col_idx].num_refs;
        ss->hs->collectables[item.col_idx] = col;
    }
}

/* API function for adding a collectable to the snapshot, describing its
 * relation to the current collectable with a constant C string that we
 * should not free. */
void MVM_profile_heap_add_collectable_rel_const_cstr(MVMThreadContext *tc,
        MVMHeapSnapshotState *ss, MVMCollectable *collectable, const char *desc) {
    if (collectable)
        add_reference_const_cstr(tc, ss, desc,
            get_collectable_idx(tc, ss, collectable));
}
void MVM_profile_heap_add_collectable_rel_const_cstr_cached(MVMThreadContext *tc,
        MVMHeapSnapshotState *ss, MVMCollectable *collectable, const char *desc, MVMuint64 *cache) {
    if (collectable)
        add_reference_const_cstr_cached(tc, ss, desc,
            get_collectable_idx(tc, ss, collectable), cache);
}

/* API function for adding a collectable to the snapshot, describing its
 * relation to the current collectable with an MVMString. */
void MVM_profile_heap_add_collectable_rel_vm_str(MVMThreadContext *tc,
        MVMHeapSnapshotState *ss, MVMCollectable *collectable, MVMString *desc) {
    if (collectable)
        add_reference_vm_str(tc, ss, desc,
            get_collectable_idx(tc, ss, collectable));
}

/* API function for adding a collectable to the snapshot, describing its
 * relation to the current collectable with an integer index. */
void MVM_profile_heap_add_collectable_rel_idx(MVMThreadContext *tc,
        MVMHeapSnapshotState *ss, MVMCollectable *collectable, MVMuint64 idx) {
    if (collectable)
        add_reference_idx(tc, ss, idx,
            get_collectable_idx(tc, ss, collectable));
}

/* Drives the overall process of recording a snapshot of the heap. */
static void record_snapshot(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapSnapshot *hs) {
    /* Initialize state for taking a snapshot. */
    MVMHeapSnapshotState ss;
    memset(&ss, 0, sizeof(MVMHeapSnapshotState));
    ss.col = col;
    ss.hs = hs;
    ss.gcwl = MVM_gc_worklist_create(tc, 1);

    /* We push the ultimate "root of roots" onto the worklist to get things
     * going, then set off on our merry way. */
    push_workitem(tc, &ss, MVM_SNAPSHOT_COL_KIND_ROOT, NULL);
    process_workitems(tc, &ss);

    /* Clean up temporary state. */
    MVM_free(ss.workitems);
    MVM_ptr_hash_demolish(tc, &ss.seen);
    MVM_gc_worklist_destroy(tc, ss.gcwl);
}

static void destroy_current_heap_snapshot(MVMThreadContext *tc) {
    MVMHeapSnapshotCollection *col = tc->instance->heap_snapshots;

    MVM_free(col->snapshot->stats->type_counts);
    MVM_free(col->snapshot->stats->type_size_sum);
    MVM_free(col->snapshot->stats->sf_counts);
    MVM_free(col->snapshot->stats->sf_size_sum);
    MVM_free(col->snapshot->stats);
    MVM_free(col->snapshot->collectables);
    MVM_free(col->snapshot->references);
    MVM_free_null(col->snapshot);
}

static void destroy_toc(MVMHeapDumpTableOfContents *toc) {
    MVM_free(toc->toc_words);
    MVM_free(toc->toc_positions);
    MVM_free(toc);
}

/* Frees all memory associated with the heap snapshot. */
static void destroy_heap_snapshot_collection(MVMThreadContext *tc) {
    MVMHeapSnapshotCollection *col = tc->instance->heap_snapshots;
    MVMuint64 i;

    for (i = 0; i < col->num_strings; i++)
        if (col->strings_free[i])
            MVM_free(col->strings[i]);
    MVM_free(col->strings);
    MVM_free(col->strings_free);

    MVM_free(col->types);
    MVM_free(col->static_frames);

#if MVM_HEAPSNAPSHOT_FORMAT == 3
    destroy_toc(col->toplevel_toc);
    if (col->second_level_toc)
        destroy_toc(col->second_level_toc);
#elif MVM_HEAPSNAPSHOT_FORMAT == 2
    MVM_free(col->index->snapshot_sizes);
    MVM_free(col->index);
#endif

    MVM_free(col);
    tc->instance->heap_snapshots = NULL;
}

#if DUMP_EVERYTHING_RAW
static gzFile sfopen(const char *format, ...) {
    gzFile result;
    char filename[1024] = {0};
    va_list args;
    va_start(args, format);
    vsnprintf(&filename, 1022, format, args);
    va_end(args);
    result = gzopen(filename, "w8");
    return result;
}

static gzFile open_coll_file(MVMHeapSnapshotCollection *col, char *typename) {
    return sfopen("/tmp/heapdump_%s_%d.txt.gz", typename, col->snapshot_idx);
}
#endif

#if MVM_HEAPSNAPSHOT_FORMAT == 3
MVMuint32 get_new_toc_entry(MVMThreadContext *tc, MVMHeapDumpTableOfContents *toc) {
    MVMuint32 i = toc->toc_entry_used++;

    if (toc->toc_entry_used >= toc->toc_entry_alloc) {
        toc->toc_entry_alloc += 8;
        toc->toc_words = MVM_realloc(toc->toc_words, toc->toc_entry_alloc * sizeof(char *));
        toc->toc_positions = (MVMuint64 *)MVM_realloc(toc->toc_positions, toc->toc_entry_alloc * sizeof(MVMuint64) * 2);
    }

    return i;
}

static void serialize_attribute_stream(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, char *name, char *start, size_t offset, size_t elem_size, size_t count, FILE* fh) {
    char *pointer = (char *)start;

    ZSTD_CStream *cstream;

    size_t outSize = ZSTD_CStreamOutSize();

    ZSTD_outBuffer outbuf;

    size_t size_position = ftell(fh);
    size_t end_position;

    size_t return_value;

    MVMuint16 elem_size_to_write = elem_size;

    size_t written_total = 0;
    /*size_t original_total = count * elem_size;*/

    char *out_buffer = MVM_malloc(outSize);
    outbuf.dst = out_buffer;
    outbuf.pos = 0;
    outbuf.size = outSize;

    cstream = ZSTD_createCStream();
    if (ZSTD_isError(return_value = ZSTD_initCStream(cstream, ZSTD_COMPRESSION_VALUE))) {
        MVM_panic(1, "ZSTD compression error in heap snapshot: %s", ZSTD_getErrorName(return_value));
    }

    {
        char namebuf[8];
        /* Yes, this is a lot of boiler plate to silence a bogus gcc warning (but not clang) :(
         * Unfortunately, this is a real edge case. Using memcpy would lead to
         * a buffer overflow if name is shorter than 8 bytes. The gcc compiler
         * warning aside, strncpy seems like exactly the right tool for the job
         * as we want at most 8 bytes and don't care for any trailing \0, but
         * are OK with zero padding at the end. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif
        strncpy(namebuf, name, 8);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        fwrite(namebuf, 8, 1, fh);
    }

    /* Write the size per entry to the file */
    fwrite(&elem_size_to_write, sizeof(MVMuint16), 1, fh);

    /* Write a few bytes of space to store the size later - maybe. */
    {
        MVMuint64 size = 0;
        fwrite(&size, sizeof(MVMuint64), 1, fh);
    }


    while (count > 0) {
        size_t written;
        ZSTD_inBuffer inbuf;

        inbuf.src = pointer;
        inbuf.pos = 0;
        inbuf.size = elem_size;


        if (ZSTD_isError(return_value = ZSTD_compressStream(cstream, &outbuf, &inbuf))) {
            MVM_panic(1, "ZSTD compression error in heap snapshot: %s", ZSTD_getErrorName(return_value));
        }

        if (outbuf.pos) {
            written = fwrite(outbuf.dst, sizeof(char), outbuf.pos, fh);
            written_total += written;
            outbuf.pos = 0;
        }

        pointer += offset;
        count--;
    }

    do {
        size_t written;

        if (ZSTD_isError(return_value = ZSTD_endStream(cstream, &outbuf))) {
            MVM_panic(1, "ZSTD compression error in heap snapshot: %s", ZSTD_getErrorName(return_value));
        }

        if (outbuf.pos) {
            written = fwrite(outbuf.dst, sizeof(char), outbuf.pos, fh);
            written_total += written;
            outbuf.pos = 0;
        }
    } while (return_value != 0 && !ZSTD_isError(return_value));

    /*fprintf(stderr, "for an attribute %s, wrote %lld kibytes (compressed from %lld kibytes, %f%%)\n", name, written_total / 1024,*/
            /*original_total / 1024, written_total * 100.0f / original_total);*/

    end_position = ftell(fh);

    /* Here would be the right place to seek backwards and write the total size. */

    if (col->second_level_toc) {
        MVMuint32 toc_i = get_new_toc_entry(tc, col->second_level_toc);
        col->second_level_toc->toc_words[toc_i] = name;
        col->second_level_toc->toc_positions[toc_i * 2]     = size_position;
        col->second_level_toc->toc_positions[toc_i * 2 + 1] = end_position;
    }

    ZSTD_freeCStream(cstream);
    MVM_free(out_buffer);
}


void string_heap_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    FILE *fh = col->fh;
    MVMuint64 i = col->strings_written;

    size_t return_value = 0;
    size_t result_buffer_size = 2048;
    char *result_buffer = MVM_malloc(2048);
    char *result_buffer_insert_pos = result_buffer;

    ZSTD_CStream *cstream;

    ZSTD_inBuffer inbuf;
    ZSTD_outBuffer outbuf;

    char typename[8] = "strings\0";
    MVMuint64 size = 0;

    MVMuint64 size_position = 0;
    MVMuint64 end_position = 0;

    while (i < col->num_strings) {
        char *str = col->strings[i];
        MVMuint32 output_size = strlen(str);

        while (result_buffer_insert_pos + output_size + 4 >= result_buffer + result_buffer_size) {
            size_t offset = result_buffer_insert_pos - result_buffer;
            result_buffer_size += 2048;
            result_buffer = MVM_realloc(result_buffer, result_buffer_size);
            result_buffer_insert_pos = result_buffer + offset;
        }

        memcpy(result_buffer_insert_pos, &output_size, sizeof(MVMuint32));
        result_buffer_insert_pos += sizeof(MVMuint32);

        memcpy(result_buffer_insert_pos, str, output_size);
        result_buffer_insert_pos += output_size;

        i++;
    }

    if (result_buffer_insert_pos == result_buffer) {
        MVM_free(result_buffer_insert_pos);
        return;
    }

    size_position = ftell(fh);
    fwrite(&typename, sizeof(char), 8, fh);
    /* If the stream is seekable, we could go back here to write the size,
     * but only if we don't write everything in one go anyway */

    cstream = ZSTD_createCStream();

    if (ZSTD_isError(return_value = ZSTD_initCStream(cstream, ZSTD_COMPRESSION_VALUE))) {
        MVM_free(result_buffer);
        MVM_panic(1, "ZSTD compression error in heap snapshot: %s", ZSTD_getErrorName(return_value));
    }

    inbuf.src = result_buffer;
    inbuf.pos = 0;
    inbuf.size = result_buffer_insert_pos - result_buffer;

    /* TODO write the right size here later */
    fwrite(&size, sizeof(MVMuint64), 1, fh);

    outbuf.dst = MVM_malloc(1024 * 10);

    while (inbuf.pos < inbuf.size) {
        size_t written = 0;

        outbuf.pos = 0;
        outbuf.size = 1024 * 10;

        return_value = ZSTD_compressStream(cstream, &outbuf, &inbuf);

        if (ZSTD_isError(return_value)) {
            MVM_free(outbuf.dst);
            MVM_free(result_buffer);
            MVM_panic(1, "Error compressing heap snapshot data: %s", ZSTD_getErrorName(return_value));
        }

        inbuf.src = (char *)inbuf.src + inbuf.pos;
        inbuf.size -= inbuf.pos;
        inbuf.pos = 0;

        while (written < outbuf.pos) {
            written += fwrite((char *)outbuf.dst + written, sizeof(char), outbuf.pos - written, fh);
        }

        outbuf.pos = 0;
    }

    do {
        return_value = ZSTD_endStream(cstream, &outbuf);

        /* If at this point we haven't written anything out yet, it'd be a great
         * opportunity to write the right size before the data */
        fwrite(outbuf.dst, sizeof(char), outbuf.pos, fh);

        outbuf.pos = 0;
    } while (return_value != 0 && !ZSTD_isError(return_value));


    if (ZSTD_isError(return_value)) {
        MVM_free(outbuf.dst);
        MVM_free(result_buffer);
        MVM_panic(1, "Error compressing heap snapshot data: %s", ZSTD_getErrorName(return_value));
    }


    end_position = ftell(fh);

    if (col->second_level_toc) {
        MVMuint32 toc_i = get_new_toc_entry(tc, col->second_level_toc);
        col->second_level_toc->toc_words[toc_i] = "strings";
        col->second_level_toc->toc_positions[toc_i * 2]     = size_position;
        col->second_level_toc->toc_positions[toc_i * 2 + 1] = end_position;
    }

    ZSTD_freeCStream(cstream);

    MVM_free(outbuf.dst);
    MVM_free(result_buffer);
    col->strings_written = i;
}

#define SERIALIZE_ATTR_STREAM(tocname, first_ptr, second_ptr, structtype, fieldname, count) serialize_attribute_stream(\
        tc,\
        col,\
        tocname,\
        (first_ptr) + offsetof(structtype,fieldname),\
        (second_ptr) - (first_ptr),\
        sizeof(((structtype*)0)->fieldname),\
        (count),\
        col->fh);

void types_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    if (col->types_written < col->num_types) {

        char *first_type = (char *)&col->types[col->types_written];
        char *second_type = (char *)&col->types[col->types_written + 1];

        SERIALIZE_ATTR_STREAM("reprname", first_type, second_type, MVMHeapSnapshotType, repr_name, col->num_types - col->types_written);
        SERIALIZE_ATTR_STREAM("typename", first_type, second_type, MVMHeapSnapshotType, type_name, col->num_types - col->types_written);

#if DUMP_EVERYTHING_RAW
        {
            gzFile reprname_fh = open_coll_file(col, "reprname");
            gzFile typename_fh = open_coll_file(col, "typename");
            MVMuint64 i;

            for (i = col->types_written; i < col->types_written; i++) {
                MVMHeapSnapshotType *t = &col->types[i];

                gzprintf(reprname_fh, "%lld\n", t->repr_name);
                gzprintf(typename_fh, "%lld\n", t->type_name);
            }

            gzclose(reprname_fh);
            gzclose(typename_fh);
        }
#endif

        col->types_written = col->num_types;
    }
}

void static_frames_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    if (col->static_frames_written < col->num_static_frames) {

        char *first_sf = (char *)&col->static_frames[col->static_frames_written];
        char *second_sf = (char *)&col->static_frames[col->static_frames_written + 1];

        SERIALIZE_ATTR_STREAM("sfname", first_sf, second_sf, MVMHeapSnapshotStaticFrame, name, col->num_static_frames - col->static_frames_written);
        SERIALIZE_ATTR_STREAM("sfcuid", first_sf, second_sf, MVMHeapSnapshotStaticFrame, cuid, col->num_static_frames - col->static_frames_written);
        SERIALIZE_ATTR_STREAM("sfline", first_sf, second_sf, MVMHeapSnapshotStaticFrame, line, col->num_static_frames - col->static_frames_written);
        SERIALIZE_ATTR_STREAM("sffile", first_sf, second_sf, MVMHeapSnapshotStaticFrame, file, col->num_static_frames - col->static_frames_written);

#if DUMP_EVERYTHING_RAW
        {
            gzFile names_fh = open_coll_file(col, "names");
            gzFile cuid_fh = open_coll_file(col, "cuid");
            gzFile line_fh = open_coll_file(col, "line");
            gzFile file_fh =open_coll_file(col, "file");
            MVMuint64 i;

            for (i = col->static_frames_written; i < col->num_static_frames; i++) {
                MVMHeapSnapshotStaticFrame *sf = &col->static_frames[i];

                gzprintf(names_fh, "%lld\n", sf->name);
                gzprintf(cuid_fh, "%lld\n", sf->cuid);
                gzprintf(line_fh, "%lld\n", sf->line);
                gzprintf(file_fh, "%lld\n", sf->file);
            }

            gzclose(names_fh);
            gzclose(cuid_fh);
            gzclose(line_fh);
            gzclose(file_fh);
        }
#endif
        col->static_frames_written = col->num_static_frames;
    }
}
void collectables_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapDumpIndexSnapshotEntry *entry) {
    MVMHeapSnapshot *s = col->snapshot;

    char *first_coll = (char *)&s->collectables[0];
    char *second_coll = (char *)&s->collectables[1];

    SERIALIZE_ATTR_STREAM("colkind",  first_coll, second_coll, MVMHeapSnapshotCollectable, kind, s->num_collectables);
    SERIALIZE_ATTR_STREAM("colsize",  first_coll, second_coll, MVMHeapSnapshotCollectable, collectable_size, s->num_collectables);
    SERIALIZE_ATTR_STREAM("coltofi",  first_coll, second_coll, MVMHeapSnapshotCollectable, type_or_frame_index, s->num_collectables);
    SERIALIZE_ATTR_STREAM("colrfcnt", first_coll, second_coll, MVMHeapSnapshotCollectable, num_refs, s->num_collectables);
    SERIALIZE_ATTR_STREAM("colrfstr", first_coll, second_coll, MVMHeapSnapshotCollectable, refs_start, s->num_collectables);
    SERIALIZE_ATTR_STREAM("colusize", first_coll, second_coll, MVMHeapSnapshotCollectable, unmanaged_size, s->num_collectables);

#if DUMP_EVERYTHING_RAW
        {
            MVMuint64 i;
            gzFile kind_fh  = open_coll_file(col, "colkind");
            gzFile size_fh  = open_coll_file(col, "colsize");
            gzFile tofi_fh  = open_coll_file(col, "coltofi");
            gzFile rfcnt_fh = open_coll_file(col, "colrfcnt");
            gzFile rfstr_fh = open_coll_file(col, "colrfstr");
            gzFile usize_fh = open_coll_file(col, "colusize");

            for (i = 0; i < s->num_collectables; i++) {
                MVMHeapSnapshotCollectable *col = &s->collectables[i];

                gzprintf(kind_fh, "%lld\n", col->kind);
                gzprintf(size_fh, "%lld\n", col->collectable_size);
                gzprintf(tofi_fh, "%lld\n", col->type_or_frame_index);
                gzprintf(rfcnt_fh, "%lld\n", col->num_refs);
                gzprintf(rfstr_fh, "%lld\n", col->refs_start);
                gzprintf(usize_fh, "%lld\n", col->unmanaged_size);
            }

            gzclose(kind_fh);
            gzclose(size_fh);
            gzclose(tofi_fh);
            gzclose(rfcnt_fh);
            gzclose(rfstr_fh);
            gzclose(usize_fh);
        }
#endif
}

void references_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapDumpIndexSnapshotEntry *entry) {
    MVMHeapSnapshot *s = col->snapshot;

    char *first_ref = (char *)&s->references[0];
    char *second_ref = (char *)&s->references[1];

    SERIALIZE_ATTR_STREAM("refdescr",  first_ref, second_ref, MVMHeapSnapshotReference, description, s->num_references);
    SERIALIZE_ATTR_STREAM("reftrget",  first_ref, second_ref, MVMHeapSnapshotReference, collectable_index, s->num_references);
}

void write_toc_to_filehandle(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapDumpTableOfContents *to_write, MVMHeapDumpTableOfContents *to_register) {
    MVMuint64 toc_start_pos;
    MVMuint64 toc_end_pos;

    MVMuint32 i;

    char text[9] = {0};

    FILE *fh = col->fh;

    toc_start_pos = ftell(fh);

    /*fprintf(stderr, "writing toc %p at position %p - %d entries\n", to_write, toc_start_pos, to_write->toc_entry_used);*/
    /*if (to_register)*/
        /*fprintf(stderr, "  - will put an entry into %p\n", to_register);*/

    {
        MVMuint64 entries_to_write = to_write->toc_entry_used;
        strncpy(text, "toc", 8);
        fwrite(text, 8, 1, fh);
        fwrite(&entries_to_write, sizeof(MVMuint64), 1, fh);
    }

    for (i = 0; i < to_write->toc_entry_used; i++) {
        strncpy(text, to_write->toc_words[i], 8);
        fwrite(text, 8, 1, fh);
        fwrite(&to_write->toc_positions[i * 2],     sizeof(MVMuint64), 1, fh);
        fwrite(&to_write->toc_positions[i * 2 + 1], sizeof(MVMuint64), 1, fh);
    }

    toc_end_pos = ftell(fh);

    fwrite(&toc_start_pos, sizeof(MVMuint64), 1, fh);

    if (to_register) {
        MVMuint32 new_i = get_new_toc_entry(tc, to_register);

        to_register->toc_words[new_i] = "toc";
        to_register->toc_positions[new_i * 2]     = toc_start_pos;
        to_register->toc_positions[new_i * 2 + 1] = toc_end_pos;
    }
}

typedef struct {
    MVMuint64 identity;
    MVMuint64 value;
} to_sort_entry;

static int comparator(const void *one_entry, const void *two_entry) {
    to_sort_entry *one = (to_sort_entry *)one_entry;
    to_sort_entry *two = (to_sort_entry *)two_entry;

    if (one->value < two->value)
        return 1;
    if (one->value > two->value)
        return -1;
    return 0;
}

typedef struct leaderboard {
    MVMuint64 tofi;
    MVMuint64 value;
} leaderboard;

#define LEADERBOARDS_TOP_SPOTS 40

static void make_leaderboards(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapSnapshot *hs) {
    MVMHeapSnapshotStats *stats = hs->stats;
    to_sort_entry *data_body;
    MVMuint32 i;
    MVMuint8 which;

    leaderboard boards[4][LEADERBOARDS_TOP_SPOTS];

    /*char *descriptions[4] = {
        "types_by_count",
        "frames_by_count",
        "types_by_size",
        "frames_by_size",
    };*/

    if (!stats)
        return;

    /* keep one allocation for all the work */
    data_body = MVM_malloc(
            (stats->type_stats_alloc > stats->sf_stats_alloc ? stats->type_stats_alloc : stats->sf_stats_alloc)
            * sizeof(to_sort_entry)
            );
    /*fprintf(stderr, "{\n");*/
    for (which = 0; which < 4; which++) {
        MVMuint32 size = which == 0 || which == 2 ? stats->type_stats_alloc : stats->sf_stats_alloc;
        for (i = 0; i < size; i++) {
            data_body[i].identity = i;
            data_body[i].value =
                which == 0 ? stats->type_counts[i]
                : which == 1 ? stats->sf_counts[i]
                : which == 2 ? stats->type_size_sum[i]
                             : stats->sf_size_sum[i];
        }
        qsort((void *)data_body, size, sizeof(to_sort_entry), comparator);

        /*fprintf(stderr, "  %s: [\n", descriptions[which]);*/
        for (i = 0; i < LEADERBOARDS_TOP_SPOTS; i++) {
            /*char *name = NULL;*/
            boards[which][i].tofi = data_body[i].identity;
            boards[which][i].value = data_body[i].value;

            /*name = col->strings[which == 0 || which == 2*/
                    /*? col->types[data_body[i].identity].type_name : col->static_frames[data_body[i].identity].name];*/

            /*fprintf(stderr, "      { id: %d, name: \"%s\", score: %d }%s\n",*/
                    /*data_body[i].identity, name, data_body[i].value, i == 20 ? "" : ",");*/
        }
        /*fprintf(stderr, "  ]\n");*/
    }
    /*fprintf(stderr, "}\n");*/

    {
        char *first_entry  = (char *)(&boards[0][0].tofi);
        char *second_entry = (char *)(&boards[0][1].tofi);

        SERIALIZE_ATTR_STREAM("topIDs",    first_entry, second_entry, leaderboard, tofi,  4 * LEADERBOARDS_TOP_SPOTS);
        SERIALIZE_ATTR_STREAM("topscore",  first_entry, second_entry, leaderboard, value, 4 * LEADERBOARDS_TOP_SPOTS);
    }

    MVM_free(data_body);
}

void snapshot_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMHeapDumpIndexSnapshotEntry *entry = NULL;

    MVMHeapDumpTableOfContents *outer_toc = col->toplevel_toc;
    MVMHeapDumpTableOfContents *inner_toc = MVM_calloc(1, sizeof(MVMHeapDumpTableOfContents));

    /*fprintf(stderr, "creating a new TOC at %p\n", inner_toc);*/

    inner_toc->toc_entry_alloc = 8;
    inner_toc->toc_words = MVM_calloc(8, sizeof(char *));
    inner_toc->toc_positions = (MVMuint64 *)MVM_calloc(8, sizeof(MVMuint64) * 2);

    col->second_level_toc = inner_toc;

    snapmeta_to_filehandle_ver3(tc, col);

    collectables_to_filehandle_ver3(tc, col, entry);
    references_to_filehandle_ver3(tc, col, entry);

    string_heap_to_filehandle_ver3(tc, col);
    types_to_filehandle_ver3(tc, col);
    static_frames_to_filehandle_ver3(tc, col);

    make_leaderboards(tc, col, col->snapshot);

    write_toc_to_filehandle(tc, col, inner_toc, outer_toc);

    /* For easier recovery when ctrl-c'ing the process */
    write_toc_to_filehandle(tc, col, col->toplevel_toc, NULL);

    MVM_free(inner_toc->toc_words);
    MVM_free(inner_toc->toc_positions);
    MVM_free(inner_toc);
}
#endif /* heapsnapshot version 3 */

static void static_frames_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMuint64 i;
    MVMHeapDumpIndex *index = col->index;
    FILE *fh = col->fh;

    fputs("fram", fh);

#if DUMP_EVERYTHING_RAW
    gzFile names_fh, cuid_fh, line_fh, file_fh;
    names_fh = open_coll_file(col, "names");
    cuid_fh = open_coll_file(col, "cuid");
    line_fh = open_coll_file(col, "line");
    file_fh =open_coll_file(col, "file");
#endif

    i = col->num_static_frames - col->static_frames_written;

    fwrite(&i, sizeof(MVMuint64), 1, fh);
    i = sizeof(MVMuint64) * 4;
    fwrite(&i, sizeof(MVMuint64), 1, fh);
    index->staticframes_size = sizeof(MVMuint64) * 2 + 4 + sizeof(MVMuint64) * 4 * (col->num_static_frames - col->static_frames_written);

    for (i = col->static_frames_written; i < col->num_static_frames; i++) {
        MVMHeapSnapshotStaticFrame *sf = &col->static_frames[i];

        fwrite(&sf->name, sizeof(MVMuint64), 1, fh);
        fwrite(&sf->cuid, sizeof(MVMuint64), 1, fh);
        fwrite(&sf->line, sizeof(MVMuint64), 1, fh);
        fwrite(&sf->file, sizeof(MVMuint64), 1, fh);

#if DUMP_EVERYTHING_RAW
        gzprintf(names_fh, "%lld\n", sf->name);
        gzprintf(cuid_fh, "%lld\n", sf->cuid);
        gzprintf(line_fh, "%lld\n", sf->line);
        gzprintf(file_fh, "%lld\n", sf->file);
#endif
    }

    col->static_frames_written = col->num_static_frames;

#if DUMP_EVERYTHING_RAW
    gzclose(names_fh);
    gzclose(cuid_fh);
    gzclose(line_fh);
    gzclose(file_fh);
#endif
}


static void string_heap_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMuint64 i;
    MVMHeapDumpIndex *index = col->index;
    FILE *fh = col->fh;
#if DUMP_EVERYTHING_RAW
    gzFile str_fh = open_coll_file(col, "strings");
#endif

    i = col->strings_written;

    fputs("strs", fh);

    /* Write out the number of strings we have and record the "header" size
     * in the index. */

    fwrite(&i, sizeof(MVMuint64), 1, fh);
    index->stringheap_size = sizeof(MVMuint64) + 4;

    for (i = col->strings_written; i < col->num_strings; i++) {
        char *str = col->strings[i];
        MVMuint64 output_size = strlen(str);

        /* Every string is stored as a 64 bit integer length followed by utf8-
         * encoded string data. */

        fwrite(&output_size, sizeof(MVMuint64), 1, fh);
        fwrite(str, sizeof(MVMuint8), output_size, fh);

        /* Adjust the size by how much we wrote, including the size prefix */

        index->stringheap_size += sizeof(MVMuint64) + sizeof(MVMuint8) * output_size;


#if DUMP_EVERYTHING_RAW
        MVMuint32 smaller_size = strlen(str);
        gzfwrite(&smaller_size, sizeof(MVMuint32), 1, str_fh);
        gzfwrite(str, sizeof(MVMuint8), smaller_size, str_fh);
#endif
    }

    /* Record how many strings have been written so far.
     * This way, the string heap can be written after each
     * snapshot, so even if the process crashes, the heap
     * snapshots are usable */
    col->strings_written = col->num_strings;

#if DUMP_EVERYTHING_RAW
    gzclose(str_fh);
#endif
}

/* The references table has extreme potential for compression by first writing
 * out a "how many bytes per field" byte, then writing each field with the
 * determined size.
 *
 * That's why there's two entries in the snapshot size table for references:
 * so that the parser can confidently skip to the exact middle of
 * the references table and parse it with two threads in parallel.
 */
static void references_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapDumpIndexSnapshotEntry *entry) {
    MVMuint64 i;
    MVMuint64 halfway;
    FILE *fh = col->fh;
    MVMHeapSnapshot *s = col->snapshot;

    fputs("refs", fh);
    fwrite(&s->num_references, sizeof(MVMuint64), 1, fh);
    i = sizeof(MVMuint64) * 2 + 1;
    fwrite(&i, sizeof(MVMuint64), 1, fh);

    entry->full_refs_size = 4 + sizeof(MVMuint64) * 2;

    halfway = s->num_references / 2 - 1;

#if DUMP_EVERYTHING_RAW
    gzFile descr_fh, kind_fh, cindex_fh;
    descr_fh = open_coll_file(col, "descrs");
    kind_fh = open_coll_file(col, "kinds");
    cindex_fh = open_coll_file(col, "cindex");
#endif

    for (i = 0; i < s->num_references; i++) {
        MVMHeapSnapshotReference *ref = &s->references[i];
        MVMuint8  descr  = ref->description & ((1 << MVM_SNAPSHOT_REF_KIND_BITS) - 1);
        MVMuint64 kind   = ref->description >> MVM_SNAPSHOT_REF_KIND_BITS;
        MVMuint64 cindex = ref->collectable_index;

        MVMuint64 maxval = MAX(kind, cindex);

#if DUMP_EVERYTHING_RAW
        gzprintf(descr_fh, "%lld\n", descr);
        gzprintf(kind_fh, "%lld\n", kind);
        gzprintf(cindex_fh, "%lld\n", cindex);
#endif

        if (maxval + 1 >= 1LL << 32) {
            fputc('6', fh);
            fwrite(&descr, sizeof(MVMuint8), 1, fh);
            fwrite(&kind, sizeof(MVMuint64), 1, fh);
            fwrite(&cindex, sizeof(MVMuint64), 1, fh);
            entry->full_refs_size += sizeof(MVMuint64) * 2 + 2;
        }
        else if (maxval + 1 >= 1 << 16) {
            MVMuint32 kind32, index32;
            kind32  = kind;
            index32 = cindex;
            fputc('3', fh);
            fwrite(&descr, sizeof(MVMuint8), 1, fh);
            fwrite(&kind32, sizeof(MVMuint32), 1, fh);
            fwrite(&index32, sizeof(MVMuint32), 1, fh);
            entry->full_refs_size += sizeof(MVMuint32) * 2 + 2;
        }
        else if (maxval + 1 >= 1 << 8) {
            MVMuint16 kind16, index16;
            kind16  = kind;
            index16 = cindex;
            fputc('1', fh);
            fwrite(&descr, sizeof(MVMuint8), 1, fh);
            fwrite(&kind16, sizeof(MVMuint16), 1, fh);
            fwrite(&index16, sizeof(MVMuint16), 1, fh);
            entry->full_refs_size += sizeof(MVMuint16) * 2 + 2;
        }
        else {
            MVMuint8 kind8, index8;
            kind8  = kind;
            index8 = cindex;
            fputc('0', fh);
            fwrite(&descr, sizeof(MVMuint8), 1, fh);
            fwrite(&kind8, sizeof(MVMuint8), 1, fh);
            fwrite(&index8, sizeof(MVMuint8), 1, fh);
            entry->full_refs_size += sizeof(MVMuint8) * 3 + 1;
        }
        if (i == halfway) {
            entry->refs_middlepoint = entry->full_refs_size;
        }
    }

#if DUMP_EVERYTHING_RAW
    gzclose(descr_fh);
    gzclose(kind_fh);
    gzclose(cindex_fh);
#endif
}

/* The following functions all act the exact same way:
 *
 * Write a little introductory text of 4 bytes for the parser to ensure
 * the index is correct, write the number of entries and the size of each entry
 * as 64bit integers, calculate the complete size for the index, and then
 * just write out each entry
 *
 * We also write a partial table after every snapshot so that if the process
 * crashes we still have a usable file.
 */
static void types_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMuint64 i;
    MVMHeapDumpIndex *index = col->index;
    FILE *fh = col->fh;

    fputs("type", fh);

#if DUMP_EVERYTHING_RAW
    gzFile repr_fh, type_fh;
    repr_fh = open_coll_file(col, "repr");
    type_fh = open_coll_file(col, "type");
#endif

    i = col->num_types - col->types_written;

    fwrite(&i, sizeof(MVMuint64), 1, fh);
    i = sizeof(MVMuint64) * 2;
    fwrite(&i, sizeof(MVMuint64), 1, fh);

    index->types_size = sizeof(MVMuint64) * 2 + 4 + sizeof(MVMuint64) * 2 * (col->num_types - col->types_written);

    for (i = col->types_written; i < col->num_types; i++) {
        MVMHeapSnapshotType *t = &col->types[i];

        fwrite(&t->repr_name, sizeof(MVMuint64), 1, fh);
        fwrite(&t->type_name, sizeof(MVMuint64), 1, fh);

#if DUMP_EVERYTHING_RAW
        gzprintf(repr_fh, "%lld\n", t->repr_name);
        gzprintf(type_fh, "%lld\n", t->type_name);
#endif
    }

    col->types_written = col->num_types;

#if DUMP_EVERYTHING_RAW
    gzclose(repr_fh);
    gzclose(type_fh);
#endif
}
/* The collectables table gets an entry in the additional "snapshot sizes
 * table" that ends up before the general index at the end of the file.
 *
 * This sizes table has three entries for each entry. The first one is the
 * size of the collectables table.
 */
static void collectables_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapDumpIndexSnapshotEntry *entry) {
    MVMuint64 i;
    FILE *fh = col->fh;
    MVMHeapSnapshot *s = col->snapshot;

    fputs("coll", fh);

    fwrite(&s->num_collectables, sizeof(MVMuint64), 1, fh);
    i = sizeof(MVMuint16) * 2 + sizeof(MVMuint32) * 2 + sizeof(MVMuint64) * 2;
    fwrite(&i, sizeof(MVMuint64), 1, fh);

    entry->collectables_size += s->num_collectables * i + 4 + sizeof(MVMuint64) * 2;


#if DUMP_EVERYTHING_RAW
    gzFile kind_fh, tofi_fh, collsize_fh, unmansize_fh, refstart_fh, refcount_fh;
    kind_fh = open_coll_file(col, "kind");
    tofi_fh = open_coll_file(col, "tofi");
    collsize_fh = open_coll_file(col, "size");
    unmansize_fh =open_coll_file(col, "unman");
    refstart_fh = open_coll_file(col, "refstart");
    refcount_fh = open_coll_file(col, "refcount");
#endif

    for (i = 0; i < s->num_collectables; i++) {
        MVMHeapSnapshotCollectable *coll = &s->collectables[i];

#if DUMP_EVERYTHING_RAW
        gzprintf(kind_fh, "%lld\n", coll->kind);
        gzprintf(tofi_fh, "%lld\n", coll->type_or_frame_index);
        gzprintf(collsize_fh, "%lld\n", coll->collectable_size);
        gzprintf(unmansize_fh, "%lld\n", coll->unmanaged_size);
        gzprintf(refstart_fh, "%lld\n", coll->refs_start);
        gzprintf(refcount_fh, "%lld\n", coll->num_refs);
#endif

        fwrite(&coll->kind, sizeof(MVMuint16), 1, fh);
        fwrite(&coll->type_or_frame_index, sizeof(MVMuint32), 1, fh);
        fwrite(&coll->collectable_size, sizeof(MVMuint16), 1, fh);
        fwrite(&coll->unmanaged_size, sizeof(MVMuint64), 1, fh);
        if (coll->num_refs)
            fwrite(&coll->refs_start, sizeof(MVMuint64), 1, fh);
        else {
            MVMuint64 refs_start = 0;
            fwrite(&refs_start, sizeof(MVMuint64), 1, fh);
        }
        fwrite(&coll->num_refs, sizeof(MVMuint32), 1, fh);
    }
#if DUMP_EVERYTHING_RAW
    gzclose(kind_fh);
    gzclose(tofi_fh);
    gzclose(collsize_fh);
    gzclose(unmansize_fh);
    gzclose(refstart_fh);
    gzclose(refcount_fh);
#endif
}


static void snapshot_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMHeapDumpIndex *index = col->index;
    MVMuint64 i = col->snapshot_idx;
    MVMHeapDumpIndexSnapshotEntry *entry;

    grow_storage((void **)&(index->snapshot_sizes), &(index->snapshot_size_entries),
            &(index->snapshot_sizes_alloced), sizeof(MVMHeapDumpIndexSnapshotEntry));

    index->snapshot_size_entries++;

    entry = &(index->snapshot_sizes[i]);

    entry->collectables_size = 0;
    entry->full_refs_size    = 0;
    entry->refs_middlepoint  = 0;
    entry->incremental_data  = 0;

    collectables_to_filehandle_ver2(tc, col, entry);
    references_to_filehandle_ver2(tc, col, entry);

    string_heap_to_filehandle_ver2(tc, col);
    types_to_filehandle_ver2(tc, col);
    static_frames_to_filehandle_ver2(tc, col);

    /*entry->incremental_data = index->stringheap_size + index->types_size + index->staticframes_size;*/
}
static void index_to_filehandle(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMHeapDumpIndex *index = col->index;
    FILE *fh = col->fh;

    fwrite(index->snapshot_sizes, sizeof(MVMHeapDumpIndexSnapshotEntry), index->snapshot_size_entries, fh);
    fwrite(&index->stringheap_size, sizeof(MVMuint64), 1, fh);
    fwrite(&index->types_size, sizeof(MVMuint64), 1, fh);
    fwrite(&index->staticframes_size, sizeof(MVMuint64), 1, fh);
    fwrite(&index->snapshot_size_entries, sizeof(MVMuint64), 1, fh);
}

#if MVM_HEAPSNAPSHOT_FORMAT == 3
static void filemeta_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    char *metadata = MVM_malloc(1024);
    MVMuint64 size_position;
    MVMuint64 end_position;
    MVMuint64 size;
    FILE *fh = col->fh;

    char typename[8] = "filemeta";

    snprintf(metadata, 1023,
            "{ "
            "\"subversion\": %d, "
            "\"start_time\": %lu, "
            "\"pid\": %ld, "
            "\"highscore_structure\": { "
                "\"entry_count\": %d, "
                "\"data_order\": ["
                    "\"types_by_count\", \"frames_by_count\", \"types_by_size\", \"frames_by_size\""
                "]"
            "}"
            "}",
            MVM_HEAPSNAPSHOT_FORMAT_SUBVERSION,
            col->start_time / 1000,
            MVM_proc_getpid(tc),
            LEADERBOARDS_TOP_SPOTS
    );

    /* Be nice and put an extra null byte after the string */
    size = strlen(metadata) + 1;

    size_position = ftell(fh);
    fwrite(&typename, sizeof(char), 8, fh);
    fwrite(&size, sizeof(MVMuint64), 1, fh);

    fputs(metadata, fh);
    MVM_free(metadata);
    fputc(0, fh);

    end_position = ftell(fh);

    {
        MVMuint32 toc_i = get_new_toc_entry(tc, col->toplevel_toc);
        col->toplevel_toc->toc_words[toc_i] = "filemeta";
        col->toplevel_toc->toc_positions[toc_i * 2]     = size_position;
        col->toplevel_toc->toc_positions[toc_i * 2 + 1] = end_position;
    }
}

static void snapmeta_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    char *metadata = MVM_malloc(1024);
    MVMHeapSnapshot *s = col->snapshot;
    MVMuint64 size_position;
    MVMuint64 end_position;
    MVMuint64 size;
    FILE *fh = col->fh;

    char typename[8] = "snapmeta";

    snprintf(metadata, 1023,
            "{ "
            "\"snap_time\": %lu, "
            "\"gc_seq_num\": %lu, "
            "\"total_heap_size\": %lu, "
            "\"total_objects\": %lu, "
            "\"total_typeobjects\": %lu, "
            "\"total_stables\": %lu, "
            "\"total_frames\": %lu, "
            "\"total_refs\": %lu "
            "}",
            s->record_time / 1000,
            MVM_load(&tc->instance->gc_seq_number),
            col->total_heap_size,
            col->total_objects,
            col->total_typeobjects,
            col->total_stables,
            col->total_frames,
            col->snapshot->num_references
    );

    /* Be nice and put an extra null byte after the string */
    size = strlen(metadata) + 1;

    size_position = ftell(fh);
    fwrite(&typename, sizeof(char), 8, fh);
    fwrite(&size, sizeof(MVMuint64), 1, fh);

    fputs(metadata, fh);
    MVM_free(metadata);
    fputc(0, fh);

    end_position = ftell(fh);

    if (col->second_level_toc) {
        MVMuint32 toc_i = get_new_toc_entry(tc, col->second_level_toc);
        col->second_level_toc->toc_words[toc_i] = "snapmeta";
        col->second_level_toc->toc_positions[toc_i * 2]     = size_position;
        col->second_level_toc->toc_positions[toc_i * 2 + 1] = end_position;
    }
}
#endif

static void finish_collection_to_filehandle(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    /*col->strings_written = 0;*/
    /*col->types_written = 0;*/
    /*col->static_frames_written = 0;*/

#if MVM_HEAPSNAPSHOT_FORMAT == 3
    {
    MVMHeapDumpTableOfContents *inner_toc = MVM_calloc(1, sizeof(MVMHeapDumpTableOfContents));

    /*fprintf(stderr, "finishing collection; creating a new TOC at %p\n", inner_toc);*/

    inner_toc->toc_entry_alloc = 8;
    inner_toc->toc_words = MVM_calloc(8, sizeof(char *));
    inner_toc->toc_positions = (MVMuint64 *)MVM_calloc(8, sizeof(MVMuint64) * 2);

    col->second_level_toc = inner_toc;

    string_heap_to_filehandle_ver3(tc, col);
    types_to_filehandle_ver3(tc, col);
    static_frames_to_filehandle_ver3(tc, col);

    write_toc_to_filehandle(tc, col, col->second_level_toc, col->toplevel_toc);

    write_toc_to_filehandle(tc, col, col->toplevel_toc, NULL);
    }
#else
    string_heap_to_filehandle_ver2(tc, col);
    types_to_filehandle_ver2(tc, col);
    static_frames_to_filehandle_ver2(tc, col);

    index_to_filehandle(tc, col);
#endif
}

/* Takes a snapshot of the heap, outputting it to the filehandle */
void MVM_profile_heap_take_snapshot(MVMThreadContext *tc) {
    if (MVM_profile_heap_profiling(tc)) {
        MVMHeapSnapshotCollection *col = tc->instance->heap_snapshots;
        MVMint64 do_heapsnapshot = 1;
        if (MVM_confprog_has_entrypoint(tc, MVM_PROGRAM_ENTRYPOINT_HEAPSNAPSHOT)) {
            do_heapsnapshot = MVM_confprog_run(tc, NULL, MVM_PROGRAM_ENTRYPOINT_HEAPSNAPSHOT, do_heapsnapshot);
        }

        if (do_heapsnapshot) {
            col->snapshot = MVM_calloc(1, sizeof(MVMHeapSnapshot));

            col->snapshot->stats = MVM_calloc(1, sizeof(MVMHeapSnapshotStats));

            col->total_heap_size = 0;
            col->total_objects = 0;
            col->total_typeobjects = 0;
            col->total_stables = 0;
            col->total_frames = 0;

            col->snapshot->record_time = uv_hrtime();

            record_snapshot(tc, col, col->snapshot);

#if MVM_HEAPSNAPSHOT_FORMAT == 3
            snapshot_to_filehandle_ver3(tc, col);
#else
            snapshot_to_filehandle_ver2(tc, col);
#endif

            fflush(col->fh);
            destroy_current_heap_snapshot(tc);
        }

        col->snapshot_idx++;
    }
}

/* Finishes heap profiling, getting the data. */
MVMObject * MVM_profile_heap_end(MVMThreadContext *tc) {
    MVMHeapSnapshotCollection *col = tc->instance->heap_snapshots;
    MVMObject *dataset;

    /* Trigger a GC run, to ensure we get at least one heap snapshot. */
    MVM_gc_enter_from_allocator(tc);

    dataset = tc->instance->VMNull;

    finish_collection_to_filehandle(tc, tc->instance->heap_snapshots);
    fclose(col->fh);
    destroy_heap_snapshot_collection(tc);
    return dataset;
}
