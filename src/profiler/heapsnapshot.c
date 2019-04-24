#include "moar.h"

#ifndef MVM_HEAPSNAPSHOT_FORMAT
#define MVM_HEAPSNAPSHOT_FORMAT 3
#endif

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

/* Start heap profiling. */
void MVM_profile_heap_start(MVMThreadContext *tc, MVMObject *config) {
    MVMHeapSnapshotCollection *col = MVM_calloc(1, sizeof(MVMHeapSnapshotCollection));
    char *path;
    MVMString *path_str;

    path_str = MVM_repr_get_str(tc,
        MVM_repr_at_key_o(tc, config, tc->instance->str_consts.path));

    if (MVM_is_null(tc, (MVMObject*)path_str)) {
        MVM_exception_throw_adhoc(tc, "Didn't specify a path for the heap snapshot profiler");
    }

    path = MVM_string_utf8_encode_C_string(tc, path_str);

    col->fh = fopen(path, "w");

    if (!col->fh) {
        char *waste[2] = {path, NULL};
        MVM_exception_throw_adhoc_free(tc, waste, "Couldn't open heap snapshot target file %s: %s", path, strerror(errno));
    }
    MVM_free(path);

    if (MVM_HEAPSNAPSHOT_FORMAT == 2) {
        col->index = MVM_calloc(1, sizeof(MVMHeapDumpIndex));
        col->index->snapshot_sizes = MVM_calloc(1, sizeof(MVMHeapDumpIndexSnapshotEntry));
        tc->instance->heap_snapshots = col;
    }
    else if (MVM_HEAPSNAPSHOT_FORMAT == 3) {
        MVMHeapDumpTableOfContents *tocs[2] = { col->toplevel_toc, col->second_level_toc };
        MVMuint8 i;
        for (i = 0; i < 2; i++) {
            MVMHeapDumpTableOfContents *toc = MVM_calloc(1, sizeof(MVMHeapDumpTableOfContents));
            tocs[i] = toc;
            toc->toc_entry_alloc = 8;
            toc->toc_words = MVM_calloc(8, sizeof(char *));
            toc->toc_positions = (MVMuint64 *)MVM_calloc(8, sizeof(MVMuint64));
        }
    }

    fprintf(col->fh, "MoarHeapDumpv00%d", MVM_HEAPSNAPSHOT_FORMAT);
}

/* Grows storage if it's full, zeroing the extension. Assumes it's only being
 * grown for one more item. */
static void grow_storage(void *store_ptr, MVMuint64 *num, MVMuint64 *alloc, size_t size) {
    void **store = (void **)store_ptr;
    if (*num == *alloc) {
        *alloc = *alloc ? 2 * *alloc : 32;
        *store = MVM_realloc(*store, *alloc * size);
        memset(((char *)*store) + *num * size, 0, (*alloc - *num) * size);
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
    col->strings[col->num_strings] = str_mode == STR_MODE_DUP ? strdup(str) : str;
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

/* Adds a reference with a VM string description. */
static void add_reference_vm_str(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                               MVMString *str,  MVMuint64 to) {
   MVMuint64 str_idx = get_vm_string_index(tc, ss, str);
   add_reference(tc, ss, MVM_SNAPSHOT_REF_KIND_STRING, str_idx, to);
}

/* Adds an entry to the seen hash. */
static void saw(MVMThreadContext *tc, MVMHeapSnapshotState *ss, void *addr, MVMuint64 idx) {
    MVMHeapSnapshotSeen *seen = MVM_calloc(1, sizeof(MVMHeapSnapshotSeen));
    seen->address = addr;
    seen->idx = idx;
    HASH_ADD_KEYPTR(hash_handle, ss->seen, &(seen->address), sizeof(void *), seen);
}

/* Checks for an entry in the seen hash. If we find an entry, write the index
 * into the index pointer passed. */
static MVMuint32 seen(MVMThreadContext *tc, MVMHeapSnapshotState *ss, void *addr, MVMuint64 *idx) {
    MVMHeapSnapshotSeen *entry;
    HASH_FIND(hash_handle, ss->seen, &addr, sizeof(void *), entry);
    if (entry) {
        *idx = entry->idx;
        return 1;
    }
    else {
        return 0;
    }
}

/* Gets the index of a collectable, either returning an existing index if we've
 * seen it before or adding it if not. */
static MVMuint64 get_collectable_idx(MVMThreadContext *tc,
        MVMHeapSnapshotState *ss, MVMCollectable *collectable) {
    MVMuint64 idx;
    if (!seen(tc, ss, collectable, &idx)) {
        if (collectable->flags & MVM_CF_STABLE)
            idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_STABLE, collectable);
        else if (collectable->flags & MVM_CF_TYPE_OBJECT)
            idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_TYPE_OBJECT, collectable);
        else if (collectable->flags & MVM_CF_FRAME)
            idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_FRAME, collectable);
        else
            idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_OBJECT, collectable);
        saw(tc, ss, collectable, idx);
    }
    return idx;
}

