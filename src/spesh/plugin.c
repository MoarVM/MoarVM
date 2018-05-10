#include "moar.h"

/* The spesh plugin mechanism allows for extending spesh to be able to reason
 * about high-level language semantics that would be challenging to implement
 * inside of the specializer, perhaps because we don't communicate sufficient
 * information for it to do what is required. */

/* Registers a new spesh plugin. */
void MVM_spesh_plugin_register(MVMThreadContext *tc, MVMString *language,
        MVMString *name, MVMObject *plugin) {
    MVMHLLConfig *hll = MVM_hll_get_config_for(tc, language);
    uv_mutex_lock(&tc->instance->mutex_hllconfigs);
    if (!hll->spesh_plugins)
        hll->spesh_plugins = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTHash);
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
        MVMint32 r = ps->num_positions - 1;
        while (l <= r) {
            MVMint32 m = l + (r - l) / 2;
            MVMuint32 test = ps->positions[m].bytecode_position;
            if (test == cur_position) {
                return ps->positions[m].guard_set;
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
            switch (kind) {
                case MVM_SPESH_PLUGIN_GUARD_OBJ:
                    pos += test == gs->guards[pos].u.object
                        ? 1
                        : gs->guards[pos].skip_on_fail;
                    break;
                case MVM_SPESH_PLUGIN_GUARD_TYPE:
                    pos += STABLE(test) == gs->guards[pos].u.type
                        ? 1
                        : gs->guards[pos].skip_on_fail;
                    break;
                case MVM_SPESH_PLUGIN_GUARD_CONC:
                    pos += IS_CONCRETE(test) ? 1 : gs->guards[pos].skip_on_fail;
                    break;
                case MVM_SPESH_PLUGIN_GUARD_TYPEOBJ:
                    pos += IS_CONCRETE(test) ? gs->guards[pos].skip_on_fail : 1;
                    break;
                case MVM_SPESH_PLUGIN_GUARD_GETATTR:
                    if (MVM_is_null(tc, collected_objects))
                        collected_objects = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
                    MVMROOT(tc, collected_objects, {
                        MVMObject *attr = MVM_repr_get_attr_o(tc, test,
                                gs->guards[pos].u.attr.class_handle, gs->guards[pos].u.attr.name, MVM_NO_HINT);
                        MVM_repr_push_o(tc, collected_objects, attr);
                    });
                    pos++;
                    break;
                default:
                    MVM_panic(1, "Guard kind unrecognized in spesh plugin guard set");
            }
        }
    }
    return NULL;
}

/* Tries to resolve a plugin by looking at the guards for the position. */
static MVMObject * resolve_using_guards(MVMThreadContext *tc, MVMuint32 cur_position,
        MVMCallsite *callsite, MVMuint16 *guard_offset) {
    MVMSpeshPluginState *ps = get_plugin_state(tc, tc->cur_frame->static_info);
    MVMSpeshPluginGuardSet *gs = guard_set_for_position(tc, cur_position, ps);
    return gs ? evaluate_guards(tc, gs, callsite, guard_offset) : NULL;
}

/* Produces an updated guard set with the given resolution result. Returns
 * the base guard set if we already having a matching guard (which means two
 * threads raced to do this resolution). */
static MVMint32 already_have_guard(MVMThreadContext *tc, MVMSpeshPluginGuardSet *base_guards) {
    return 0;
}
static MVMSpeshPluginGuardSet * append_guard(MVMThreadContext *tc,
        MVMSpeshPluginGuardSet *base_guards, MVMObject *resolved,
        MVMStaticFrameSpesh *barrier) {
    MVMSpeshPluginGuardSet *result;
    if (base_guards && already_have_guard(tc, base_guards)) {
        result = base_guards;
    }
    else {
        MVMuint32 insert_pos, i;
        result = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(MVMSpeshPluginGuardSet));
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
    }
    return result;
}

/* Produces an updated spesh plugin state by adding the updated guards at
 * the specified position. */
