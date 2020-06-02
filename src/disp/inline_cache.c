#include "moar.h"

MVMuint32 try_update_cache_entry(MVMThreadContext *tc, MVMDispInlineCacheEntry **target,
        MVMDispInlineCacheEntry *from, MVMDispInlineCacheEntry *to);

/**
 * Inline caching of getlexstatic_o
 **/

static MVMObject * getlexstatic_initial(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *name);
static MVMObject * getlexstatic_resolved(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *name);

/* Unlinked node. */
static MVMDispInlineCacheEntry unlinked_getlexstatic = { getlexstatic_initial };

/* Initial unlinked handler. */
static MVMObject * getlexstatic_initial(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *name) {
    /* Do the lookup. */
    MVMRegister *found = MVM_frame_find_lexical_by_name(tc, name, MVM_reg_obj);
    MVMObject *result = found ? found->o : tc->instance->VMNull;

    /* Set up result node and try to install it. */
    MVMStaticFrame *sf = tc->cur_frame->static_info;
    MVMDispInlineCacheEntryResolvedGetLexStatic *new_entry = MVM_fixed_size_alloc(tc,
            tc->instance->fsa, sizeof(MVMDispInlineCacheEntryResolvedGetLexStatic));
    new_entry->base.run_getlexstatic = getlexstatic_resolved;
    MVM_ASSIGN_REF(tc, &(sf->common.header), new_entry->result, result);
    try_update_cache_entry(tc, entry_ptr, &unlinked_getlexstatic, &(new_entry->base));

    return result;
}

/* Once resolved, just hand back the result. */
static MVMObject * getlexstatic_resolved(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *name) {
    MVMDispInlineCacheEntryResolvedGetLexStatic *resolved =
        (MVMDispInlineCacheEntryResolvedGetLexStatic *)*entry_ptr;
    MVM_ASSERT_NOT_FROMSPACE(tc, resolved->result);
    return resolved->result;
}

/**
 * Inline caching of dispatch_*
 **/

static void dispatch_initial(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *cs, MVMuint16 *arg_indices, MVMuint32 bytecode_offset);

static MVMDispInlineCacheEntry unlinked_dispatch = { .run_dispatch = dispatch_initial };

static void dispatch_initial(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *callsite, MVMuint16 *arg_indices, MVMuint32 bytecode_offset) {
    /* Resolve the dispatcher. */
    MVMDispDefinition *disp = MVM_disp_registry_find(tc, id);

    /* Form argument capture object for dispatcher to process. */
    MVMArgs capture_arg_info = {
        .callsite = callsite,
        .source = tc->cur_frame->work,
        .map = arg_indices
    };
    MVMObject *capture = MVM_capture_from_args(tc, capture_arg_info);

    /* Run the dispatcher. */
    MVM_disp_program_run_dispatch(tc, disp, capture, entry_ptr, seen);
}

static void dispatch_monomorphic(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *callsite, MVMuint16 *arg_indices, MVMuint32 bytecode_offset) {
    MVMDispProgram *dp = ((MVMDispInlineCacheEntryMonomorphicDispatch *)seen)->dp;
    MVMCallStackDispatchRun *record = MVM_callstack_allocate_dispatch_run(tc,
            dp->num_temporaries);
    record->arg_info.callsite = callsite;
    record->arg_info.source = tc->cur_frame->work;
    record->arg_info.map = arg_indices;
    if (!MVM_disp_program_run(tc, dp, record)) {
        /* Dispatch program failed. Remove this record and then record a new
         * dispatch program. */
        MVM_callstack_unwind_dispatch_run(tc);
        dispatch_initial(tc, entry_ptr, seen, id, callsite, arg_indices, bytecode_offset);
    }
    else {
        if (MVM_spesh_log_is_logging(tc))
            MVM_spesh_log_dispatch_resolution(tc, bytecode_offset, 0);
    }
}

static void dispatch_polymorphic(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *callsite, MVMuint16 *arg_indices, MVMuint32 bytecode_offset) {
    /* Set up dispatch run record. */
    MVMDispInlineCacheEntryPolymorphicDispatch *disp =
            (MVMDispInlineCacheEntryPolymorphicDispatch *)seen;
    MVMCallStackDispatchRun *record = MVM_callstack_allocate_dispatch_run(tc,
            disp->max_temporaries);
    record->arg_info.callsite = callsite;
    record->arg_info.source = tc->cur_frame->work;
    record->arg_info.map = arg_indices;

    /* Go through the dispatch programs, taking the first one that works. */
    MVMuint32 i;
    for (i = 0; i < disp->num_dps; i++)
        if (MVM_disp_program_run(tc, disp->dps[i], record)) {
            if (MVM_spesh_log_is_logging(tc))
                MVM_spesh_log_dispatch_resolution(tc, bytecode_offset, i);
            return;
        }

    /* If we reach here, then no program matched; run the dispatch program
     * for another go at it. */
    MVM_callstack_unwind_dispatch_run(tc);
    dispatch_initial(tc, entry_ptr, seen, id, callsite, arg_indices, bytecode_offset);
}