/* Gets the index of a frame, either returning an existing index if we've seen
 * it before or adding it if not. */
static MVMuint64 get_frame_idx(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
        MVMFrame *frame) {
    MVMuint64 idx;
    if (!seen(tc, ss, frame, &idx)) {
        idx = push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_FRAME, frame);
        saw(tc, ss, frame, idx);
    }
    return idx;
}

/* Adds a type table index reference to the collectable snapshot entry, either
 * using an existing type table entry or adding a new one. */
static void set_type_index(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
       MVMHeapSnapshotCollectable *col, MVMSTable *st) {
    MVMuint64 repr_idx = get_string_index(tc, ss, (char *)st->REPR->name, STR_MODE_CONST);
    char *debug_name = MVM_6model_get_stable_debug_name(tc, st);
    MVMuint64 type_idx;

    MVMuint64 i;
    MVMHeapSnapshotType *t;

    if (strlen(debug_name) != 0) {
        type_idx = get_string_index(tc, ss, MVM_6model_get_stable_debug_name(tc, st), STR_MODE_DUP);
    }
    else {
        char anon_with_repr[256] = {0};
        snprintf(anon_with_repr, 250, "<anon %s>", st->REPR->name);
        type_idx = get_string_index(tc, ss, anon_with_repr, STR_MODE_DUP);
    }

    for (i = 0; i < ss->col->num_types; i++) {
        t = &(ss->col->types[i]);
        if (t->repr_name == repr_idx && t->type_name == type_idx) {
            col->type_or_frame_index = i;
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
    MVMuint64 file_idx = ann && ann->filename_string_heap_index < cu->body.num_strings
        ? get_vm_string_index(tc, ss, MVM_cu_string(tc, cu, ann->filename_string_heap_index))
        : get_vm_string_index(tc, ss, cu->body.filename);

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
        MVMHeapSnapshotCollectable *col, MVMCollectable *c) {
    MVMuint32 sc_idx = MVM_sc_get_idx_of_sc(c);
    if (sc_idx > 0)
        add_reference_const_cstr(tc, ss, "<SC>",
            get_collectable_idx(tc, ss,
                (MVMCollectable *)tc->instance->all_scs[sc_idx]->sc));
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
static void process_object(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
        MVMHeapSnapshotCollectable *col, MVMObject *obj) {
    process_collectable(tc, ss, col, (MVMCollectable *)obj);
    set_type_index(tc, ss, col, obj->st);
    add_reference_const_cstr(tc, ss, "<STable>",
        get_collectable_idx(tc, ss, (MVMCollectable *)obj->st));
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
    }
}
static void process_workitems(MVMThreadContext *tc, MVMHeapSnapshotState *ss) {
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
                process_object(tc, ss, &col, (MVMObject *)item.target);
                break;
            case MVM_SNAPSHOT_COL_KIND_STABLE: {
                MVMuint16 i;
                MVMSTable *st = (MVMSTable *)item.target;
                process_collectable(tc, ss, &col, (MVMCollectable *)st);
                set_type_index(tc, ss, &col, st);

                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)st->method_cache, "Method cache");

                for (i = 0; i < st->type_check_cache_length; i++)
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->type_check_cache[i], "Type cache entry");

                if (st->container_spec && st->container_spec->gc_mark_data) {
                    st->container_spec->gc_mark_data(tc, st, ss->gcwl);
                    process_gc_worklist(tc, ss, "Container spec data item");
                }

                if (st->boolification_spec)
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->boolification_spec->method,
                        "Boolification method");

                if (st->invocation_spec) {
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->invocation_spec->class_handle,
                        "Invocation spec class handle");
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->invocation_spec->attr_name,
                        "Invocation spec attribute name");
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->invocation_spec->invocation_handler,
                        "Invocation spec invocation handler");
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->invocation_spec->md_class_handle,
                        "Invocation spec class handle (multi)");
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->invocation_spec->md_cache_attr_name,
                        "Invocation spec cache attribute name (multi)");
                    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                        (MVMCollectable *)st->invocation_spec->md_valid_attr_name,
                        "Invocation spec valid attribute name (multi)");
                }

                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)st->WHO, "WHO");
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)st->WHAT, "WHAT");
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)st->HOW, "HOW");
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)st->HOW_sc, "HOW serialization context");
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)st->method_cache_sc,
                    "Method cache serialization context");

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
                    MVMLexicalRegistry **names = frame->static_info->body.lexical_names_list;
                    if (frame->spesh_cand && frame->spesh_cand->lexical_types) {
                        type_map = frame->spesh_cand->lexical_types;
                        count    = frame->spesh_cand->num_lexicals;
                    }
                    else {
                        type_map = frame->static_info->body.lexical_types;
                        count    = frame->static_info->body.num_lexicals;
                    }
                    for (i = 0; i < count; i++) {
                        if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj) {
                            if (i < name_count)
                                MVM_profile_heap_add_collectable_rel_vm_str(tc, ss,
                                    (MVMCollectable *)frame->env[i].o, names[i]->key);
                            else
                                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                                    (MVMCollectable *)frame->env[i].o, "Lexical (inlined)");
                        }
                    }
                    col.unmanaged_size += frame->allocd_env;
                }

                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)frame->code_ref, "Code reference");
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)frame->static_info, "Static frame");

                if (frame->extra) {
                    MVMFrameExtra *e = frame->extra;
                    if (e->special_return_data && e->mark_special_return_data) {
                        e->mark_special_return_data(tc, frame, ss->gcwl);
                        process_gc_worklist(tc, ss, "Special return data");
                    }
                    if (e->continuation_tags) {
                        MVMContinuationTag *tag = e->continuation_tags;
                        while (tag) {
                            MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                                (MVMCollectable *)tag->tag, "Continuation tag");
                            col.unmanaged_size += sizeof(MVMContinuationTag);
                            tag = tag->next;
                        }
                    }
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
        MVMHeapSnapshotState *ss, MVMCollectable *collectable, char *desc) {
    if (collectable)
        add_reference_const_cstr(tc, ss, desc,
            get_collectable_idx(tc, ss, collectable));
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
    MVM_HASH_DESTROY(tc, hash_handle, MVMHeapSnapshotSeen, ss.seen);
    MVM_gc_worklist_destroy(tc, ss.gcwl);
}