MVMSpeshPluginState * updated_state(MVMThreadContext *tc, MVMSpeshPluginState *base_state,
        MVMuint32 position, MVMSpeshPluginGuardSet *base_guards,
        MVMSpeshPluginGuardSet *new_guards) {
    MVMSpeshPluginState *result = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            sizeof(MVMSpeshPluginState));
    result->num_positions = (base_state ? base_state->num_positions : 0) +
        (base_guards == NULL ? 1 : 0);
    result->positions = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            sizeof(MVMSpeshPluginPosition) * result->num_positions);
    if (base_state) {
        MVMuint32 copy_from = 0;
        MVMuint32 insert_at = 0;
        MVMuint32 inserted = 0;
        while (!inserted && copy_from < base_state->num_positions) {
            MVMuint32 bytecode_position = base_state->positions[copy_from].bytecode_position;
            if (bytecode_position < position) {
                result->positions[insert_at] = base_state->positions[copy_from];
                copy_from++;
                insert_at++;
            }
            else if (bytecode_position == position) {
                /* Update of existing state; copy those after this one. */
                result->positions[insert_at].bytecode_position = position;
                result->positions[insert_at].guard_set = new_guards;
                copy_from++;
                insert_at++;
                inserted = 1;
            }
            else {
                /* Insert a new position in order. */
                result->positions[insert_at].bytecode_position = position;
                result->positions[insert_at].guard_set = new_guards;
                insert_at++;
                inserted = 1;
            }
            if (inserted && copy_from < base_state->num_positions)
                memcpy(result->positions + insert_at, base_state->positions + copy_from,
                        (base_state->num_positions - copy_from) * sizeof(MVMSpeshPluginPosition));
        }
        if (!inserted) {
            result->positions[insert_at].bytecode_position = position;
            result->positions[insert_at].guard_set = new_guards;
        }
    }
    else {
        result->positions[0].bytecode_position = position;
        result->positions[0].guard_set = new_guards;
    }
    return result;
}

/* Schedules replaced guards to be freed. */
void free_dead_guards(MVMThreadContext *tc, MVMSpeshPluginGuardSet *gs) {
    if (gs) {
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                gs->num_guards * sizeof(MVMSpeshPluginGuard), gs->guards);
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                sizeof(MVMSpeshPluginGuardSet), gs);
    }
}

/* Schedules replaced state to be freed. */
void free_dead_state(MVMThreadContext *tc, MVMSpeshPluginState *ps) {
    if (ps) {
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                ps->num_positions * sizeof(MVMSpeshPluginPosition), ps->positions);
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                sizeof(MVMSpeshPluginState), ps);
    }
}

/* Data for use with special return mechanism to store the result of a
 * resolution. */
typedef struct {
    MVMRegister *result;
    MVMuint32 position;
} MVMSpeshPluginSpecialReturnData;

/* Callback after a resolver to update the guard structure. */
static void add_resolution_to_guard_set(MVMThreadContext *tc, void *sr_data) {
    MVMSpeshPluginSpecialReturnData *srd = (MVMSpeshPluginSpecialReturnData *)sr_data;
    MVMStaticFrame *cur_sf = tc->cur_frame->static_info;
    MVMSpeshPluginState *base_state = get_plugin_state(tc, cur_sf);
    MVMSpeshPluginGuardSet *base_guards = guard_set_for_position(tc, srd->position, base_state);
    MVMSpeshPluginGuardSet *new_guards = append_guard(tc, base_guards, srd->result->o,
            cur_sf->body.spesh);
    if (new_guards != base_guards) {
        MVMSpeshPluginState *new_state = updated_state(tc, base_state, srd->position,
                base_guards, new_guards);
        MVMuint32 committed = MVM_trycas(
                &tc->cur_frame->static_info->body.spesh->body.plugin_state,
                base_state, new_state);
        if (committed) {
            free_dead_guards(tc, base_guards);
            free_dead_state(tc, base_state);
        }
        else {
            /* Lost update race. Discard; a retry will happen naturally. */
            free_dead_guards(tc, new_guards);
            free_dead_state(tc, new_state);
        }
    }

    /* Clear up recording state. */
    MVM_fixed_size_free(tc, tc->instance->fsa,
            MVM_SPESH_PLUGIN_GUARD_LIMIT * sizeof(MVMSpeshPluginGuard),
            tc->plugin_guards);
    tc->plugin_guards = NULL;
    tc->plugin_guard_args = NULL;

    MVM_free(srd);
}

