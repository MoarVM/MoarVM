#include "moar.h"

#define DUMP_FULL_CACHES 0

static MVMuint32 try_update_cache_entry(MVMThreadContext *tc, MVMDispInlineCacheEntry **target,
        MVMDispInlineCacheEntry *from, MVMDispInlineCacheEntry *to);

/**
 * Inline caching of getlexstatic_o
 **/

static int getlexstatic_initial(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *name, MVMRegister *r);
static int getlexstatic_resolved(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *name, MVMRegister *r);

/* Unlinked node. */
static MVMDispInlineCacheEntry unlinked_getlexstatic = { getlexstatic_initial };

/* Initial unlinked handler. */
static int getlexstatic_initial(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *name, MVMRegister *r) {
    /* Do the lookup. */
    int found = MVM_frame_find_lexical_by_name(tc, name, MVM_reg_obj, r);
    MVMObject *result = found > 0 ? r->o : tc->instance->VMNull;
    // FIXME if the fallback resolver is used we must not cace the VMNull

    /* Set up result node and try to install it. */
    MVMStaticFrame *sf = tc->cur_frame->static_info;
    MVMDispInlineCacheEntryResolvedGetLexStatic *new_entry = MVM_malloc(sizeof(MVMDispInlineCacheEntryResolvedGetLexStatic));
    new_entry->base.run_getlexstatic = getlexstatic_resolved;
    MVM_ASSIGN_REF(tc, &(sf->common.header), new_entry->result, result);
    try_update_cache_entry(tc, entry_ptr, &unlinked_getlexstatic, &(new_entry->base));

    return found;
}

/* Once resolved, just hand back the result. */
static int getlexstatic_resolved(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *name, MVMRegister *r) {
    MVMDispInlineCacheEntryResolvedGetLexStatic *resolved =
        (MVMDispInlineCacheEntryResolvedGetLexStatic *)*entry_ptr;
    MVM_ASSERT_NOT_FROMSPACE(tc, resolved->result);
    r->o = resolved->result;
    return 1;
}

/**
 * Inline caching of dispatch_*
 **/

static void dispatch_initial(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *cs, MVMuint16 *arg_indices,
        MVMRegister *source, MVMStaticFrame *sf, MVMuint32 bytecode_offset);

static void dispatch_initial_flattening(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *cs, MVMuint16 *arg_indices,
        MVMRegister *source, MVMStaticFrame *sf, MVMuint32 bytecode_offset);

static MVMDispInlineCacheEntry unlinked_dispatch = { .run_dispatch = dispatch_initial };

static MVMDispInlineCacheEntry unlinked_dispatch_flattening =
    { .run_dispatch = dispatch_initial_flattening };

static void dispatch_initial(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *callsite, MVMuint16 *arg_indices,
        MVMRegister *source, MVMStaticFrame *sf, MVMuint32 bytecode_offset) {
    /* Resolve the dispatcher. */
    MVMDispDefinition *disp = MVM_disp_registry_find(tc, id);

    /* Form argument info and run the dispatcher. */
    MVMArgs arg_info = {
        .callsite = callsite,
        .source = source,
        .map = arg_indices
    };
    MVM_disp_program_run_dispatch(tc, disp, arg_info, entry_ptr, seen, sf);
}

static void dispatch_initial_flattening(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *cs, MVMuint16 *arg_indices,
        MVMRegister *source, MVMStaticFrame *sf, MVMuint32 bytecode_offset) {
    /* Resolve the dispatcher. */
    MVMDispDefinition *disp = MVM_disp_registry_find(tc, id);

    /* Perform flattening of arguments, then run the dispatcher. */
    MVMCallStackFlattening *record = MVM_args_perform_flattening(tc, cs,
            source, arg_indices);
    MVM_disp_program_run_dispatch(tc, disp, record->arg_info, entry_ptr, seen, sf);
}

