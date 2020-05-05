#include "moar.h"

/* The spesh plugin mechanism allows for extending spesh to be able to reason
 * about high-level language semantics that would be challenging to implement
 * inside of the specializer, perhaps because we don't communicate sufficient
 * information for it to do what is required. */

/* A limit to stop us ending up with an oddly large number of guards. This
 * usually indicates a plugin bug; there's also an option to turn on a log
 * of stack traces where we end up with more than this. */
#define MVM_SPESH_GUARD_LIMIT   1000
#define MVM_SPESH_GUARD_DEBUG   0

/* Registers a new spesh plugin. */
void MVM_spesh_plugin_register(MVMThreadContext *tc, MVMString *language,
        MVMString *name, MVMObject *plugin) {
    MVMHLLConfig *hll = MVM_hll_get_config_for(tc, language);
    uv_mutex_lock(&tc->instance->mutex_hllconfigs);
    if (!hll->spesh_plugins) {
        MVMROOT2(tc, name, plugin, {
            hll->spesh_plugins = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
        });
    }
    MVM_repr_bind_key_o(tc, hll->spesh_plugins, name, plugin);
    uv_mutex_unlock(&tc->instance->mutex_hllconfigs);
}

/* Gets the spesh plugin state for the specified frame. */
static MVMSpeshPluginState * get_plugin_state(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMStaticFrameSpesh *sfs = sf->body.spesh;
    if (!sfs)
        MVM_panic(1, "Unexpectedly missing specialization state for static frame");
    return sfs->body.plugin_state;
}

/* Gets the guard set for a particular position. */
static MVMSpeshPluginGuardSet * guard_set_for_position(MVMThreadContext *tc,
        MVMuint32 cur_position, MVMSpeshPluginState *ps) {
    if (ps) {
        MVMint32 l = 0;
        MVMint32 r = ps->body.num_positions - 1;
        while (l <= r) {
            MVMint32 m = l + (r - l) / 2;
            MVMuint32 test = ps->body.positions[m].bytecode_position;
            if (test == cur_position) {
                return ps->body.positions[m].guard_set;
            }
            if (test < cur_position)
                l = m + 1;
            else
                r = m - 1;
        }
    }
    return NULL;
}

/* Looks through a guard set and returns any result that matches. */
static MVMObject * evaluate_guards(MVMThreadContext *tc, MVMSpeshPluginGuardSet *gs,
        MVMCallsite *callsite, MVMuint16 *guard_offset) {
    MVMuint32 pos = 0;
    MVMuint32 end = gs->num_guards;
    MVMRegister *args = tc->cur_frame->args;
    MVMuint32 arg_end = callsite->flag_count;
    MVMObject *collected_objects = tc->instance->VMNull;
    while (pos < end) {
        MVMuint16 kind = gs->guards[pos].kind;
        if (kind == MVM_SPESH_PLUGIN_GUARD_RESULT) {
            *guard_offset = pos;
            return gs->guards[pos].u.result;
        }
        else {
            MVMuint16 test_idx = gs->guards[pos].test_idx;
            MVMObject *test = test_idx < arg_end
                    ? args[test_idx].o
                    : MVM_repr_at_pos_o(tc, collected_objects, test_idx - arg_end);
            MVMuint32 outcome;
            switch (kind) {
                case MVM_SPESH_PLUGIN_GUARD_OBJ:
                    outcome = test == gs->guards[pos].u.object;
                    break;
                case MVM_SPESH_PLUGIN_GUARD_NOTOBJ:
                    outcome = test != gs->guards[pos].u.object;
                    break;
                case MVM_SPESH_PLUGIN_GUARD_TYPE:
                    outcome = STABLE(test) == gs->guards[pos].u.type;
                    break;
                case MVM_SPESH_PLUGIN_GUARD_CONC:
                    outcome = IS_CONCRETE(test);
                    break;
                case MVM_SPESH_PLUGIN_GUARD_TYPEOBJ:
                    outcome = !IS_CONCRETE(test);
                    break;
                case MVM_SPESH_PLUGIN_GUARD_GETATTR:
                    if (MVM_is_null(tc, collected_objects)) {
                        MVMROOT(tc, test, {
                            collected_objects = MVM_repr_alloc_init(tc,
                                tc->instance->boot_types.BOOTArray);
                        });
                    }
                    MVMROOT(tc, collected_objects, {
                        MVMObject *attr = MVM_repr_get_attr_o(tc, test,
                                gs->guards[pos].u.attr.class_handle, gs->guards[pos].u.attr.name, MVM_NO_HINT);
                        MVM_repr_push_o(tc, collected_objects, attr);
                    });
                    outcome = 1;
                    break;
                default:
                    MVM_panic(1, "Guard kind unrecognized in spesh plugin guard set");
            }
            if (outcome) {
                pos += 1;
            }
            else {
                pos += gs->guards[pos].skip_on_fail;
                if (!MVM_is_null(tc, collected_objects))
                    MVM_repr_pos_set_elems(tc, collected_objects, 0);
            }
        }
    }
    return NULL;
}

/* Tries to resolve a plugin by looking at the guards for the position. */
static MVMObject * resolve_using_guards(MVMThreadContext *tc, MVMuint32 cur_position,
        MVMCallsite *callsite, MVMuint16 *guard_offset, MVMStaticFrame *sf) {
    MVMObject *result;
    MVMSpeshPluginState *ps = get_plugin_state(tc, sf);
    MVMSpeshPluginGuardSet *gs = guard_set_for_position(tc, cur_position, ps);
    MVMROOT(tc, ps, { /* keep ps alive in case another thread replaces it during evaluate_guards */
        result = gs ? evaluate_guards(tc, gs, callsite, guard_offset) : NULL;
    });
    return result;
}