/* When we throw an exception when evaluating a resolver, then clean up any
 * recorded guards so as not to leak memory. */
static void cleanup_recorded_guards(MVMThreadContext *tc, void *sr_data) {
    MVM_fixed_size_free(tc, tc->instance->fsa,
            MVM_SPESH_PLUGIN_GUARD_LIMIT * sizeof(MVMSpeshPluginGuard),
            tc->plugin_guards);
    tc->plugin_guards = NULL;
    tc->plugin_guard_args = NULL;
    MVM_free(sr_data);
}

/* Sets up state in the current thread context for recording guards. */
static void setup_for_guard_recording(MVMThreadContext *tc, MVMCallsite *callsite) {
    /* Validate the callsite meets restrictions. */
    MVMuint32 i = 0;
    if (tc->plugin_guards)
        MVM_exception_throw_adhoc(tc, "Recursive spesh plugin setup not yet implemented");
    if (callsite->num_pos != callsite->flag_count)
        MVM_exception_throw_adhoc(tc, "A spesh plugin must have only positional args");
    if (callsite->has_flattening)
        MVM_exception_throw_adhoc(tc, "A spesh plugin must not have flattening args");
    for (i = 0; i < callsite->flag_count; i++)
        if (callsite->arg_flags[i] != MVM_CALLSITE_ARG_OBJ)
            MVM_exception_throw_adhoc(tc, "A spesh plugin must only be passed object args");

    /* Set up guard recording space and arguments array. */
    tc->plugin_guards = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            MVM_SPESH_PLUGIN_GUARD_LIMIT * sizeof(MVMSpeshPluginGuard));
    tc->num_plugin_guards = 0;
    tc->plugin_guard_args = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    for (i = 0; i < callsite->flag_count; i++)
        MVM_repr_push_o(tc, tc->plugin_guard_args, tc->cur_frame->args[i].o);
}