/* Transition a callsite such that it incorporates a newly record dispatch
 * program. */
static void set_max_temps(MVMDispInlineCacheEntryPolymorphicDispatch *entry) {
    MVMuint32 i;
    MVMuint32 max = 0;
    for (i = 0; i < entry->num_dps; i++)
        if (entry->dps[i]->num_temporaries > max)
            max = entry->dps[i]->num_temporaries;
    entry->max_temporaries = max;
}
void MVM_disp_inline_cache_transition(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *entry,
        MVMDispProgram *dp) {
    /* Ensure that the entry is current (this is re-checked when we actaully
     * update it, but this ensures we won't dereference a dangling pointer
     * below). */
    if (*entry_ptr != entry)
        return;

    /* Now go by the initial state. */
    if (entry->run_dispatch == dispatch_initial) {
        /* Unlinked -> monomorphic transition. */
        MVMDispInlineCacheEntryMonomorphicDispatch *new_entry = MVM_fixed_size_alloc(tc,
                tc->instance->fsa, sizeof(MVMDispInlineCacheEntryMonomorphicDispatch));
        new_entry->base.run_dispatch = dispatch_monomorphic;
        new_entry->dp = dp;
        // TODO write-barrier
        if (!try_update_cache_entry(tc, entry_ptr, &unlinked_dispatch, &(new_entry->base)))
            MVM_disp_program_destroy(tc, dp);
    }
    else if (entry->run_dispatch == dispatch_monomorphic) {
        /* Monomorphic -> polymorphic transition. */
        MVMDispInlineCacheEntryPolymorphicDispatch *new_entry = MVM_fixed_size_alloc(tc,
                tc->instance->fsa, sizeof(MVMDispInlineCacheEntryPolymorphicDispatch));
        new_entry->base.run_dispatch = dispatch_polymorphic;
        new_entry->num_dps = 2;
        new_entry->dps = MVM_fixed_size_alloc(tc, tc->instance->fsa,
                new_entry->num_dps * sizeof(MVMDispProgram *));
        new_entry->dps[0] = ((MVMDispInlineCacheEntryMonomorphicDispatch *)entry)->dp;
        new_entry->dps[1] = dp;
        set_max_temps(new_entry);
        // TODO write-barrier
        if (!try_update_cache_entry(tc, entry_ptr, entry, &(new_entry->base)))
            MVM_disp_program_destroy(tc, dp);
    }
    else if (entry->run_dispatch == dispatch_polymorphic) {
        /* Polymorphic -> polymorphic transition. */
        MVMDispInlineCacheEntryPolymorphicDispatch *prev_entry =
                (MVMDispInlineCacheEntryPolymorphicDispatch *)entry;
        MVMDispInlineCacheEntryPolymorphicDispatch *new_entry = MVM_fixed_size_alloc(tc,
                tc->instance->fsa, sizeof(MVMDispInlineCacheEntryPolymorphicDispatch));
        new_entry->base.run_dispatch = dispatch_polymorphic;
        new_entry->num_dps = prev_entry->num_dps + 1;
        new_entry->dps = MVM_fixed_size_alloc(tc, tc->instance->fsa,
                new_entry->num_dps * sizeof(MVMDispProgram *));
        memcpy(new_entry->dps, prev_entry->dps, prev_entry->num_dps * sizeof(MVMDispProgram *));
        new_entry->dps[prev_entry->num_dps] = dp;
        set_max_temps(new_entry);
        // TODO write-barrier
        if (!try_update_cache_entry(tc, entry_ptr, entry, &(new_entry->base)))
            MVM_disp_program_destroy(tc, dp);
    }
    else {
        MVM_oops(tc, "unknown transition requested for dispatch inline cache");
    }
}

/**
 * Inline caching general stuff
 **/

static MVMuint32 round_down_to_power_of_two(MVMuint32 v) {
    /* Thanks to http://graphics.stanford.edu/~seander/bithacks.html. */
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v >> 1;
}

static MVMuint32 shift_for_interval(MVMuint32 v) {
    MVMuint32 res = 0;
    while (v >>= 1)
        res++;
    return res;
}

