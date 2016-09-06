#include "moar.h"

/* Check if we're currently taking heap snapshots. */
MVMint32 MVM_profile_heap_profiling(MVMThreadContext *tc) {
    return tc->instance->heap_snapshots != NULL;
}

/* Start heap profiling. */
void MVM_profile_heap_start(MVMThreadContext *tc, MVMObject *config) {
    tc->instance->heap_snapshots = MVM_calloc(1, sizeof(MVMHeapSnapshotCollection));
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
    HASH_ADD_KEYPTR(hash_handle, ss->seen, (char *)&(seen->address), sizeof(void *), seen);
}

/* Checks for an entry in the seen hash. If we find an entry, write the index
 * into the index pointer passed. */
static MVMuint32 seen(MVMThreadContext *tc, MVMHeapSnapshotState *ss, void *addr, MVMuint64 *idx) {
    MVMHeapSnapshotSeen *entry;
    HASH_FIND(hash_handle, ss->seen, (char *)&(addr), sizeof(void *), entry);
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

/* Gets the index of a frame, either returning an existing inde if we've seen
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
    MVMuint64 type_idx = st->debug_name
        ? get_string_index(tc, ss, st->debug_name, STR_MODE_DUP)
        : get_string_index(tc, ss, "<anon>", STR_MODE_CONST);

    MVMuint64 i;
    MVMHeapSnapshotType *t;
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
    while (c_ptr = MVM_gc_worklist_get(tc, ss->gcwl)) {
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
                    if (frame->spesh_cand && frame->spesh_log_idx == -1 && frame->spesh_cand->lexical_types) {
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
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)frame->dynlex_cache_name,
                    "Dynamic lexical cache name");

                if (frame->special_return_data && frame->mark_special_return_data) {
                    frame->mark_special_return_data(tc, frame, ss->gcwl);
                    process_gc_worklist(tc, ss, "Special return data");
                }

                if (frame->continuation_tags) {
                    MVMContinuationTag *tag = frame->continuation_tags;
                    while (tag) {
                        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                            (MVMCollectable *)tag->tag, "Continuation tag");
                        col.unmanaged_size += sizeof(MVMContinuationTag);
                        tag = tag->next;
                    }
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
                if (!MVM_FRAME_IS_ON_CALLSTACK(tc, tc->cur_frame))
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
    MVM_HASH_DESTROY(hash_handle, MVMHeapSnapshotSeen, ss.seen);
    MVM_gc_worklist_destroy(tc, ss.gcwl);
}

/* Takes a snapshot of the heap, adding it to the current heap snapshot
 * collection. */
void MVM_profile_heap_take_snapshot(MVMThreadContext *tc) {
    if (MVM_profile_heap_profiling(tc)) {
        MVMHeapSnapshotCollection *col = tc->instance->heap_snapshots;
        grow_storage(&(col->snapshots), &(col->num_snapshots), &(col->alloc_snapshots),
            sizeof(MVMHeapSnapshot));
        record_snapshot(tc, col, &(col->snapshots[col->num_snapshots]));
        col->num_snapshots++;
    }
}

/* Frees all memory associated with the heap snapshot. */
static void destroy_heap_snapshot_collection(MVMThreadContext *tc) {
    MVMHeapSnapshotCollection *col = tc->instance->heap_snapshots;
    MVMuint64 i;

    for (i = 0; i < col->num_snapshots; i++) {
        MVMHeapSnapshot *hs = &(col->snapshots[i]);
        MVM_free(hs->collectables);
        MVM_free(hs->references);
    }
    MVM_free(col->snapshots);

    for (i = 0; i < col->num_strings; i++)
        if (col->strings_free[i])
            MVM_free(col->strings[i]);
    MVM_free(col->strings);
    MVM_free(col->strings_free);

    MVM_free(col->types);
    MVM_free(col->static_frames);

    MVM_free(col);
    tc->instance->heap_snapshots = NULL;
}

/* Turns the collected data into MoarVM objects. */
#define vmstr(tc, cstr) MVM_string_utf8_decode(tc, tc->instance->VMString, cstr, strlen(cstr))
#define box_s(tc, str) MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type, str)
MVMObject * string_heap_array(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMObject *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStrArray);
    MVMuint64 i;
    for (i = 0; i < col->num_strings; i++)
        MVM_repr_bind_pos_s(tc, arr, i, vmstr(tc, col->strings[i]));
    return arr;
}
MVMObject * types_str(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    /* Produces ; separated sequences of:
     *   repr_string_index,type_name_string_index
     * Both of which are integers.
     */
     MVMObject *result;
     size_t buffer_size = 10 * col->num_types;
     size_t buffer_pos  = 0;
     char *buffer       = MVM_malloc(buffer_size);

     MVMuint64 i;
     for (i = 0; i < col->num_types; i++) {
         char tmp[256];
         int item_chars = snprintf(tmp, 256,
            "%"PRIu64",%"PRIu64";",
            col->types[i].repr_name,
            col->types[i].type_name);
         if (item_chars < 0)
             MVM_panic(1, "Failed to save type in heap snapshot");
         if (buffer_pos + item_chars >= buffer_size) {
             buffer_size += 4096;
             buffer = MVM_realloc(buffer, buffer_size);
         }
         memcpy(buffer + buffer_pos, tmp, item_chars);
         buffer_pos += item_chars;
     }
    if (buffer_pos > 1)
        buffer[buffer_pos - 1] = 0; /* Cut off the trailing ; for ease of parsing */
     buffer[buffer_pos] = 0;

     result = box_s(tc, vmstr(tc, buffer));
     MVM_free(buffer);
     return result;
}
MVMObject * static_frames_str(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    /* Produces ; separated sequences of:
     *   name_string_index,cuid_string_index,line_number,file_string_index
     * All of which are integers.
     */
     MVMObject *result;
     size_t buffer_size = 16 * col->num_static_frames;
     size_t buffer_pos  = 0;
     char *buffer       = MVM_malloc(buffer_size);

     MVMuint64 i;
     for (i = 0; i < col->num_static_frames; i++) {
         char tmp[256];
         int item_chars = snprintf(tmp, 256,
            "%"PRId64",%"PRId64",%"PRId64",%"PRId64";",
            col->static_frames[i].name,
            col->static_frames[i].cuid,
            col->static_frames[i].line,
            col->static_frames[i].file);
         if (item_chars < 0)
             MVM_panic(1, "Failed to save static frame in heap snapshot");
         if (buffer_pos + item_chars >= buffer_size) {
             buffer_size += 4096;
             buffer = MVM_realloc(buffer, buffer_size);
         }
         memcpy(buffer + buffer_pos, tmp, item_chars);
         buffer_pos += item_chars;
     }
    if (buffer_pos > 1)
        buffer[buffer_pos - 1] = 0; /* Cut off the trailing ; for ease of parsing */
     buffer[buffer_pos] = 0;

     result = box_s(tc, vmstr(tc, buffer));
     MVM_free(buffer);
     return result;
}
MVMObject * collectables_str(MVMThreadContext *tc, MVMHeapSnapshot *s) {
    /* Produces ; separated sequences of:
     *   kind,type_or_frame_index,collectable_size,unmanaged_size,refs_start,num_refs
     * All of which are integers.
     */
     MVMObject *result;
     size_t buffer_size = 20 * s->num_collectables;
     size_t buffer_pos  = 0;
     char *buffer       = MVM_malloc(buffer_size);

     MVMuint64 i;
     for (i = 0; i < s->num_collectables; i++) {
         char tmp[256];
         int item_chars = snprintf(tmp, 256,
            "%"PRIu16",%"PRId32",%"PRIu16",%"PRIu64",%"PRIu64",%"PRIu32";",
            s->collectables[i].kind,
            s->collectables[i].type_or_frame_index,
            s->collectables[i].collectable_size,
            s->collectables[i].unmanaged_size,
            s->collectables[i].num_refs ? s->collectables[i].refs_start : (MVMuint64)0,
            s->collectables[i].num_refs);
         if (item_chars < 0)
             MVM_panic(1, "Failed to save collectable in heap snapshot");
         if (buffer_pos + item_chars >= buffer_size) {
             buffer_size += 4096;
             buffer = MVM_realloc(buffer, buffer_size);
         }
         memcpy(buffer + buffer_pos, tmp, item_chars);
         buffer_pos += item_chars;
     }
    if (buffer_pos > 1)
        buffer[buffer_pos - 1] = 0; /* Cut off the trailing ; for ease of parsing */
     buffer[buffer_pos] = 0;

     result = box_s(tc, vmstr(tc, buffer));
     MVM_free(buffer);
     return result;
}
MVMObject * references_str(MVMThreadContext *tc, MVMHeapSnapshot *s) {
    /* Produces ; separated sequences of:
     *   kind,idx,to
     * All of which are integers.
     */
    MVMObject *result;
    size_t buffer_size = 10 * s->num_references;
    size_t buffer_pos  = 0;
    char *buffer       = MVM_malloc(buffer_size);

    MVMuint64 i;
    for (i = 0; i < s->num_references; i++) {
        char tmp[128];
        int item_chars = snprintf(tmp, 128, "%lu,%lu,%lu;",
            s->references[i].description & ((1 << MVM_SNAPSHOT_REF_KIND_BITS) - 1),
            s->references[i].description >> MVM_SNAPSHOT_REF_KIND_BITS,
            s->references[i].collectable_index);
        if (item_chars < 0)
            MVM_panic(1, "Failed to save reference in heap snapshot");
        if (buffer_pos + item_chars >= buffer_size) {
            buffer_size += 4096;
            buffer = MVM_realloc(buffer, buffer_size);
        }
        memcpy(buffer + buffer_pos, tmp, item_chars);
        buffer_pos += item_chars;
    }
    if (buffer_pos > 1)
        buffer[buffer_pos - 1] = 0; /* Cut off the trailing ; for ease of parsing */
    buffer[buffer_pos] = 0;

    result = box_s(tc, vmstr(tc, buffer));
    MVM_free(buffer);
    return result;
}
MVMObject * snapshot_to_mvm_object(MVMThreadContext *tc, MVMHeapSnapshot *s) {
    MVMObject *snapshot = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_hash_type);
    MVM_repr_bind_key_o(tc, snapshot, vmstr(tc, "collectables"),
        collectables_str(tc, s));
    MVM_repr_bind_key_o(tc, snapshot, vmstr(tc, "references"),
        references_str(tc, s));
    return snapshot;
}
MVMObject * snapshots_to_mvm_objects(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMObject *arr = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
    MVMuint64 i;
    for (i = 0; i < col->num_snapshots; i++)
        MVM_repr_bind_pos_o(tc, arr, i,
            snapshot_to_mvm_object(tc, &(col->snapshots[i])));
    return arr;
}
MVMObject * collection_to_mvm_objects(MVMThreadContext *tc, MVMHeapSnapshotCollection *col) {
    MVMObject *results;

    /* Allocate in gen2, so as not to trigger GC. */
    MVM_gc_allocate_gen2_default_set(tc);

    /* Top-level results is a hash. */
    results = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_hash_type);
    MVM_repr_bind_key_o(tc, results, vmstr(tc, "strings"),
        string_heap_array(tc, col));
    MVM_repr_bind_key_o(tc, results, vmstr(tc, "types"),
        types_str(tc, col));
    MVM_repr_bind_key_o(tc, results, vmstr(tc, "static_frames"),
        static_frames_str(tc, col));
    MVM_repr_bind_key_o(tc, results, vmstr(tc, "snapshots"),
        snapshots_to_mvm_objects(tc, col));

    /* Switch off gen2 allocations now we're done. */
    MVM_gc_allocate_gen2_default_clear(tc);

    return results;
}

/* Finishes heap profiling, getting the data. */
MVMObject * MVM_profile_heap_end(MVMThreadContext *tc) {
    MVMObject *dataset;

    /* Trigger a GC run, to ensure we get at least one heap snapshot. */
    MVM_gc_enter_from_allocator(tc);

    /* Process and return the data. */
    dataset = collection_to_mvm_objects(tc, tc->instance->heap_snapshots);
    destroy_heap_snapshot_collection(tc);
    return dataset;
}