/* Resolves a spesh plugin for the current HLL. */
void MVM_spesh_plugin_resolve(MVMThreadContext *tc, MVMString *name,
                              MVMRegister *result, MVMuint8 *op_addr,
                              MVMuint8 *next_addr, MVMCallsite *callsite) {
    MVMuint32 position = (MVMuint32)(op_addr - *tc->interp_bytecode_start);
    MVMObject *resolved;
    MVMuint16 guard_offset;
    MVMROOT(tc, name, {
        resolved = resolve_using_guards(tc, position, callsite, &guard_offset);
    });
    if (resolved) {
        /* Resolution through guard tree successful, so no invoke needed. */
        result->o = resolved;
        *tc->interp_cur_op = next_addr;
        if (MVM_spesh_log_is_logging(tc))
            MVM_spesh_log_plugin_resolution(tc, position, guard_offset);
    }
    else {
        /* No pre-resolved value, so we need to run the plugin. Find it. */
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

        /* Set up the guard state to record into. */
        MVMROOT(tc, plugin, {
            setup_for_guard_recording(tc, callsite);
        });

        /* Run it, registering handlers to save or discard guards and result. */
        tc->cur_frame->return_value = result;
        tc->cur_frame->return_type = MVM_RETURN_OBJ;
        tc->cur_frame->return_address = next_addr;
        srd = MVM_malloc(sizeof(MVMSpeshPluginSpecialReturnData));
        srd->result = result;
        srd->position = position;
        MVM_frame_special_return(tc, tc->cur_frame, add_resolution_to_guard_set,
                cleanup_recorded_guards, srd, NULL);
        STABLE(plugin)->invoke(tc, plugin, callsite, tc->cur_frame->args);
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
            MVM_exception_throw_adhoc(tc, "Too many guards recorded by spesh plugin");
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
void MVM_spesh_plugin_rewrite_resolve(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                      MVMSpeshIns *ins, MVMuint32 bytecode_offset,
                                      MVMint32 guard_index) {
    /* Resolve guard set (should never be missing, but fail softly if so). */
    MVMSpeshPluginState *ps = get_plugin_state(tc, g->sf);
    MVMSpeshPluginGuardSet *gs = guard_set_for_position(tc, bytecode_offset, ps);
    if (gs) {
        /* Collect registers that go with each argument, and delete the arg
         * and prepargs instructions. */
        MVMuint32 arg_regs_length;
        MVMSpeshOperand *arg_regs = arg_ins_to_reg_list(tc, g, bb, ins, &arg_regs_length);
        MVMuint32 deopt_to = bytecode_offset - (
                6 * arg_regs_length +   /* The bytes the args instructions use */
                4);                     /* The bytes of the prepargs instruction */

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

        /* Prepend guards. */
        while (++guards_start <= guards_end) {
            MVMSpeshPluginGuard *guard = &(gs->guards[guards_start]);
            switch (guard->kind) {
                case MVM_SPESH_PLUGIN_GUARD_OBJ: {
                    MVMSpeshIns *guard_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                    guard_ins->info = MVM_op_get_op(MVM_OP_sp_guardobj);
                    guard_ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                    guard_ins->operands[0] = arg_regs[guard->test_idx];
                    MVM_spesh_get_facts(tc, g, arg_regs[guard->test_idx])->usages++;
                    guard_ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                            (MVMCollectable *)guard->u.object);
                    guard_ins->operands[2].lit_ui32 = deopt_to;
                    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard_ins);
                    break;
                }
                case MVM_SPESH_PLUGIN_GUARD_TYPE: {
                    MVMSpeshIns *guard_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                    guard_ins->info = MVM_op_get_op(MVM_OP_sp_guard);
                    guard_ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                    guard_ins->operands[0] = arg_regs[guard->test_idx];
                    MVM_spesh_get_facts(tc, g, arg_regs[guard->test_idx])->usages++;
                    guard_ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
                            (MVMCollectable *)guard->u.type);
                    guard_ins->operands[2].lit_ui32 = deopt_to;
                    MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, guard_ins);
                    break;
                }
                default:
                    MVM_panic(1, "Unknown spesh plugin guard kind %d to insert during specialization",
                            guard->kind);
            }
        }
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

/* Called from the GC to mark the spesh plugin state. */
void MVM_spesh_plugin_state_mark(MVMThreadContext *tc, MVMSpeshPluginState *ps,
                                 MVMGCWorklist *worklist) {
    if (ps) {
        MVMuint32 i;
        for (i = 0; i < ps->num_positions; i++) {
            MVMSpeshPluginGuardSet *gs = ps->positions[i].guard_set;
            MVM_spesh_plugin_guard_list_mark(tc, gs->guards, gs->num_guards, worklist);
        }
    }
}

/* Called from the GC when the spesh plugin state should be freed. This means
 * that it is no longer reachable, meaning the static frame and its bytecode
 * also went away. Thus a simple free will suffice. */
void MVM_spesh_plugin_state_free(MVMThreadContext *tc, MVMSpeshPluginState *ps) {
    if (ps) {
        MVMuint32 i;
        for (i = 0; i < ps->num_positions; i++) {
            MVM_fixed_size_free(tc, tc->instance->fsa,
                    ps->positions[i].guard_set->num_guards * sizeof(MVMSpeshPluginGuard),
                    ps->positions[i].guard_set->guards);
            MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMSpeshPluginGuardSet),
                    ps->positions[i].guard_set);
        }
        MVM_fixed_size_free(tc, tc->instance->fsa,
                ps->num_positions * sizeof(MVMSpeshPluginPosition),
                ps->positions);
        MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMSpeshPluginState), ps);
    }
}