/* Sets up the inline caches for the specified static frame. */
void MVM_disp_inline_cache_setup(MVMThreadContext *tc, MVMStaticFrame *sf) {
    /* Walk the bytecode, looking for instructions that want cache entries.
     * Also look for the minimum byte distance between two such instructions. */
    MVMCompUnit *cu = sf->body.cu;
    MVMuint8 *cur_op = sf->body.bytecode;
    MVMuint8 *end = cur_op + sf->body.bytecode_size;
    typedef struct {
        size_t offset;
        MVMuint16 op;
    } Cacheable;
    MVMuint32 min_byte_interval = sf->body.bytecode_size;
    MVMuint32 last_cacheable = 0;
    MVMuint32 i;
    MVM_VECTOR_DECL(Cacheable, cacheable_ins);
    MVM_VECTOR_INIT(cacheable_ins, sf->body.bytecode_size >> 5);
    while (cur_op < end) {
        /* If the op is cacheable, then collect it. */
        MVMuint16 op = *((MVMuint16 *)cur_op);
        const MVMOpInfo *info = MVM_bytecode_get_validated_op_info(tc, cu, op);
        if (info->uses_cache) {
            Cacheable c;
            c.offset = cur_op - sf->body.bytecode;
            c.op = op;
            MVM_VECTOR_PUSH(cacheable_ins, c);
            if (c.offset - last_cacheable < min_byte_interval)
                min_byte_interval = c.offset - last_cacheable;
            last_cacheable = c.offset;
        }

        /* Step to the next op. */
        cur_op += 2;
        for (i = 0; i < info->num_operands; i++) {
            MVMuint8 flags = info->operands[i];
            MVMuint8 rw    = flags & MVM_operand_rw_mask;
            switch (rw) {
            case MVM_operand_read_reg:
            case MVM_operand_write_reg:
                cur_op += 2;
                break;
            case MVM_operand_read_lex:
            case MVM_operand_write_lex:
                cur_op += 4;
                break;
            case MVM_operand_literal: {
                MVMuint32 type = flags & MVM_operand_type_mask;
                switch (type) {
                case MVM_operand_int8:
                    cur_op += 1;
                    break;
                case MVM_operand_int16:
                case MVM_operand_callsite:
                case MVM_operand_coderef:
                    cur_op += 2;
                    break;
                case MVM_operand_int32:
                case MVM_operand_uint32:
                case MVM_operand_num32:
                case MVM_operand_str:
                case MVM_operand_ins:
                    cur_op += 4;
                    break;
                case MVM_operand_int64:
                case MVM_operand_num64:
                    cur_op += 8;
                    break;
                default:
                    MVM_oops(tc,
                        "Spesh: unknown operand type %d in inline cache", (int)type);
                }
                break;
            }
            default:
                break;
            }
        }
        if (MVM_op_get_mark(op)[1] == 'd') {
            /* Dispatch op, so skip over argument registers. */
            MVMCallsite *cs = cu->body.callsites[*((MVMuint16 *)(cur_op -2))];
            cur_op += cs->flag_count * 2;
        }
    }

    /* Assuming we have some... */
    if (MVM_VECTOR_ELEMS(cacheable_ins)) {
        /* Calculate the size and interval of cache we need and allocate it. */
        MVMuint32 rounded_interval = round_down_to_power_of_two(min_byte_interval);
        MVMuint32 num_entries = 1 + (sf->body.bytecode_size / rounded_interval);
        MVMuint32 bit_shift = shift_for_interval(rounded_interval);
        MVMDispInlineCacheEntry **entries = MVM_calloc(num_entries,
                sizeof(MVMDispInlineCacheEntry *));

        /* Set up unlinked entries for each instruction. */
        for (i = 0; i < MVM_VECTOR_ELEMS(cacheable_ins); i++) {
            MVMuint32 slot = cacheable_ins[i].offset >> bit_shift;
            if (entries[slot]) // Can become an assert
                MVM_panic(1, "Inline cache slot overlap");
            switch (cacheable_ins[i].op) {
                case MVM_OP_getlexstatic_o:
                    entries[slot] = &unlinked_getlexstatic;
                    break;
                case MVM_OP_dispatch_v:
                case MVM_OP_dispatch_i:
                case MVM_OP_dispatch_n:
                case MVM_OP_dispatch_s:
                case MVM_OP_dispatch_o:
                    entries[slot] = &unlinked_dispatch;
                    break;
                default:
                    MVM_oops(tc, "Unimplemented case of inline cache unlinked state");
            }
        }

        /* Install. */
        sf->body.inline_cache.entries = entries;
        sf->body.inline_cache.num_entries = num_entries;
        sf->body.inline_cache.bit_shift = bit_shift;
    }

    MVM_VECTOR_DESTROY(cacheable_ins);
}