static void destroy_current_heap_snapshot(MVMThreadContext *tc) {
    MVMHeapSnapshotCollection *col = tc->instance->heap_snapshots;

    MVM_free(col->snapshot->collectables);
    MVM_free(col->snapshot->references);
    MVM_free(col->snapshot);

    col->snapshot = NULL;
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

    MVM_free(col->index->snapshot_sizes);
    MVM_free(col->index);

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
void serialize_attribute_stream(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, char *name, char *start, size_t offset, size_t elem_size, size_t count, FILE* fh) {
    char *pointer = (char *)start;

    ZSTD_CStream *cstream;

    size_t outSize = ZSTD_CStreamOutSize();

    ZSTD_inBuffer inbuf;
    ZSTD_outBuffer outbuf;

    size_t size_position = ftell(fh);

    MVMuint16 elem_size_to_write = elem_size;

    size_t written_total = 0;
    size_t original_total = count * elem_size;

    char *out_buffer = MVM_malloc(outSize);
    outbuf.dst = out_buffer;
    outbuf.pos = 0;
    outbuf.size = outSize;

    cstream = ZSTD_createCStream();
    if (ZSTD_isError(ZSTD_initCStream(cstream, 16))) {
        MVM_panic(1, "oops");
    }

    fprintf(stderr, "will compress %lld bytes per field\n", elem_size);

    /* Write the size per entry to the file */
    fwrite(&elem_size_to_write, sizeof(MVMuint16), 1, fh);

    while (count > 0) {
        size_t retval;
        size_t written;
        ZSTD_inBuffer inbuf;

        inbuf.src = pointer;
        inbuf.pos = 0;
        inbuf.size = elem_size;


        if (ZSTD_isError(retval = ZSTD_compressStream(cstream, &outbuf, &inbuf))) {
            fprintf(stderr, "compression error: %s\n", ZSTD_getErrorName(retval));
        };

        if (outbuf.pos) {
            fprintf(stderr, "%lld ", outbuf.pos);

            written = fwrite(outbuf.dst, sizeof(char), outbuf.pos, fh);
            written_total += written;
            outbuf.pos = 0;
        }

        pointer += offset;
        count--;
    }

    {
        size_t written;
        size_t retval;

        if (ZSTD_isError(retval = ZSTD_endStream(cstream, &outbuf))) {
            fprintf(stderr, "compression error in endStream: %s\n", ZSTD_getErrorName(retval));
        };

        if (outbuf.pos) {
            fprintf(stderr, "%lld.\n", outbuf.pos);

            written = fwrite(outbuf.dst, sizeof(char), outbuf.pos, fh);
            written_total += written;
            outbuf.pos = 0;
        }
        else {
            fprintf(stderr, "should not happen?!?\n");
        }
    }

    fprintf(stderr, "for an attribute, wrote %lld kibytes (compressed from %lld kibytes, %f%%)\n", written_total / 1024,
            original_total / 1024, written_total * 100.0f / original_total);

    MVM_free(out_buffer);
}


void string_heap_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMuint64 i = col->strings_written;
    MVMHeapDumpIndex *index = col->index;
    FILE *fh = fopen("/tmp/new_format_output.mvmheap", "a");

    size_t result_buffer_size = 2048;
    char *result_buffer = MVM_malloc(2048);
    char *result_buffer_insert_pos = result_buffer;

    ZSTD_CStream *cstream;

    /*size_t inSize  = ZSTD_CStreamInSize();*/
    size_t outSize = ZSTD_CStreamOutSize();

    ZSTD_inBuffer inbuf;
    ZSTD_outBuffer outbuf;

    /*ZSTD_outBuffer outbuf = { MVM_malloc(outSize), outSize, 0 };*/

    char typename[8] = "strings\0";
    MVMuint64 size = 0;

    MVMuint64 size_position = 0; 

    while (i < col->num_strings) {
        char *str = col->strings[i];
        MVMuint32 output_size = strlen(str);

        if (result_buffer_insert_pos + output_size + 4 >= result_buffer + result_buffer_size) {
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

    fprintf(stderr, "%d kilobytes of string data in buffer\n", (result_buffer_insert_pos - result_buffer) / 1024);

    if (result_buffer_insert_pos == result_buffer)
        return;

    fwrite(&typename, sizeof(char), 8, fh);
    /* If the stream is seekable, we can go back here to write the size,
     * but only if we don't write everything in one go anyway */
    size_position = ftell(fh);

    cstream = ZSTD_createCStream();

    if (ZSTD_isError(ZSTD_initCStream(cstream, 19))) {
        MVM_panic(1, "oops");
    }

    inbuf.src = result_buffer;
    inbuf.pos = 0;
    inbuf.size = result_buffer_insert_pos - result_buffer;
    fprintf(stderr, "inbuf: %x %x\n", inbuf.pos, inbuf.size);

    /* TODO write the right size here later */
    fwrite(&size, sizeof(MVMuint64), 1, fh);

    outbuf.dst = MVM_malloc(1024 * 10);

    while (inbuf.pos < inbuf.size) {
        size_t returnval = 0;
        size_t written = 0;

        outbuf.pos = 0;
        outbuf.size = 1024 * 10;

        fprintf(stderr, "inbuf: %x %x\n", inbuf.pos, inbuf.size);
        returnval = ZSTD_compressStream(cstream, &outbuf, &inbuf);
        fprintf(stderr, "returnvalue: %d\n", returnval);
        fprintf(stderr, "outbuf: %x %x\n", outbuf.pos, outbuf.size);

        if (ZSTD_isError(returnval)) {
            fprintf(stderr, "%s\n", ZSTD_getErrorName(returnval));
            MVM_panic(1, "oops");
        }

        inbuf.src = (char *)inbuf.src + inbuf.pos;
        inbuf.size -= inbuf.pos;
        inbuf.pos = 0;

        written = fwrite(outbuf.dst, sizeof(char), outbuf.pos, fh);

        outbuf.pos = 0;
        fprintf(stderr, "once around the loop\n");
    }

    ZSTD_endStream(cstream, &outbuf);

    /* If at this point we haven't written anything out yet, it'd be a great
     * opportunity to write the right size before the data */
    fwrite(outbuf.dst, sizeof(char), outbuf.pos, fh);

    MVM_free(outbuf.dst);
    MVM_free(result_buffer);
    col->strings_written = i;

    fclose(fh);
}

#define SERIALIZE_ATTR_STREAM(tocname, first_ptr, second_ptr, structtype, fieldname, count) serialize_attribute_stream(\
        tc,\
        col,\
        tocname,\
        (first_ptr) + offsetof(structtype,fieldname),\
        (second_ptr) - (first_ptr),\
        sizeof(((structtype*)0)->fieldname),\
        (count),\
        fh);

void types_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    if (col->types_written < col->num_types) {
        FILE *fh = fopen("/tmp/new_format_output.mvmheap", "a");

        char *first_type = (char *)&col->types[col->types_written];
        char *second_type = (char *)&col->types[col->types_written + 1];

        SERIALIZE_ATTR_STREAM("reprname", first_type, second_type, MVMHeapSnapshotType, repr_name, col->num_types - col->types_written);
        SERIALIZE_ATTR_STREAM("typename", first_type, second_type, MVMHeapSnapshotType, type_name, col->num_types - col->types_written);
        fprintf(stderr, "%lld types written\n", col->num_types - col->types_written);
    }
    else {
        fprintf(stderr, "no types written\n");
    }
}