/* Produces an updated guard set with the given resolution result. Returns
 * the base guard set if we already having a matching guard (which means two
 * threads raced to do this resolution). */
static MVMint32 already_have_guard(MVMThreadContext *tc, MVMSpeshPluginGuardSet *base_guards) {
    return 0;
}

static MVMSpeshPluginGuardSet * copy_guard_set(MVMThreadContext *tc, MVMSpeshPluginGuardSet *base_guards) {
    MVMSpeshPluginGuardSet *result = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(MVMSpeshPluginGuardSet));
    result->num_guards = (base_guards ? base_guards->num_guards : 0);
    result->guards = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            sizeof(MVMSpeshPluginGuard) * result->num_guards);
    if (base_guards) {
        memcpy(result->guards, base_guards->guards,
                base_guards->num_guards * sizeof(MVMSpeshPluginGuard));
    }
    return result;
}

static MVMSpeshPluginGuardSet * append_guard(MVMThreadContext *tc,
        MVMSpeshPluginGuardSet *base_guards, MVMObject *resolved,
        MVMSpeshPluginState *barrier) {
    MVMuint32 insert_pos, i;
    MVMSpeshPluginGuardSet *result = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(MVMSpeshPluginGuardSet));
    result->num_guards = (base_guards ? base_guards->num_guards : 0) +
        tc->num_plugin_guards + 1; /* + 1 for result node */
    result->guards = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            sizeof(MVMSpeshPluginGuard) * result->num_guards);
    if (base_guards) {
        memcpy(result->guards, base_guards->guards,
                base_guards->num_guards * sizeof(MVMSpeshPluginGuard));
        insert_pos = base_guards->num_guards;
    }
    else {
        insert_pos = 0;
    }
    for (i = 0; i < tc->num_plugin_guards; i++) {
        result->guards[insert_pos].kind = tc->plugin_guards[i].kind;
        result->guards[insert_pos].test_idx = tc->plugin_guards[i].test_idx;
        result->guards[insert_pos].skip_on_fail = 1 + tc->num_plugin_guards - i;
        switch (tc->plugin_guards[i].kind) {
            case MVM_SPESH_PLUGIN_GUARD_OBJ:
            case MVM_SPESH_PLUGIN_GUARD_NOTOBJ:
                MVM_ASSIGN_REF(tc, &(barrier->common.header),
                        result->guards[insert_pos].u.object,
                        tc->plugin_guards[i].u.object);
                break;
            case MVM_SPESH_PLUGIN_GUARD_TYPE:
                MVM_ASSIGN_REF(tc, &(barrier->common.header),
                        result->guards[insert_pos].u.type,
                        tc->plugin_guards[i].u.type);
                break;
            case MVM_SPESH_PLUGIN_GUARD_CONC:
            case MVM_SPESH_PLUGIN_GUARD_TYPEOBJ:
                /* These carry no extra argument. */
                break;
            case MVM_SPESH_PLUGIN_GUARD_GETATTR:
                MVM_ASSIGN_REF(tc, &(barrier->common.header),
                        result->guards[insert_pos].u.attr.class_handle,
                        tc->plugin_guards[i].u.attr.class_handle);
                MVM_ASSIGN_REF(tc, &(barrier->common.header),
                        result->guards[insert_pos].u.attr.name,
                        tc->plugin_guards[i].u.attr.name);
                break;
            default:
                MVM_panic(1, "Unexpected spesh plugin guard type");
        }
        insert_pos++;
    }
    result->guards[insert_pos].kind = MVM_SPESH_PLUGIN_GUARD_RESULT;
    MVM_ASSIGN_REF(tc, &(barrier->common.header),
            result->guards[insert_pos].u.result, resolved);
    return result;
}

/* Produces an updated spesh plugin state by adding the updated guards at
 * the specified position. */
MVMSpeshPluginState * update_state(MVMThreadContext *tc, MVMSpeshPluginState *result, MVMSpeshPluginState *base_state,
        MVMuint32 position, MVMSpeshPluginGuardSet *base_guards,
        MVMSpeshPluginGuardSet *new_guards) {
    result->body.num_positions = (base_state ? base_state->body.num_positions : 0) +
        (base_guards == NULL ? 1 : 0);
    result->body.positions = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            sizeof(MVMSpeshPluginPosition) * result->body.num_positions);
    if (base_state) {
        MVMuint32 copy_from = 0;
        MVMuint32 insert_at = 0;
        MVMuint32 inserted = 0;
        while (copy_from < base_state->body.num_positions) {
            MVMuint32 bytecode_position = base_state->body.positions[copy_from].bytecode_position;
            if (bytecode_position < position) {
                result->body.positions[insert_at].bytecode_position = bytecode_position;
                result->body.positions[insert_at].guard_set
                    = copy_guard_set(tc, base_state->body.positions[copy_from].guard_set);
                copy_from++;
                insert_at++;
            }
            else if (bytecode_position == position) {
                /* Update of existing state; copy those after this one. */
                result->body.positions[insert_at].bytecode_position = position;
                result->body.positions[insert_at].guard_set = new_guards;
                copy_from++;
                insert_at++;
                inserted = 1;
            }
            else if (!inserted) {
                /* Insert a new position in order. */
                result->body.positions[insert_at].bytecode_position = position;
                result->body.positions[insert_at].guard_set = new_guards;
                insert_at++;
                inserted = 1;
            }
            else {
                result->body.positions[insert_at].bytecode_position = bytecode_position;
                result->body.positions[insert_at].guard_set
                    = copy_guard_set(tc, base_state->body.positions[copy_from].guard_set);
                copy_from++;
                insert_at++;
            }
        }
        if (!inserted) {
            result->body.positions[insert_at].bytecode_position = position;
            result->body.positions[insert_at].guard_set = new_guards;
        }
    }
    else {
        result->body.positions[0].bytecode_position = position;
        result->body.positions[0].guard_set = new_guards;
    }
    return result;
}

