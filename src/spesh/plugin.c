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
        MVMuint32 l = 0;
        MVMuint32 r = ps->num_positions - 1;
        while (l <= r) {
            MVMuint32 m = l + (r - l) / 2;
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
static MVMObject * evaluate_guards(MVMThreadContext *tc, MVMSpeshPluginGuardSet *gs) {
    MVMuint32 pos = 0;
    MVMuint32 end = gs->num_guards;
    while (pos < end) {
        switch (gs->guards[pos].kind) {
            case MVM_SPESH_PLUGIN_GUARD_RESULT:
                return gs->guards[pos].u.result;
            default:
                MVM_panic(1, "Guard kind NYI");
        }
    }
    return NULL;
}

/* Tries to resolve a plugin by looking at the guards for the position. */
static MVMObject * resolve_using_guards(MVMThreadContext *tc, MVMuint32 cur_position) {
    MVMSpeshPluginState *ps = get_plugin_state(tc, tc->cur_frame->static_info);
    MVMSpeshPluginGuardSet *gs = guard_set_for_position(tc, cur_position, ps);
    return gs ? evaluate_guards(tc, gs) : NULL;
}

/* Produces an updated guard set with the given resolution result. Returns
 * the base guard set if we already having a matching guard (which means two
 * threads raced to do this resolution). */
/* TODO Update this when we actually have guards recorded etc. */
static MVMSpeshPluginGuardSet * append_guard(MVMThreadContext *tc,
        MVMSpeshPluginGuardSet *base_guards, MVMObject *resolved,
        MVMStaticFrameSpesh *barrier) {
    MVMSpeshPluginGuardSet *result;
    if (!base_guards) {
        result = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(MVMSpeshPluginGuardSet));
        result->num_guards = 1;
        result->guards = MVM_fixed_size_alloc(tc, tc->instance->fsa,
                sizeof(MVMSpeshPluginGuard) * result->num_guards);
        result->guards[0].kind = MVM_SPESH_PLUGIN_GUARD_RESULT;
        MVM_ASSIGN_REF(tc, &(barrier->common.header), result->guards[0].u.result, resolved);
    }
    else {
        result = base_guards;
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
    MVM_free(srd);
}

/* When we throw an exception when evaluating a resolver, then clean up any
 * recorded guards so as not to leak memory. */
static void cleanup_recorded_guards(MVMThreadContext *tc, void *sr_data) {
    MVM_panic(1, "NYI");
}

/* Resolves a spesh plugin for the current HLL. */
void MVM_spesh_plugin_resolve(MVMThreadContext *tc, MVMString *name,
                              MVMRegister *result, MVMuint8 *op_addr,
                              MVMuint8 *next_addr, MVMCallsite *callsite) {
    MVMuint32 position = (MVMuint32)(op_addr - *tc->interp_bytecode_start);
    MVMObject *resolved = resolve_using_guards(tc, position);
    if (resolved) {
        /* Resolution through guard tree successful, so no invoke needed. */
        result->o = resolved;
        *tc->interp_cur_op = next_addr;
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

/* Adds a guard that the guardee must have exactly the specified type. Will
 * throw if we are not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_type(MVMThreadContext *tc, MVMObject *guardee, MVMObject *type) {
}

/* Adds a guard that the guardee must be concrete. Will throw if we are not
 * currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_concrete(MVMThreadContext *tc, MVMObject *guardee) {
}

/* Adds a guard that the guardee must not be concrete. Will throw if we are
 * not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_typeobj(MVMThreadContext *tc, MVMObject *guardee) {
}

/* Adds a guard that the guardee must exactly match the provided object
 * literal. Will throw if we are not currently inside of a spesh plugin. */
void MVM_spesh_plugin_addguard_obj(MVMThreadContext *tc, MVMObject *guardee) {
}

/* Gets an attribute and adds that object to the set of objects that we may
 * guard against. Will throw if we are not currently inside of a spesh
 * plugin. */
MVMObject * MVM_spesh_plugin_addguard_getattr(MVMThreadContext *tc, MVMObject *guardee,
            MVMObject *class_handle, MVMString *name) {
    return MVM_repr_get_attr_o(tc, guardee, class_handle, name, MVM_NO_HINT);
}

/* Called from the GC to mark the spesh plugin state. */
void MVM_spesh_plugin_state_mark(MVMThreadContext *tc, MVMSpeshPluginState *ps,
                                 MVMGCWorklist *worklist) {
    if (ps) {
        MVMuint32 i, j;
        for (i = 0; i < ps->num_positions; i++) {
            MVMSpeshPluginGuardSet *gs = ps->positions[i].guard_set;
            for (j = 0; j < gs->num_guards; j++) {
                switch (gs->guards[j].kind) {
                    case MVM_SPESH_PLUGIN_GUARD_RESULT:
                        MVM_gc_worklist_add(tc, worklist, &gs->guards[j].u.result);
                        break;
                    case MVM_SPESH_PLUGIN_GUARD_OBJ:
                        MVM_gc_worklist_add(tc, worklist, &gs->guards[j].u.object);
                        break;
                    case MVM_SPESH_PLUGIN_GUARD_TYPE:
                        MVM_gc_worklist_add(tc, worklist, &gs->guards[j].u.type);
                        break;
                    case MVM_SPESH_PLUGIN_GUARD_GETATTR:
                        MVM_gc_worklist_add(tc, worklist, &gs->guards[j].u.attr.class_handle);
                        MVM_gc_worklist_add(tc, worklist, &gs->guards[j].u.attr.name);
                        break;
                }
            }
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