void static_frames_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    if (col->static_frames_written < col->num_static_frames) {
        FILE *fh = fopen("/tmp/new_format_output.mvmheap", "a");

        char *first_sf = (char *)&col->static_frames[col->types_written];
        char *second_sf = (char *)&col->static_frames[col->types_written + 1];

        SERIALIZE_ATTR_STREAM("sfname", first_sf, second_sf, MVMHeapSnapshotStaticFrame, name, col->num_static_frames - col->static_frames_written);
        SERIALIZE_ATTR_STREAM("sfcuid", first_sf, second_sf, MVMHeapSnapshotStaticFrame, cuid, col->num_static_frames - col->static_frames_written);
        SERIALIZE_ATTR_STREAM("sfline", first_sf, second_sf, MVMHeapSnapshotStaticFrame, line, col->num_static_frames - col->static_frames_written);
        SERIALIZE_ATTR_STREAM("sffile", first_sf, second_sf, MVMHeapSnapshotStaticFrame, file, col->num_static_frames - col->static_frames_written);
        fprintf(stderr, "%lld SFs written\n", col->num_static_frames - col->static_frames_written);
    }
    else {
        fprintf(stderr, "no SFs written\n");
    }
}
void collectables_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapDumpIndexSnapshotEntry *entry) {
    FILE *fh = fopen("/tmp/new_format_output.mvmheap", "a");
    MVMHeapSnapshot *s = col->snapshot;

    char *first_coll = (char *)&s->collectables[0];
    char *second_coll = (char *)&s->collectables[1];

    SERIALIZE_ATTR_STREAM("colkind",  first_coll, second_coll, MVMHeapSnapshotCollectable, kind, s->num_collectables);
    SERIALIZE_ATTR_STREAM("colsize",  first_coll, second_coll, MVMHeapSnapshotCollectable, collectable_size, s->num_collectables);
    SERIALIZE_ATTR_STREAM("coltofi",  first_coll, second_coll, MVMHeapSnapshotCollectable, type_or_frame_index, s->num_collectables);
    SERIALIZE_ATTR_STREAM("colrfcnt", first_coll, second_coll, MVMHeapSnapshotCollectable, num_refs, s->num_collectables);
    SERIALIZE_ATTR_STREAM("colrfstr", first_coll, second_coll, MVMHeapSnapshotCollectable, refs_start, s->num_collectables);
    SERIALIZE_ATTR_STREAM("colusize", first_coll, second_coll, MVMHeapSnapshotCollectable, unmanaged_size, s->num_collectables);
}