/* Data for use with special return mechanism to store the result of a
 * resolution. */
typedef struct {
    /* Current speshresolve we're handling. */
    MVMRegister *result;
    MVMStaticFrame *sf;
    MVMuint32 position;

    /* The speshresolve that was in dynamic scope when we started this one,
     * if any. */
    MVMSpeshPluginGuard *prev_plugin_guards;
    MVMObject *prev_plugin_guard_args;
    MVMuint32 prev_num_plugin_guards;
} MVMSpeshPluginSpecialReturnData;

/* Callback to mark the special return data. */
static void mark_plugin_sr_data(MVMThreadContext *tc, MVMFrame *frame, MVMGCWorklist *worklist) {
    MVMSpeshPluginSpecialReturnData *srd = (MVMSpeshPluginSpecialReturnData *)
        frame->extra->special_return_data;
    MVM_gc_worklist_add(tc, worklist, &(srd->sf));
    MVM_gc_worklist_add(tc, worklist, &(srd->prev_plugin_guard_args));
    MVM_spesh_plugin_guard_list_mark(tc, srd->prev_plugin_guards, srd->prev_num_plugin_guards, worklist);
}

/* Restore the spesh plugin resolve state that was in dynamic scope when we
 * started this spesh resolve. */
static void restore_prev_spesh_plugin_state(MVMThreadContext *tc, MVMSpeshPluginSpecialReturnData *srd) {
    tc->plugin_guards = srd->prev_plugin_guards;
    tc->plugin_guard_args = srd->prev_plugin_guard_args;
    tc->num_plugin_guards = srd->prev_num_plugin_guards;
}

static void stash_prev_spesh_plugin_state(MVMThreadContext *tc, MVMSpeshPluginSpecialReturnData *srd) {
    tc->temp_plugin_guards = srd->prev_plugin_guards;
    tc->temp_plugin_guard_args = srd->prev_plugin_guard_args;
    tc->temp_num_plugin_guards = srd->prev_num_plugin_guards;
}

static void apply_prev_spesh_plugin_state(MVMThreadContext *tc) {
    tc->plugin_guards = tc->temp_plugin_guards;
    tc->plugin_guard_args = tc->temp_plugin_guard_args;
    tc->num_plugin_guards = tc->temp_num_plugin_guards;
    tc->temp_plugin_guards = NULL;
    tc->temp_plugin_guard_args = NULL;
    tc->temp_num_plugin_guards = 0;
}

/* Callback after a resolver to update the guard structure. */
static void add_resolution_to_guard_set(MVMThreadContext *tc, void *sr_data) {
    MVMSpeshPluginSpecialReturnData *srd = (MVMSpeshPluginSpecialReturnData *)sr_data;
    MVMSpeshPluginState *base_state = get_plugin_state(tc, srd->sf);
    MVMSpeshPluginGuardSet *base_guards;
    stash_prev_spesh_plugin_state(tc, srd);
    base_guards = guard_set_for_position(tc, srd->position, base_state);
    if (!base_guards || base_guards->num_guards < MVM_SPESH_GUARD_LIMIT) {
        if (!base_guards || !already_have_guard(tc, base_guards)) {
            MVMSpeshPluginState *new_state;
            MVMROOT(tc, base_state, {
                new_state = (MVMSpeshPluginState *)MVM_repr_alloc_init(tc, tc->instance->SpeshPluginState);
            });
            {
                MVMSpeshPluginGuardSet *new_guards = append_guard(tc, base_guards, srd->result->o, new_state);
                MVMuint32 committed;
                update_state(tc, new_state, base_state, srd->position, base_guards, new_guards);
                committed = MVM_trycas(
                        &srd->sf->body.spesh->body.plugin_state,
                        base_state, new_state);
                if (committed) {
                    MVM_gc_write_barrier(tc, (MVMCollectable*)srd->sf->body.spesh, (MVMCollectable *)new_state);
                }
                else {
                    /* Lost update race. Discard; a retry will happen naturally. */
                }
            }
        }
    }
    else {
#if MVM_SPESH_GUARD_DEBUG
        fprintf(stderr, "Too many plugin guards added (%"PRIu32"); probable spesh plugin bug\n",
            base_guards->num_guards);
        MVM_dump_backtrace(tc);
#endif
    }

    /* Clear up recording state. */
    MVM_fixed_size_free(tc, tc->instance->fsa,
            MVM_SPESH_PLUGIN_GUARD_LIMIT * sizeof(MVMSpeshPluginGuard),
            tc->plugin_guards);
    apply_prev_spesh_plugin_state(tc);

    MVM_free(srd);
}

/* When we throw an exception when evaluating a resolver, then clean up any
 * recorded guards so as not to leak memory. */
static void cleanup_recorded_guards(MVMThreadContext *tc, void *sr_data) {
    MVMSpeshPluginSpecialReturnData *srd = (MVMSpeshPluginSpecialReturnData *)sr_data;
    MVM_fixed_size_free(tc, tc->instance->fsa,
            MVM_SPESH_PLUGIN_GUARD_LIMIT * sizeof(MVMSpeshPluginGuard),
            tc->plugin_guards);
    restore_prev_spesh_plugin_state(tc, srd);
    MVM_free(srd);
}