static void dispatch_monomorphic(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *callsite, MVMuint16 *arg_indices,
        MVMRegister *source, MVMStaticFrame *sf, MVMuint32 bytecode_offset) {
    MVMint32 cid = MVM_spesh_log_is_logging(tc) ? tc->cur_frame->spesh_correlation_id : 0;
    MVMDispProgram *dp = ((MVMDispInlineCacheEntryMonomorphicDispatch *)seen)->dp;
    MVMCallStackDispatchRun *record = MVM_callstack_allocate_dispatch_run(tc,
            dp->num_temporaries);
    record->arg_info.callsite = callsite;
    record->arg_info.source = source;
    record->arg_info.map = arg_indices;
    MVMint64 outcome;
    MVMROOT2(tc, id, sf, {
        outcome = MVM_disp_program_run(tc, dp, record, cid, bytecode_offset, 0);
    });
    if (!outcome) {
        /* Dispatch program failed. Remove this record and then record a new
         * dispatch program. */
        MVM_callstack_unwind_failed_dispatch_run(tc);
        dispatch_initial(tc, entry_ptr, seen, id, callsite, arg_indices, source, sf,
                bytecode_offset);
    }
}

static void dispatch_monomorphic_flattening(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *callsite, MVMuint16 *arg_indices,
        MVMRegister *source, MVMStaticFrame *sf, MVMuint32 bytecode_offset) {
    MVMint32 cid = MVM_spesh_log_is_logging(tc) ? tc->cur_frame->spesh_correlation_id : 0;
    /* First, perform flattening of the arguments. */
    MVMCallStackFlattening *flat_record = MVM_args_perform_flattening(tc, callsite,
            source, arg_indices);

    /* In the best case, the resulting callsite matches, so we're maybe
     * really monomorphic. */
    MVMDispInlineCacheEntryMonomorphicDispatchFlattening *entry =
            (MVMDispInlineCacheEntryMonomorphicDispatchFlattening *)seen;
    if (flat_record->arg_info.callsite == entry->flattened_cs) {
        MVMDispProgram *dp = entry->dp;
        MVMCallStackDispatchRun *record = MVM_callstack_allocate_dispatch_run(tc,
                dp->num_temporaries);
        record->arg_info = flat_record->arg_info;
        MVMint64 outcome;
        MVMROOT2(tc, id, sf, {
            outcome = MVM_disp_program_run(tc, dp, record, cid, bytecode_offset, 0);
        });
        if (outcome) {
            /* It matches, so we're ready to continue. */
            return;
        }
        else {
            /* Dispatch program failed. Remove this record. */
            MVM_callstack_unwind_failed_dispatch_run(tc);
        }
    }

    /* If we get here, then either the callsite didn't match or the dispatch
     * program didn't match, so we need to run the dispatch callback again. */
    MVMDispDefinition *disp = MVM_disp_registry_find(tc, id);
    MVM_disp_program_run_dispatch(tc, disp, flat_record->arg_info, entry_ptr, seen,
            sf);
}

static void dispatch_polymorphic(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *callsite, MVMuint16 *arg_indices,
        MVMRegister *source, MVMStaticFrame *sf, MVMuint32 bytecode_offset) {
    MVMint32 cid = MVM_spesh_log_is_logging(tc) ? tc->cur_frame->spesh_correlation_id : 0;
    /* Set up dispatch run record. */
    MVMDispInlineCacheEntryPolymorphicDispatch *entry =
            (MVMDispInlineCacheEntryPolymorphicDispatch *)seen;
    MVMCallStackDispatchRun *record = MVM_callstack_allocate_dispatch_run(tc,
            entry->max_temporaries);
    record->arg_info.callsite = callsite;
    record->arg_info.source = source;
    record->arg_info.map = arg_indices;

    /* Go through the dispatch programs, starting with the most recent one
     * (which is most likely to be the megamorphic handler) and taking the
     * first one that works. */
    MVMint32 i;
    for (i = entry->num_dps - 1; i >= 0; i--) {
        MVMint64 outcome;
        MVMROOT2(tc, id, sf, {
            outcome = MVM_disp_program_run(tc, entry->dps[i], record, cid, bytecode_offset, i);
        });
        if (outcome)
            return;
    }

    /* If we reach here, then no program matched; run the dispatch program
     * for another go at it. */
    MVM_callstack_unwind_failed_dispatch_run(tc);
    dispatch_initial(tc, entry_ptr, seen, id, callsite, arg_indices, source, sf,
            bytecode_offset);
}

