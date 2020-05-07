#include "moar.h"

void try_update_cache_entry(MVMThreadContext *tc, MVMDispInlineCacheEntry **target,
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
        MVMDispInlineCacheEntry **entry_ptr, MVMString *id,
        MVMCallsite *cs, MVMuint16 *arg_indices);

static MVMDispInlineCacheEntry unlinked_dispatch = { .run_dispatch = dispatch_initial };

static void dispatch_initial(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *id,
        MVMCallsite *callsite, MVMuint16 *arg_indices) {
    /* Resolve the dispatcher. */
    MVMDispDefinition *disp = MVM_disp_registry_find(tc, id);

    /* Form argument capture object for dispatcher to process. */
    MVMArgs capture_arg_info = {
        .callsite = callsite,
        .source = tc->cur_frame->work,
        .map = arg_indices
    };
    MVMObject *capture = MVM_capture_from_args(tc, capture_arg_info);

    /* Run the dispatcher. TODO this will actually set up a callstack entry
     * about the ongoing dispatcher, and to mark the args. Otherwise, it will
     * not work for user-defiend ones. */
    MVMCallsite *disp_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_INV_ARG);
    MVMRegister r = { .o = capture };
    MVMArgs dispatch_args = {
        .callsite = disp_callsite,
        .source = &r,
        .map = MVM_args_identity_map(tc, disp_callsite)
    };
    MVMObject *dispatch = disp->dispatch;
    if (REPR(dispatch)->ID == MVM_REPR_ID_MVMCFunction) {
        ((MVMCFunction *)dispatch)->body.func(tc, dispatch_args);
    }
    else {
        MVM_panic(1, "dispatch callback only supported as a MVMCFunction");
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
    else {
        MVM_oops(tc, "Unimplemented cleanup_entry case");
    }
}

/* Tries to migrate an inline cache entry to its next state. */
void try_update_cache_entry(MVMThreadContext *tc, MVMDispInlineCacheEntry **target,
        MVMDispInlineCacheEntry *from, MVMDispInlineCacheEntry *to) {
    if (MVM_trycas(target, from, to))
        cleanup_entry(tc, from);
    else
        cleanup_entry(tc, to);
}

/* GC-mark an inline cache entry. */
void MVM_disp_inline_cache_mark(MVMThreadContext *tc, MVMDispInlineCache *cache,
        MVMGCWorklist *worklist) {
    MVMuint32 i;
    for (i = 0; i < cache->num_entries; i++) {
        MVMDispInlineCacheEntry *entry = cache->entries[i];
        if (entry) {
            if (entry->run_getlexstatic == getlexstatic_resolved) {
                MVM_gc_worklist_add(tc, worklist,
                        &(((MVMDispInlineCacheEntryResolvedGetLexStatic *)entry)->result));
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