/* Sets up state in the current thread context for recording guards. */
static void setup_for_guard_recording(MVMThreadContext *tc, MVMCallsite *callsite) {
    /* Validate the callsite meets restrictions. */
    MVMuint32 i = 0;
    if (callsite->num_pos != callsite->flag_count)
        MVM_exception_throw_adhoc(tc, "A spesh plugin must have only positional args");
    if (callsite->has_flattening)
        MVM_exception_throw_adhoc(tc, "A spesh plugin must not have flattening args");
    for (i = 0; i < callsite->flag_count; i++)
        if ((callsite->arg_flags[i] & MVM_CALLSITE_ARG_TYPE_MASK) != MVM_CALLSITE_ARG_OBJ)
            MVM_exception_throw_adhoc(tc, "A spesh plugin must only be passed object args");

    /* Set up guard recording space and arguments array. */
    tc->plugin_guards = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            MVM_SPESH_PLUGIN_GUARD_LIMIT * sizeof(MVMSpeshPluginGuard));
    tc->num_plugin_guards = 0;
    tc->plugin_guard_args = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    for (i = 0; i < callsite->flag_count; i++)
        MVM_repr_push_o(tc, tc->plugin_guard_args, tc->cur_frame->args[i].o);
}

/* Resolves a spesh plugin for the current HLL from the slow-path interpreter. */
static void call_resolver(MVMThreadContext *tc, MVMString *name, MVMRegister *result,
                          MVMuint32 position, MVMStaticFrame *sf, MVMuint8 *next_addr,
                          MVMCallsite *callsite) {
    /* No pre-resolved value, so we need to run the plugin. Capture state
     * of any ongoing spesh plugin resolve. */
    MVMSpeshPluginGuard *prev_plugin_guards = tc->plugin_guards;
    MVMObject *prev_plugin_guard_args = tc->plugin_guard_args;
    MVMuint32 prev_num_plugin_guards = tc->num_plugin_guards;

    /* Find the plugin. */
    MVMSpeshPluginSpecialReturnData *srd;
    MVMObject *plugin = NULL;
    MVMHLLConfig *hll = MVM_hll_current(tc);
    uv_mutex_lock(&tc->instance->mutex_hllconfigs);
    if (hll->spesh_plugins)
        plugin = MVM_repr_at_key_o(tc, hll->spesh_plugins, name);
    uv_mutex_unlock(&tc->instance->mutex_hllconfigs);
    if (MVM_is_null(tc, plugin)) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "No such spesh plugin '%s' for current language",
            c_name);
    }

    /* Run it, registering handlers to save or discard guards and result. */
    tc->cur_frame->return_value = result;
    tc->cur_frame->return_type = MVM_RETURN_OBJ;
    if (next_addr)
        tc->cur_frame->return_address = next_addr; /* JIT sets this otherwise */
    srd = MVM_malloc(sizeof(MVMSpeshPluginSpecialReturnData));
    srd->result = result;
    srd->position = position;
    srd->sf = sf;
    srd->prev_plugin_guards = prev_plugin_guards;
    srd->prev_plugin_guard_args = prev_plugin_guard_args;
    srd->prev_num_plugin_guards = prev_num_plugin_guards;
    MVM_frame_special_return(tc, tc->cur_frame, add_resolution_to_guard_set,
            cleanup_recorded_guards, srd, mark_plugin_sr_data);

    /* Set up the guard state to record into. */
    MVMROOT2(tc, plugin, prev_plugin_guard_args, {
        setup_for_guard_recording(tc, callsite);
    });
    STABLE(plugin)->invoke(tc, plugin, callsite, tc->cur_frame->args);
}
void MVM_spesh_plugin_resolve(MVMThreadContext *tc, MVMString *name,
                              MVMRegister *result, MVMuint8 *op_addr,
                              MVMuint8 *next_addr, MVMCallsite *callsite) {
    MVMuint32 position = (MVMuint32)(op_addr - *tc->interp_bytecode_start);
    MVMObject *resolved;
    MVMuint16 guard_offset;
    MVMROOT(tc, name, {
        resolved = resolve_using_guards(tc, position, callsite, &guard_offset,
                tc->cur_frame->static_info);
    });
    if (resolved) {
        /* Resolution through guard tree successful, so no invoke needed. */
        result->o = resolved;
        *tc->interp_cur_op = next_addr;
        if (MVM_spesh_log_is_logging(tc))
            MVM_spesh_log_plugin_resolution(tc, position, guard_offset);
    }
    else {
        call_resolver(tc, name, result, position, tc->cur_frame->static_info,
                next_addr, callsite);
    }
}

/* Resolves a spesh plugin for the current HLL from quickened bytecode. */
void MVM_spesh_plugin_resolve_spesh(MVMThreadContext *tc, MVMString *name,
                                    MVMRegister *result, MVMuint32 position,
                                    MVMStaticFrame *sf, MVMuint8 *next_addr,
                                    MVMCallsite *callsite) {
    MVMObject *resolved;
    MVMuint16 guard_offset;
    MVMROOT2(tc, name, sf, {
        resolved = resolve_using_guards(tc, position, callsite, &guard_offset, sf);
    });
    if (resolved) {
        /* Resolution through guard tree successful, so no invoke needed. */
        result->o = resolved;
        *tc->interp_cur_op = next_addr;
    }
    else {
        call_resolver(tc, name, result, position, sf, next_addr, callsite);
    }
}

/* Resolves a spesh plugin for the current HLL from the JIT. */
void MVM_spesh_plugin_resolve_jit(MVMThreadContext *tc, MVMString *name,
                                  MVMRegister *result, MVMuint32 position,
                                  MVMStaticFrame *sf, MVMCallsite *callsite) {
    MVMObject *resolved;
    MVMuint16 guard_offset;

    /* Save callsite in frame (the JIT's output doesn't do so, and we need
     * it for GC marking). */
    tc->cur_frame->cur_args_callsite = callsite;

    MVMROOT2(tc, name, sf, {
        resolved = resolve_using_guards(tc, position, callsite, &guard_offset, sf);
    });
    if (resolved) {
        /* Resolution through guard tree successful, so no invoke needed. */
        result->o = resolved;
    }
    else {
        call_resolver(tc, name, result, position, sf, NULL, callsite);
    }
}