static void dispatch_polymorphic_flattening(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *seen,
        MVMString *id, MVMCallsite *callsite, MVMuint16 *arg_indices,
        MVMRegister *source, MVMStaticFrame *sf, MVMuint32 bytecode_offset) {
    MVMint32 cid = MVM_spesh_log_is_logging(tc) ? tc->cur_frame->spesh_correlation_id : 0;
    /* First, perform flattening of the arguments. */
    MVMCallStackFlattening *flat_record = MVM_args_perform_flattening(tc, callsite,
            source, arg_indices);

    /* Set up dispatch run record. */
    MVMDispInlineCacheEntryPolymorphicDispatchFlattening *entry =
            (MVMDispInlineCacheEntryPolymorphicDispatchFlattening *)seen;
    MVMCallStackDispatchRun *record = MVM_callstack_allocate_dispatch_run(tc,
            entry->max_temporaries);
    record->arg_info = flat_record->arg_info;

    /* Go through the callsite and dispatch program pairs, taking the first one
     * that works. */
    MVMint32 i;
    for (i = entry->num_dps - 1; i >= 0; i--) {
        if (flat_record->arg_info.callsite == entry->flattened_css[i]) {
            MVMint64 outcome;
            MVMROOT2(tc, id, sf, {
                outcome = MVM_disp_program_run(tc, entry->dps[i], record, cid, bytecode_offset, i);
            });
            if (outcome)
                return;
        }
    }

    /* If we get here, then none of the callsite/dispatch program pairings
     * matched, so we need to run the dispatch callback again. */
    MVM_callstack_unwind_failed_dispatch_run(tc);
    MVMDispDefinition *disp = MVM_disp_registry_find(tc, id);
    MVM_disp_program_run_dispatch(tc, disp, flat_record->arg_info, entry_ptr, seen,
            sf);
}

MVMuint32 MVM_disp_inline_cache_get_kind(MVMThreadContext *tc,
        MVMDispInlineCacheEntry *entry) {
    MVMint32 kind = MVM_disp_inline_cache_try_get_kind(tc, entry);
    if (kind < 0)
        MVM_oops(tc, "Unknown handler in inline cache entry");
    return (MVMuint32)kind;
}

MVMint32 MVM_disp_inline_cache_try_get_kind(MVMThreadContext *tc,
        MVMDispInlineCacheEntry *entry) {
    if (!entry)
        return -1;
    if (entry->run_dispatch == dispatch_initial) {
        return MVM_INLINE_CACHE_KIND_INITIAL;
    }
    else if (entry->run_dispatch == dispatch_initial_flattening) {
        return MVM_INLINE_CACHE_KIND_INITIAL_FLATTENING;
    }
    else if (entry->run_dispatch == dispatch_monomorphic) {
        return MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH;
    }
    else if (entry->run_dispatch == dispatch_monomorphic_flattening) {
        return MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH_FLATTENING;
    }
    else if (entry->run_dispatch == dispatch_polymorphic) {
        return MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH;
    }
    else if (entry->run_dispatch == dispatch_polymorphic_flattening) {
        return MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH_FLATTENING;
    }
    return -1;
}
/* Transition a callsite such that it incorporates a newly record dispatch
 * program. Returns a true value if it is transitioned successfully. */