void references_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapDumpIndexSnapshotEntry *entry) {
    FILE *fh = fopen("/tmp/new_format_output.mvmheap", "a");
    MVMHeapSnapshot *s = col->snapshot;

    char *first_ref = (char *)&s->references[0];
    char *second_ref = (char *)&s->references[1];

    SERIALIZE_ATTR_STREAM("refdescr",  first_ref, second_ref, MVMHeapSnapshotReference, description, s->num_references);
    SERIALIZE_ATTR_STREAM("reftrget",  first_ref, second_ref, MVMHeapSnapshotReference, collectable_index, s->num_references);
}

void snapshot_to_filehandle_ver3(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMuint64 i = col->snapshot_idx;
    MVMHeapDumpIndexSnapshotEntry *entry;

    MVMHeapDumpTableOfContents *outer_toc = col->toplevel_toc;
    MVMHeapDumpTableOfContents *inner_toc;

    if (outer_toc->toc_entry_used == outer_toc->toc_entry_alloc) {
        outer_toc->toc_entry_alloc *= 2;

        outer_toc->toc_words     = MVM_realloc(outer_toc->toc_words,     sizeof(char *)    * outer_toc->toc_entry_alloc);
        outer_toc->toc_positions = MVM_realloc(outer_toc->toc_positions, sizeof(MVMuint64) * outer_toc->toc_entry_alloc);
    }

    inner_toc = &outer_toc[i];
    inner_toc->toc_entry_alloc = 8;
    inner_toc->toc_words = MVM_calloc(8, sizeof(char *));
    inner_toc->toc_positions = (MVMuint64 *)MVM_calloc(8, sizeof(MVMuint64));

    /*collectables_to_filehandle_ver2(tc, col, entry);*/
    /*references_to_filehandle_ver2(tc, col, entry);*/

    /*string_heap_to_filehandle_ver2(tc, col);*/
    /*types_to_filehandle_ver2(tc, col);*/
    /*static_frames_to_filehandle_ver2(tc, col);*/

    MVM_free(inner_toc);
}
#endif /* heapsnapshot version 3 */