/* Returns a pointer into the current recording guard set for the guard we
 * should write into. */
MVMSpeshPluginGuard * get_guard_to_record_into(MVMThreadContext *tc) {
    if (tc->plugin_guards) {
        if (tc->num_plugin_guards < MVM_SPESH_PLUGIN_GUARD_LIMIT) {
            return &(tc->plugin_guards[tc->num_plugin_guards++]);
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Too many guards (%"PRIu32") recorded by spesh plugin, max allowed is %"PRId32"",
                tc->num_plugin_guards, MVM_SPESH_PLUGIN_GUARD_LIMIT);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Not in a spesh plugin, so cannot record a guard");
    }
}

/* Gets the index of the guarded args to record a guard against. */
MVMuint16 get_guard_arg_index(MVMThreadContext *tc, MVMObject *find) {
    MVMint64 n = MVM_repr_elems(tc, tc->plugin_guard_args);
    MVMint64 i;
    for (i = 0; i < n; i++)
        if (MVM_repr_at_pos_o(tc, tc->plugin_guard_args, i) == find)
            return (MVMuint16)i;
    MVM_exception_throw_adhoc(tc, "Object not in set of those to guard against");
}

/* Adds a guard that the guardee must have exactly the specified type. Will
 * throw if we are not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_type(MVMThreadContext *tc, MVMObject *guardee, MVMObject *type) {
    MVMuint16 idx = get_guard_arg_index(tc, guardee);
    MVMSpeshPluginGuard *guard = get_guard_to_record_into(tc);
    guard->kind = MVM_SPESH_PLUGIN_GUARD_TYPE;
    guard->test_idx = idx;
    guard->u.type = STABLE(type);
}

/* Adds a guard that the guardee must be concrete. Will throw if we are not
 * currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_concrete(MVMThreadContext *tc, MVMObject *guardee) {
    MVMuint16 idx = get_guard_arg_index(tc, guardee);
    MVMSpeshPluginGuard *guard = get_guard_to_record_into(tc);
    guard->kind = MVM_SPESH_PLUGIN_GUARD_CONC;
    guard->test_idx = idx;
}

/* Adds a guard that the guardee must not be concrete. Will throw if we are
 * not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_typeobj(MVMThreadContext *tc, MVMObject *guardee) {
    MVMuint16 idx = get_guard_arg_index(tc, guardee);
    MVMSpeshPluginGuard *guard = get_guard_to_record_into(tc);
    guard->kind = MVM_SPESH_PLUGIN_GUARD_TYPEOBJ;
    guard->test_idx = idx;
}

/* Adds a guard that the guardee must exactly match the provided object
 * literal. Will throw if we are not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_obj(MVMThreadContext *tc, MVMObject *guardee) {
    MVMuint16 idx = get_guard_arg_index(tc, guardee);
    MVMSpeshPluginGuard *guard = get_guard_to_record_into(tc);
    guard->kind = MVM_SPESH_PLUGIN_GUARD_OBJ;
    guard->test_idx = idx;
    guard->u.object = guardee;
}

/* Adds a guard that the guardee must NOT exactly match the provided object
 * literal. Will throw if we are not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_notobj(MVMThreadContext *tc, MVMObject *guardee, MVMObject *not) {
    MVMuint16 idx = get_guard_arg_index(tc, guardee);
    MVMSpeshPluginGuard *guard = get_guard_to_record_into(tc);
    guard->kind = MVM_SPESH_PLUGIN_GUARD_NOTOBJ;
    guard->test_idx = idx;
    guard->u.object = not;
}

/* Gets an attribute and adds that object to the set of objects that we may
 * guard against. Will throw if we are not currently inside of a spesh
 * plugin. */
MVMObject * MVM_spesh_plugin_addguard_getattr(MVMThreadContext *tc, MVMObject *guardee,
            MVMObject *class_handle, MVMString *name) {
    MVMObject *attr;
    MVMuint16 idx = get_guard_arg_index(tc, guardee);
    MVMSpeshPluginGuard *guard = get_guard_to_record_into(tc);
    guard->kind = MVM_SPESH_PLUGIN_GUARD_GETATTR;
    guard->test_idx = idx;
    guard->u.attr.class_handle = class_handle;
    guard->u.attr.name = name;
    attr = MVM_repr_get_attr_o(tc, guardee, class_handle, name, MVM_NO_HINT);
    MVM_repr_push_o(tc, tc->plugin_guard_args, attr);
    return attr;
}

/* Rewrites a spesh plugin resolve to instead resolve to a spesh slot with the
 * result of the resolution, inserting guards as needed. */