/* Cleans up a cache entry. */
void cleanup_entry(MVMThreadContext *tc, MVMDispInlineCacheEntry *entry) {
    if (!entry)
        return;
    else if (entry->run_getlexstatic == getlexstatic_initial) {
        /* Never free initial getlexstatic state. */
    }
    else if (entry->run_getlexstatic == getlexstatic_resolved) {
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                sizeof(MVMDispInlineCacheEntryResolvedGetLexStatic), entry);
    }
    else if (entry->run_dispatch == dispatch_initial) {
        /* Never free initial dispatch state. */
    }
    else if (entry->run_dispatch == dispatch_monomorphic) {
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                sizeof(MVMDispInlineCacheEntryMonomorphicDispatch), entry);
    }
    else if (entry->run_dispatch == dispatch_polymorphic) {
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                sizeof(MVMDispInlineCacheEntryPolymorphicDispatch), entry);
    }
    else {
        MVM_oops(tc, "Unimplemented cleanup_entry case");
    }
}

/* Tries to migrate an inline cache entry to its next state. */
MVMuint32 try_update_cache_entry(MVMThreadContext *tc, MVMDispInlineCacheEntry **target,
        MVMDispInlineCacheEntry *from, MVMDispInlineCacheEntry *to) {
    if (MVM_trycas(target, from, to)) {
        cleanup_entry(tc, from);
        return 1;
    }
    else {
        cleanup_entry(tc, to);
        return 0;
    }
}

/* Given the inline cache entry for a getlexstatic_o instruction, return the
 * object it is resolved to, if it is in a resolved state. */
MVMObject * MVM_disp_inline_cache_get_lex_resolution(MVMThreadContext *tc, MVMStaticFrame *sf,
        MVMuint32 bytecode_offset) {
    MVMDispInlineCache *cache = &(sf->body.inline_cache);
    MVMDispInlineCacheEntry *entry = cache->entries[bytecode_offset >> cache->bit_shift];
    return entry->run_getlexstatic == getlexstatic_resolved
        ? ((MVMDispInlineCacheEntryResolvedGetLexStatic *)entry)->result
        : NULL;
}

/* Given a static frame and a bytecode offset, resolve the inline cache slot. */
MVMuint32 MVM_disp_inline_cache_get_slot(MVMThreadContext *tc, MVMStaticFrame *sf,
        MVMuint32 bytecode_offset) {
    MVMDispInlineCache *cache = &(sf->body.inline_cache);
    return bytecode_offset >> cache->bit_shift;
}

/* GC-mark an inline cache entry. */
void MVM_disp_inline_cache_mark(MVMThreadContext *tc, MVMDispInlineCache *cache,
        MVMGCWorklist *worklist) {
    MVMuint32 i;
    for (i = 0; i < cache->num_entries; i++) {
        MVMDispInlineCacheEntry *entry = cache->entries[i];
        if (entry) {
            if (entry->run_getlexstatic == getlexstatic_initial) {
                /* Nothing to mark. */
            }
            else if (entry->run_getlexstatic == getlexstatic_resolved) {
                MVM_gc_worklist_add(tc, worklist,
                        &(((MVMDispInlineCacheEntryResolvedGetLexStatic *)entry)->result));
            }
            else if (entry->run_dispatch == dispatch_initial) {
                /* Nothing to mark. */
            }
            else if (entry->run_dispatch == dispatch_monomorphic) {
                MVMDispInlineCacheEntryMonomorphicDispatch *mono =
                    (MVMDispInlineCacheEntryMonomorphicDispatch *)entry;
                MVM_disp_program_mark(tc, mono->dp, worklist);
            }
            else if (entry->run_dispatch == dispatch_polymorphic) {
                MVMDispInlineCacheEntryPolymorphicDispatch *poly =
                    (MVMDispInlineCacheEntryPolymorphicDispatch *)entry;
                MVMuint32 i;
                for (i = 0; i < poly->num_dps; i++)
                    MVM_disp_program_mark(tc, poly->dps[i], worklist);
            }
            else {
                MVM_panic(1, "Unimplemented case of inline cache GC marking");
            }
        }
    }
}

/* Clear up the memory associated with an inline cache. */
void MVM_disp_inline_cache_destroy(MVMThreadContext *tc, MVMDispInlineCache *cache) {
    MVMuint32 i;
    for (i = 0; i < cache->num_entries; i++)
        cleanup_entry(tc, cache->entries[i]);
    MVM_free(cache->entries);
}