static void set_max_temps(MVMDispInlineCacheEntryPolymorphicDispatch *entry) {
    MVMuint32 i;
    MVMuint32 max = 0;
    for (i = 0; i < entry->num_dps; i++)
        if (entry->dps[i]->num_temporaries > max)
            max = entry->dps[i]->num_temporaries;
    entry->max_temporaries = max;
}
static void set_max_temps_flattening(MVMDispInlineCacheEntryPolymorphicDispatchFlattening *entry) {
    MVMuint32 i;
    MVMuint32 max = 0;
    for (i = 0; i < entry->num_dps; i++)
        if (entry->dps[i]->num_temporaries > max)
            max = entry->dps[i]->num_temporaries;
    entry->max_temporaries = max;
}
static void gc_barrier_program(MVMThreadContext *tc, MVMStaticFrame *root,
        MVMDispProgram *dp) {
    MVMuint32 i;
    for (i = 0; i < dp->num_gc_constants; i++)
        MVM_gc_write_barrier(tc, (MVMCollectable *)root, dp->gc_constants[i]);
}
MVMuint32 MVM_disp_inline_cache_transition(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMDispInlineCacheEntry *entry,
        MVMStaticFrame *root, MVMDispDefinition *initial_disp,
        MVMCallsite *initial_cs, MVMDispProgram *dp) {
    /* Ensure that the entry is current (this is re-checked when we actaully
     * update it, but this ensures we won't dereference a dangling pointer
     * below; safe as we know there's not a GC safepoint between here and
     * the end of the logic below this point. */
    if (*entry_ptr != entry)
        return 0;

    MVMuint32 kind = MVM_disp_inline_cache_get_kind(tc, entry);

    /* Now go by the initial state. */
    if (kind == MVM_INLINE_CACHE_KIND_INITIAL) {
        /* Unlinked -> monomorphic transition. */
        MVMDispInlineCacheEntryMonomorphicDispatch *new_entry = MVM_malloc(sizeof(MVMDispInlineCacheEntryMonomorphicDispatch));
        new_entry->base.run_dispatch = dispatch_monomorphic;
        new_entry->dp = dp;
        gc_barrier_program(tc, root, dp);
        return try_update_cache_entry(tc, entry_ptr, &unlinked_dispatch, &(new_entry->base));
    }
    else if (kind == MVM_INLINE_CACHE_KIND_INITIAL_FLATTENING) {
        /* Unlinked flattening -> monomorphic flattening transition. Since we shall
         * retain the callsite to assert against, we force interning of it. */
        MVMDispInlineCacheEntryMonomorphicDispatchFlattening *new_entry = MVM_malloc(sizeof(MVMDispInlineCacheEntryMonomorphicDispatchFlattening));
        new_entry->base.run_dispatch = dispatch_monomorphic_flattening;
        if (!initial_cs->is_interned)
            MVM_callsite_intern(tc, &initial_cs, 1, 0);
        new_entry->flattened_cs = initial_cs;
        new_entry->dp = dp;
        gc_barrier_program(tc, root, dp);
        return try_update_cache_entry(tc, entry_ptr, &unlinked_dispatch_flattening,
                &(new_entry->base));
    }
    else if (kind == MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH) {
        /* Monomorphic -> polymorphic transition. */
        MVMDispInlineCacheEntryPolymorphicDispatch *new_entry = MVM_malloc(sizeof(MVMDispInlineCacheEntryPolymorphicDispatch));
        new_entry->base.run_dispatch = dispatch_polymorphic;
        new_entry->num_dps = 2;
        new_entry->dps = MVM_malloc(new_entry->num_dps * sizeof(MVMDispProgram *));
        new_entry->dps[0] = ((MVMDispInlineCacheEntryMonomorphicDispatch *)entry)->dp;
        new_entry->dps[1] = dp;
        set_max_temps(new_entry);
        gc_barrier_program(tc, root, dp);
        return try_update_cache_entry(tc, entry_ptr, entry, &(new_entry->base));
    }
    else if (kind == MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH_FLATTENING) {
        /* Monomorphic flattening -> polymorphic flattening transition. */
        MVMDispInlineCacheEntryPolymorphicDispatchFlattening *new_entry = MVM_malloc(sizeof(MVMDispInlineCacheEntryPolymorphicDispatchFlattening));
        new_entry->base.run_dispatch = dispatch_polymorphic_flattening;
        new_entry->num_dps = 2;

        new_entry->flattened_css = MVM_malloc(new_entry->num_dps * sizeof(MVMCallsite *));
        if (!initial_cs->is_interned)
            MVM_callsite_intern(tc, &initial_cs, 1, 0);
        new_entry->flattened_css[0] = ((MVMDispInlineCacheEntryMonomorphicDispatchFlattening *)entry)
                ->flattened_cs;
        new_entry->flattened_css[1] = initial_cs;

        new_entry->dps = MVM_malloc(new_entry->num_dps * sizeof(MVMDispProgram *));
        new_entry->dps[0] = ((MVMDispInlineCacheEntryMonomorphicDispatchFlattening *)entry)->dp;
        new_entry->dps[1] = dp;

        set_max_temps_flattening(new_entry);
        gc_barrier_program(tc, root, dp);
        return try_update_cache_entry(tc, entry_ptr, entry, &(new_entry->base));
    }
    else if (kind == MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH) {
        /* Polymorphic -> polymorphic transition. */
        MVMDispInlineCacheEntryPolymorphicDispatch *prev_entry =
                (MVMDispInlineCacheEntryPolymorphicDispatch *)entry;
        if (prev_entry->num_dps == MVM_INLINE_CACHE_MAX_POLY) {
#if DUMP_FULL_CACHES
            char *disp_id = MVM_string_utf8_encode_C_string(tc, initial_disp->id);
            fprintf(stderr, "\n\nFull polymorphic dispatch inline cache for %s at:\n",
                    disp_id);
            MVM_dump_backtrace(tc);
            MVM_free(disp_id);
#endif
            return 0;
        }
        MVMDispInlineCacheEntryPolymorphicDispatch *new_entry = MVM_malloc(sizeof(MVMDispInlineCacheEntryPolymorphicDispatch));
        new_entry->base.run_dispatch = dispatch_polymorphic;
        new_entry->num_dps = prev_entry->num_dps + 1;
        new_entry->dps = MVM_malloc(new_entry->num_dps * sizeof(MVMDispProgram *));
        memcpy(new_entry->dps, prev_entry->dps, prev_entry->num_dps * sizeof(MVMDispProgram *));
        new_entry->dps[prev_entry->num_dps] = dp;
        set_max_temps(new_entry);
        gc_barrier_program(tc, root, dp);
        return try_update_cache_entry(tc, entry_ptr, entry, &(new_entry->base));
    }
    else if (kind == MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH_FLATTENING) {
        /* Polymorphic flattening -> polymorphic flattening transition. */
        MVMDispInlineCacheEntryPolymorphicDispatchFlattening *prev_entry =
                (MVMDispInlineCacheEntryPolymorphicDispatchFlattening *)entry;
        if (prev_entry->num_dps == MVM_INLINE_CACHE_MAX_POLY) {
#if DUMP_FULL_CACHES
            char *disp_id = MVM_string_utf8_encode_C_string(tc, initial_disp->id);
            fprintf(stderr, "\n\nFull polymorphic flattening dispatch inline cache for %s at:\n",
                    disp_id);
            MVM_dump_backtrace(tc);
            MVM_free(disp_id);
#endif
            return 0;
        }
        MVMDispInlineCacheEntryPolymorphicDispatchFlattening *new_entry = MVM_malloc(sizeof(MVMDispInlineCacheEntryPolymorphicDispatchFlattening));
        new_entry->base.run_dispatch = dispatch_polymorphic_flattening;
        new_entry->num_dps = prev_entry->num_dps + 1;

        new_entry->flattened_css = MVM_malloc(new_entry->num_dps * sizeof(MVMCallsite *));
        memcpy(new_entry->flattened_css, prev_entry->flattened_css,
                prev_entry->num_dps * sizeof(MVMCallsite * *));
        if (!initial_cs->is_interned)
            MVM_callsite_intern(tc, &initial_cs, 1, 0);
        new_entry->flattened_css[prev_entry->num_dps] = initial_cs;

        new_entry->dps = MVM_malloc(new_entry->num_dps * sizeof(MVMDispProgram *));
        memcpy(new_entry->dps, prev_entry->dps, prev_entry->num_dps * sizeof(MVMDispProgram *));
        new_entry->dps[prev_entry->num_dps] = dp;

        set_max_temps_flattening(new_entry);
        gc_barrier_program(tc, root, dp);
        return try_update_cache_entry(tc, entry_ptr, entry, &(new_entry->base));
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
        MVMuint16 callsite_idx;
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
        MVMint32 save_callsite_to = -1;
        if (info->uses_cache) {
            Cacheable c;
            c.offset = cur_op - sf->body.bytecode;
            c.op = op;
            save_callsite_to = MVM_VECTOR_ELEMS(cacheable_ins);
            MVM_VECTOR_PUSH(cacheable_ins, c);
            if (c.offset - last_cacheable < min_byte_interval)
                min_byte_interval = c.offset - last_cacheable;
            last_cacheable = c.offset;
        }

        /* Step to the next op, saving any spotted callsite if needed. */
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
                case MVM_operand_coderef:
                    cur_op += 2;
                    break;
                case MVM_operand_callsite:
                    if (save_callsite_to >= 0)
                        cacheable_ins[save_callsite_to].callsite_idx = *((MVMuint16 *)cur_op);
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
                case MVM_OP_dispatch_u:
                case MVM_OP_dispatch_n:
                case MVM_OP_dispatch_s:
                case MVM_OP_dispatch_o: {
                    MVMCallsite *cs = sf->body.cu->body.callsites[cacheable_ins[i].callsite_idx];
                    entries[slot] = cs->has_flattening
                        ? &unlinked_dispatch_flattening
                        : &unlinked_dispatch;
                    break;
                }
                case MVM_OP_assertparamcheck:
                    /* When we have a dispatch resumption due to a bind failure
                     * we need the inline cache to hang around somewhere, and
                     * since this is typically because we're going to the next
                     * multi candidate upon failure, it'll likely be lightly
                     * morphic on the assertparamcheck op itself. */
                    entries[slot] = &unlinked_dispatch;
                    break;
                case MVM_OP_bindcomplete:
                    /* The bindcomplete op will usually do nothing, but in the
                     * case we need to do a dispatch resumption upon bind success,
                     * this is where we'll hang the dispatch program. */
                    entries[slot] = &unlinked_dispatch;
                    break;
                case MVM_OP_istype:
                    /* The istype op most often looks at the cache and gives a
                     * result based on that, but may fall back to needing to
                     * do method calls. In that case, we handle it like a
                     * dispatch op. */
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
static void cleanup_entry(MVMThreadContext *tc, MVMDispInlineCacheEntry *entry, MVMuint8 destroy_dps) {
    if (!entry)
        return;
    else if (entry->run_getlexstatic == getlexstatic_initial) {
        /* Never free initial getlexstatic state. */
    }
    else if (entry->run_getlexstatic == getlexstatic_resolved) {
        MVM_free_at_safepoint(tc, entry);
    }
    else if (entry->run_dispatch == dispatch_initial ||
            entry->run_dispatch == dispatch_initial_flattening) {
        /* Never free initial dispatch state. */
    }
    else if (entry->run_dispatch == dispatch_monomorphic) {
        if (destroy_dps)
            MVM_disp_program_destroy(tc, ((MVMDispInlineCacheEntryMonomorphicDispatch *)entry)->dp);
        MVM_free_at_safepoint(tc, entry);
    }
    else if (entry->run_dispatch == dispatch_monomorphic_flattening) {
        if (destroy_dps)
            MVM_disp_program_destroy(tc, ((MVMDispInlineCacheEntryMonomorphicDispatchFlattening *)entry)->dp);
        MVM_free_at_safepoint(tc, entry);
    }
    else if (entry->run_dispatch == dispatch_polymorphic) {
        MVMuint32 num_dps = ((MVMDispInlineCacheEntryPolymorphicDispatch *)entry)->num_dps;
        MVMuint32 dpi;
        if (destroy_dps)
            for (dpi = 0; dpi < num_dps; dpi++)
                MVM_disp_program_destroy(tc, ((MVMDispInlineCacheEntryPolymorphicDispatch *)entry)->dps[dpi]);
        MVM_free_at_safepoint(tc, ((MVMDispInlineCacheEntryPolymorphicDispatch *)entry)->dps);
        MVM_free_at_safepoint(tc, entry);
    }
    else if (entry->run_dispatch == dispatch_polymorphic_flattening) {
        MVMuint32 num_dps = ((MVMDispInlineCacheEntryPolymorphicDispatchFlattening *)entry)->num_dps;
        MVMuint32 dpi;
        if (destroy_dps)
            for (dpi = 0; dpi < num_dps; dpi++)
                MVM_disp_program_destroy(tc, ((MVMDispInlineCacheEntryPolymorphicDispatchFlattening *)entry)->dps[dpi]);
        MVM_free_at_safepoint(tc, ((MVMDispInlineCacheEntryPolymorphicDispatchFlattening *)entry)->flattened_css);
        MVM_free_at_safepoint(tc, ((MVMDispInlineCacheEntryPolymorphicDispatchFlattening *)entry)->dps);
        MVM_free_at_safepoint(tc, entry);
    }
    else {
        MVM_oops(tc, "Unimplemented cleanup_entry case");
    }
}

/* Tries to migrate an inline cache entry to its next state. */
MVMuint32 try_update_cache_entry(MVMThreadContext *tc, MVMDispInlineCacheEntry **target,
        MVMDispInlineCacheEntry *from, MVMDispInlineCacheEntry *to) {
    if (MVM_trycas(target, from, to)) {
        cleanup_entry(tc, from, 0);
        return 1;
    }
    else {
        cleanup_entry(tc, to, 0);
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
            else if (entry->run_dispatch == dispatch_initial ||
                    entry->run_dispatch == dispatch_initial_flattening) {
                /* Nothing to mark. */
            }
            else if (entry->run_dispatch == dispatch_monomorphic) {
                MVMDispInlineCacheEntryMonomorphicDispatch *mono =
                    (MVMDispInlineCacheEntryMonomorphicDispatch *)entry;
                MVM_disp_program_mark(tc, mono->dp, worklist, NULL);
            }
            else if (entry->run_dispatch == dispatch_monomorphic_flattening) {
                MVMDispInlineCacheEntryMonomorphicDispatchFlattening *mono =
                    (MVMDispInlineCacheEntryMonomorphicDispatchFlattening *)entry;
                MVM_disp_program_mark(tc, mono->dp, worklist, NULL);
            }
            else if (entry->run_dispatch == dispatch_polymorphic) {
                MVMDispInlineCacheEntryPolymorphicDispatch *poly =
                    (MVMDispInlineCacheEntryPolymorphicDispatch *)entry;
                MVMuint32 i;
                for (i = 0; i < poly->num_dps; i++)
                    MVM_disp_program_mark(tc, poly->dps[i], worklist, NULL);
            }
            else if (entry->run_dispatch == dispatch_polymorphic_flattening) {
                MVMDispInlineCacheEntryPolymorphicDispatchFlattening *poly =
                    (MVMDispInlineCacheEntryPolymorphicDispatchFlattening *)entry;
                MVMuint32 i;
                for (i = 0; i < poly->num_dps; i++)
                    MVM_disp_program_mark(tc, poly->dps[i], worklist, NULL);
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
        cleanup_entry(tc, cache->entries[i], 1);
    MVM_free(cache->entries);
}