static MVMSpeshOperand *arg_ins_to_reg_list(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                            MVMSpeshIns *ins, MVMuint32 *reg_list_length) {
    /* Search back to prepargs to find the maximum argument index. */
    MVMSpeshIns *cursor = ins->prev;
    MVMSpeshIns *cur_arg;
    MVMint32 max_arg_idx = -1;
    while (cursor->info->opcode != MVM_OP_prepargs) {
        if (cursor->info->opcode == MVM_OP_arg_o) {
            MVMuint16 idx = cursor->operands[0].lit_ui16;
            if (idx > max_arg_idx)
                max_arg_idx = idx;
        }
        else {
            MVM_oops(tc, "Malformed spesh resolve argument sequence");
        }
        cursor = cursor->prev;
    }
    cur_arg = cursor->next;

    /* Delete the prepargs instruction. */
    MVM_spesh_manipulate_delete_ins(tc, g, bb, cursor);

    /* If there are any args, collect registers and delete them. */
    if (max_arg_idx >= 0) {
        MVMSpeshOperand *result = MVM_malloc((max_arg_idx + 1) * sizeof(MVMSpeshOperand));
        while (cur_arg->info->opcode == MVM_OP_arg_o) {
            MVMSpeshIns *next_arg = cur_arg->next;
            result[cur_arg->operands[0].lit_ui16] = cur_arg->operands[1];
            MVM_spesh_manipulate_delete_ins(tc, g, bb, cur_arg);
            cur_arg = next_arg;
        }
        *reg_list_length = max_arg_idx + 1;
        return result;
    }
    else {
        *reg_list_length = 0;
        return NULL;
    }
}
MVMSpeshAnn * steal_prepargs_deopt(MVMThreadContext *tc, MVMSpeshIns *ins) {
    MVMSpeshIns *cur = ins->prev;
    while (cur) {
        if (cur->info->opcode == MVM_OP_prepargs) {
            MVMSpeshAnn *ann = cur->annotations;
            MVMSpeshAnn *prev_ann = NULL;
            while (ann) {
                if (ann->type == MVM_SPESH_ANN_DEOPT_ONE_INS) {
                    if (prev_ann)
                        prev_ann->next = ann->next;
                    else
                        cur->annotations = ann->next;
                    ann->next = NULL;
                    return ann;
                }
                prev_ann = ann;
                ann = ann->next;
            }
            MVM_oops(tc, "Could not find deopt annotation on prepargs before speshresolve");
        }
        cur = cur->prev;
    }
    MVM_oops(tc, "Could not find prepargs before speshresolve");
}
MVMSpeshAnn * clone_deopt_ann(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshAnn *in) {
    MVMSpeshAnn *cloned = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAnn));
    MVMuint32 deopt_idx = g->num_deopt_addrs;
    cloned->type = in->type;
    cloned->data.deopt_idx = deopt_idx;
    MVM_spesh_graph_grow_deopt_table(tc, g);
    g->deopt_addrs[deopt_idx * 2] = g->deopt_addrs[in->data.deopt_idx * 2];
    g->num_deopt_addrs++;
    return cloned;
}
void MVM_spesh_plugin_rewrite_resolve(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                      MVMSpeshIns *ins, MVMuint32 bytecode_offset,
                                      MVMint32 guard_index) {
    /* Resolve guard set (should never be missing, but fail softly if so). */
    MVMSpeshPluginState *ps = get_plugin_state(tc, g->sf);
    MVMSpeshPluginGuardSet *gs = guard_set_for_position(tc, bytecode_offset, ps);
    if (gs) {
        MVM_VECTOR_DECL(MVMSpeshOperand, temps);

        /* Steal the deopt annotation from the prepargs, and calculate the
         * deopt position. */
        MVMSpeshAnn *stolen_deopt_ann = steal_prepargs_deopt(tc, ins);
        MVMuint32 stolen_deopt_ann_used = 0;
        MVMuint32 deopt_idx = stolen_deopt_ann->data.deopt_idx;

        /* Collect registers that go with each argument, and delete the arg
         * and prepargs instructions. */
        MVMuint32 arg_regs_length, i;
        MVMSpeshOperand *arg_regs = arg_ins_to_reg_list(tc, g, bb, ins, &arg_regs_length);

        /* Find result and add it to a spesh slot. */
        MVMObject *resolvee = gs->guards[guard_index].u.result;
        MVMuint16 resolvee_slot = MVM_spesh_add_spesh_slot_try_reuse(tc, g, (MVMCollectable *)resolvee);

        /* Find guard range. */
        MVMint32 guards_end = guard_index - 1;
        MVMint32 guards_start = guards_end;
        while (guards_start >= 0 && gs->guards[guards_start].kind != MVM_SPESH_PLUGIN_GUARD_RESULT)
            guards_start--;

        /* Rewrite resolve instruction into a spesh slot lookup. */
        ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
        ins->operands[1].lit_i16 = resolvee_slot;
        MVM_spesh_facts_object_facts(tc, g, ins->operands[0], resolvee);

        /* Prepend guards. */
        MVM_VECTOR_INIT(temps, 0);
        while (++guards_start <= guards_end) {
            MVMSpeshPluginGuard *guard = &(gs->guards[guards_start]);
            MVMSpeshFacts *preguarded_facts = MVM_spesh_get_facts(tc, g, arg_regs[guard->test_idx]);
            if (guard->kind != MVM_SPESH_PLUGIN_GUARD_GETATTR) {
                if (stolen_deopt_ann_used) {
                    stolen_deopt_ann = clone_deopt_ann(tc, g, stolen_deopt_ann);
                    deopt_idx = stolen_deopt_ann->data.deopt_idx;
                }
            }
            switch (guard->kind) {
                case MVM_SPESH_PLUGIN_GUARD_OBJ: {
                    if ((preguarded_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) == 0
                            || preguarded_facts->value.o != (MVMObject *)guard->u.object) {
                        MVMSpeshOperand preguard_reg = arg_regs[guard->test_idx];
                        MVMSpeshOperand guard_reg = MVM_spesh_manipulate_split_version(tc, g,
                                preguard_reg, bb, ins->prev);
                        MVMSpeshFacts *guarded_facts = MVM_spesh_get_facts(tc, g, guard_reg);
                        MVMSpeshIns *guard_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                        guard_ins->info = MVM_op_get_op(MVM_OP_sp_guardobj);
                        guard_ins->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
                        guard_ins->operands[0] = guard_reg;
                        guarded_facts->writer = guard_ins;
                        guard_ins->operands[1] = preguard_reg;
                        MVM_spesh_usages_add_by_reg(tc, g, preguard_reg, guard_ins);
                        guard_ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                                (MVMCollectable *)guard->u.object);
                        guard_ins->operands[3].lit_ui32 = deopt_idx;
                        guard_ins->annotations = stolen_deopt_ann;
                        MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard_ins);
                        guarded_facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                        guarded_facts->value.o = (MVMObject *)guard->u.object;
                        arg_regs[guard->test_idx] = guard_reg;
                        stolen_deopt_ann_used = 1;
                    }
                    else {
                        MVM_spesh_get_and_use_facts(tc, g, arg_regs[guard->test_idx]);
                    }
                    break;
                }
                case MVM_SPESH_PLUGIN_GUARD_NOTOBJ: {
                    MVMuint32 may_match = 1;
                    if ((preguarded_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) &&
                            preguarded_facts->value.o != guard->u.object)
                        may_match = 0;
                    else if ((preguarded_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) &&
                            STABLE(preguarded_facts->type) != STABLE(guard->u.object))
                        may_match = 0;
                    else if ((preguarded_facts->flags & MVM_SPESH_FACT_CONCRETE) &&
                            !IS_CONCRETE(guard->u.object))
                        may_match = 0;
                    else if ((preguarded_facts->flags & MVM_SPESH_FACT_TYPEOBJ) &&
                            IS_CONCRETE(guard->u.object))
                        may_match = 0;
                    if (may_match) {
                        MVMSpeshOperand preguard_reg = arg_regs[guard->test_idx];
                        MVMSpeshOperand guard_reg = MVM_spesh_manipulate_split_version(tc, g,
                                preguard_reg, bb, ins->prev);
                        MVMSpeshFacts *guarded_facts = MVM_spesh_get_facts(tc, g, guard_reg);
                        MVMSpeshIns *guard_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                        guard_ins->info = MVM_op_get_op(MVM_OP_sp_guardnotobj);
                        guard_ins->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
                        guard_ins->operands[0] = guard_reg;
                        guarded_facts->writer = guard_ins;
                        guard_ins->operands[1] = preguard_reg;
                        MVM_spesh_usages_add_by_reg(tc, g, preguard_reg, guard_ins);
                        guard_ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                                (MVMCollectable *)guard->u.object);
                        guard_ins->operands[3].lit_ui32 = deopt_idx;
                        guard_ins->annotations = stolen_deopt_ann;
                        MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard_ins);
                        stolen_deopt_ann_used = 1;
                    }
                    else {
                        MVM_spesh_get_and_use_facts(tc, g, arg_regs[guard->test_idx]);
                    }
                    break;
                }
                case MVM_SPESH_PLUGIN_GUARD_TYPE: {
                    if ((preguarded_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE) == 0
                            || STABLE(preguarded_facts->type) != guard->u.type) {
                        MVMSpeshOperand preguard_reg = arg_regs[guard->test_idx];
                        MVMSpeshOperand guard_reg = MVM_spesh_manipulate_split_version(tc, g,
                                preguard_reg, bb, ins->prev);
                        MVMSpeshFacts *guarded_facts = MVM_spesh_get_facts(tc, g, guard_reg);
                        MVMSpeshIns *guard_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                        guard_ins->info = MVM_op_get_op(MVM_OP_sp_guard);
                        guard_ins->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
                        guard_ins->operands[0] = guard_reg;
                        guarded_facts->writer = guard_ins;
                        guard_ins->operands[1] = preguard_reg;
                        MVM_spesh_usages_add_by_reg(tc, g, preguard_reg, guard_ins);
                        guard_ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                                (MVMCollectable *)guard->u.type);
                        guard_ins->operands[3].lit_ui32 = deopt_idx;
                        guard_ins->annotations = stolen_deopt_ann;
                        MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard_ins);
                        guarded_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
                        guarded_facts->type = guard->u.type->WHAT;
                        arg_regs[guard->test_idx] = guard_reg;
                        stolen_deopt_ann_used = 1;
                    }
                    else {
                        MVM_spesh_get_and_use_facts(tc, g, arg_regs[guard->test_idx]);
                    }
                    break;
                }
                case MVM_SPESH_PLUGIN_GUARD_CONC: {
                    if ((preguarded_facts->flags & MVM_SPESH_FACT_CONCRETE) == 0) {
                        MVMSpeshOperand preguard_reg = arg_regs[guard->test_idx];
                        MVMSpeshOperand guard_reg = MVM_spesh_manipulate_split_version(tc, g,
                                preguard_reg, bb, ins->prev);
                        MVMSpeshFacts *guarded_facts = MVM_spesh_get_facts(tc, g, guard_reg);
                        MVMSpeshIns *guard_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                        guard_ins->info = MVM_op_get_op(MVM_OP_sp_guardjustconc);
                        guard_ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                        guard_ins->operands[0] = guard_reg;
                        guarded_facts->writer = guard_ins;
                        guard_ins->operands[1] = preguard_reg;
                        MVM_spesh_usages_add_by_reg(tc, g, preguard_reg, guard_ins);
                        guard_ins->operands[2].lit_ui32 = deopt_idx;
                        guard_ins->annotations = stolen_deopt_ann;
                        MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard_ins);
                        guarded_facts->flags |= MVM_SPESH_FACT_CONCRETE;
                        arg_regs[guard->test_idx] = guard_reg;
                        stolen_deopt_ann_used = 1;
                    }
                    else {
                        MVM_spesh_get_and_use_facts(tc, g, arg_regs[guard->test_idx]);
                    }
                    break;
                }
                case MVM_SPESH_PLUGIN_GUARD_TYPEOBJ: {
                    if ((preguarded_facts->flags & MVM_SPESH_FACT_TYPEOBJ) == 0) {
                        MVMSpeshOperand preguard_reg = arg_regs[guard->test_idx];
                        MVMSpeshOperand guard_reg = MVM_spesh_manipulate_split_version(tc, g,
                                preguard_reg, bb, ins->prev);
                        MVMSpeshFacts *guarded_facts = MVM_spesh_get_facts(tc, g, guard_reg);
                        MVMSpeshIns *guard_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                        guard_ins->info = MVM_op_get_op(MVM_OP_sp_guardjusttype);
                        guard_ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                        guard_ins->operands[0] = guard_reg;
                        guarded_facts->writer = guard_ins;
                        guard_ins->operands[1] = preguard_reg;
                        MVM_spesh_usages_add_by_reg(tc, g, preguard_reg, guard_ins);
                        guard_ins->operands[2].lit_ui32 = deopt_idx;
                        guard_ins->annotations = stolen_deopt_ann;
                        MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard_ins);
                        guarded_facts->flags |= MVM_SPESH_FACT_TYPEOBJ;
                        arg_regs[guard->test_idx] = guard_reg;
                        stolen_deopt_ann_used = 1;
                    }
                    else {
                        MVM_spesh_get_and_use_facts(tc, g, arg_regs[guard->test_idx]);
                    }
                    break;
                }
                case MVM_SPESH_PLUGIN_GUARD_GETATTR: {
                    MVMSpeshFacts *facts;

                    /* This isn't really a guard, but rather a request to insert
                     * a getattr instruction. We also need a target register for
                     * it, and will allocate a temporary one for it. */
                    MVMSpeshOperand target = MVM_spesh_manipulate_get_temp_reg(tc,
                        g, MVM_reg_obj);

                    /* Further to that, we also need registers holding both the
                     * class handle and attribute name, which will be used by
                     * the getattr instructions. */
                    MVMSpeshOperand ch_temp = MVM_spesh_manipulate_get_temp_reg(tc,
                        g, MVM_reg_obj);
                    MVMSpeshOperand name_temp = MVM_spesh_manipulate_get_temp_reg(tc,
                        g, MVM_reg_str);

                    /* Emit spesh slot lookup instruction for the class handle. */
                    MVMSpeshIns *spesh_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                    spesh_ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
                    spesh_ins->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
                    spesh_ins->operands[0] = ch_temp;
                    spesh_ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                        (MVMCollectable *)guard->u.attr.class_handle);
                    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, spesh_ins);
                    facts = MVM_spesh_get_facts(tc, g, ch_temp);
                    facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE;
                    facts->type = guard->u.attr.class_handle;
                    facts->writer = spesh_ins;

                    /* Emit spesh slot lookup instruction for the attr name. */
                    spesh_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                    spesh_ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
                    spesh_ins->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
                    spesh_ins->operands[0] = name_temp;
                    spesh_ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                        (MVMCollectable *)guard->u.attr.name);
                    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, spesh_ins);
                    facts = MVM_spesh_get_facts(tc, g, name_temp);
                    facts->flags |= MVM_SPESH_FACT_KNOWN_VALUE;
                    facts->value.s = guard->u.attr.name;
                    facts->writer = spesh_ins;

                    /* Emit the getattr instruction. */
                    spesh_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                    spesh_ins->info = MVM_op_get_op(MVM_OP_getattrs_o);
                    spesh_ins->operands = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));
                    spesh_ins->operands[0] = target;
                    spesh_ins->operands[1] = arg_regs[guard->test_idx];
                    MVM_spesh_usages_add_by_reg(tc, g, arg_regs[guard->test_idx], spesh_ins);
                    spesh_ins->operands[2] = ch_temp;
                    MVM_spesh_usages_add_by_reg(tc, g, ch_temp, spesh_ins);
                    spesh_ins->operands[3] = name_temp;
                    MVM_spesh_usages_add_by_reg(tc, g, name_temp, spesh_ins);
                    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, spesh_ins);
                    MVM_spesh_get_facts(tc, g, target)->writer = spesh_ins;

                    /* Release the temporaries for the class handle and name. */
                    MVM_spesh_manipulate_release_temp_reg(tc, g, ch_temp);
                    MVM_spesh_manipulate_release_temp_reg(tc, g, name_temp);

                    /* Add the target into the guard reg set and stash it in our
                     * list of temp registers to release. */
                    arg_regs = MVM_realloc(arg_regs,
                        (arg_regs_length + 1) * sizeof(MVMSpeshOperand));
                    arg_regs[arg_regs_length++] = target;
                    MVM_VECTOR_PUSH(temps, target);

                    break;
                }
                default:
                    MVM_panic(1, "Unknown spesh plugin guard kind %d to insert during specialization",
                            guard->kind);
            }
        }

        /* Free up any temporary registers we created. */
        for (i = 0; i < MVM_VECTOR_ELEMS(temps); i++)
            MVM_spesh_manipulate_release_temp_reg(tc, g, temps[i]);
        MVM_VECTOR_DESTROY(temps);
        MVM_free(arg_regs);
    }
}

/* Called to mark a guard list. */
void MVM_spesh_plugin_guard_list_mark(MVMThreadContext *tc, MVMSpeshPluginGuard *guards,
                                      MVMuint32 num_guards, MVMGCWorklist *worklist) {
    MVMuint32 i;
    if (guards) {
        for (i = 0; i < num_guards; i++) {
            switch (guards[i].kind) {
                case MVM_SPESH_PLUGIN_GUARD_RESULT:
                    MVM_gc_worklist_add(tc, worklist, &guards[i].u.result);
                    break;
                case MVM_SPESH_PLUGIN_GUARD_OBJ:
                case MVM_SPESH_PLUGIN_GUARD_NOTOBJ:
                    MVM_gc_worklist_add(tc, worklist, &guards[i].u.object);
                    break;
                case MVM_SPESH_PLUGIN_GUARD_TYPE:
                    MVM_gc_worklist_add(tc, worklist, &guards[i].u.type);
                    break;
                case MVM_SPESH_PLUGIN_GUARD_GETATTR:
                    MVM_gc_worklist_add(tc, worklist, &guards[i].u.attr.class_handle);
                    MVM_gc_worklist_add(tc, worklist, &guards[i].u.attr.name);
                    break;
            }
        }
    }
}