void static_frames_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMuint64 i;
    MVMHeapDumpIndex *index = col->index;
    FILE *fh = col->fh;
    gzFile names_fh, cuid_fh, line_fh, file_fh;

    fputs("fram", fh);

#if DUMP_EVERYTHING_RAW
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


void string_heap_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
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
        MVMuint32 smaller_size = strlen(str);

        /* Every string is stored as a 64 bit integer length followed by utf8-
         * encoded string data. */

        fwrite(&output_size, sizeof(MVMuint64), 1, fh);
        fwrite(str, sizeof(MVMuint8), output_size, fh);

        /* Adjust the size by how much we wrote, including the size prefix */

        index->stringheap_size += sizeof(MVMuint64) + sizeof(MVMuint8) * output_size;


#if DUMP_EVERYTHING_RAW
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
void references_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapDumpIndexSnapshotEntry *entry) {
    MVMuint64 i;
    MVMuint64 halfway;
    FILE *fh = col->fh;
    MVMHeapSnapshot *s = col->snapshot;
    gzFile descr_fh, kind_fh, cindex_fh;

    fputs("refs", fh);
    fwrite(&s->num_references, sizeof(MVMuint64), 1, fh);
    i = sizeof(MVMuint64) * 2 + 1;
    fwrite(&i, sizeof(MVMuint64), 1, fh);

    entry->full_refs_size = 4 + sizeof(MVMuint64) * 2;

    halfway = s->num_references / 2 - 1;

#if DUMP_EVERYTHING_RAW
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
            MVMuint8 descr8, kind8, index8;
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
void types_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMuint64 i;
    MVMHeapDumpIndex *index = col->index;
    FILE *fh = col->fh;
    gzFile repr_fh, type_fh;

    fputs("type", fh);

#if DUMP_EVERYTHING_RAW
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
void collectables_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapDumpIndexSnapshotEntry *entry) {
    MVMuint64 i;
    FILE *fh = col->fh;
    MVMHeapSnapshot *s = col->snapshot;
    gzFile kind_fh, tofi_fh, collsize_fh, unmansize_fh, refstart_fh, refcount_fh;

    fputs("coll", fh);

    fwrite(&s->num_collectables, sizeof(MVMuint64), 1, fh);
    i = sizeof(MVMuint16) * 2 + sizeof(MVMuint32) * 2 + sizeof(MVMuint64) * 2;
    fwrite(&i, sizeof(MVMuint64), 1, fh);

    entry->collectables_size += s->num_collectables * i + 4 + sizeof(MVMuint64) * 2;

    
#if DUMP_EVERYTHING_RAW
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


void snapshot_to_filehandle_ver2(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
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
void index_to_filehandle(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMHeapDumpIndex *index = col->index;
    FILE *fh = col->fh;

    fwrite(index->snapshot_sizes, sizeof(MVMHeapDumpIndexSnapshotEntry), index->snapshot_size_entries, fh);
    fwrite(&index->stringheap_size, sizeof(MVMuint64), 1, fh);
    fwrite(&index->types_size, sizeof(MVMuint64), 1, fh);
    fwrite(&index->staticframes_size, sizeof(MVMuint64), 1, fh);
    fwrite(&index->snapshot_size_entries, sizeof(MVMuint64), 1, fh);
}
void finish_collection_to_filehandle(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    col->strings_written = 0;
    col->types_written = 0;
    col->static_frames_written = 0;

    string_heap_to_filehandle_ver2(tc, col);
    types_to_filehandle_ver2(tc, col);
    static_frames_to_filehandle_ver2(tc, col);

    index_to_filehandle(tc, col);
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
