#include "moar.h"

/* Calculates the maxium number of new nodes that might be needed to add a
 * guard for the specified callsite and types. (It may be less in reality
 * due to head sharing.) */
static size_t max_new_nodes(MVMCallsite *cs, MVMSpeshStatsType *types) {
    size_t needed = 2; /* One for callsite, one for result */
    MVMuint32 i;
    for (i = 0; i < cs->flag_count; i++) {
        if (cs->arg_flags[i] & MVM_CALLSITE_ARG_OBJ) {
            if (types[i].type)
                needed += 2; /* One to read arg, one to check */
            if (types[i].rw_cont)
                needed++;
            if (types[i].decont_type)
                needed += 2; /* One to decont, one to check */
        }
    }
    return needed + 1;
}

/* Allocates a new set of spesh argument guards extended by the extras amount
 * of nodes specified. Copies the original argument guards into it. */
MVMSpeshArgGuard * copy_and_extend(MVMThreadContext *tc, MVMSpeshArgGuard *orig, size_t extra) {
    size_t orig_nodes = orig ? orig->used_nodes : 0;
    size_t total_nodes = orig_nodes + extra;
    size_t node_size = total_nodes * sizeof(MVMSpeshArgGuardNode);
    size_t size = sizeof(MVMSpeshArgGuard) + node_size;
    MVMSpeshArgGuard *copy = MVM_fixed_size_alloc(tc, tc->instance->fsa, size);
    copy->nodes = (MVMSpeshArgGuardNode *)((char *)copy + sizeof(MVMSpeshArgGuard));
    copy->used_nodes = orig_nodes;
    copy->num_nodes = total_nodes;
    if (orig_nodes)
        memcpy(copy->nodes, orig->nodes, orig_nodes * sizeof(MVMSpeshArgGuardNode));
    return copy;
}

/* Locates an existing node that matches a particular callsite. If there is
 * no such node, adds it. */
MVMuint32 get_callsite_node(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMCallsite *cs) {
    MVMuint32 have_fixup_node = 0;
    MVMuint32 fixup_node;
    if (ag->used_nodes) {
        MVMuint32 current_node = 0;
        do {
            MVMSpeshArgGuardNode *agn = &(ag->nodes[current_node]);
            if (agn->op == MVM_SPESH_GUARD_OP_CALLSITE) {
                /* If it matches, we've found it. */
                if (agn->cs == cs)
                    return current_node;

                /* Otherwise, treat this as the working fixup node, and take
                 * the no branch. */
                fixup_node = current_node;
                have_fixup_node = 1;
                current_node = agn->no;
            }
            else {
                /* We only expect callsite nodes at the top level. */
                MVM_panic(1, "Spesh arg guard: unexpected callsite structure in tree");
            }
        } while (current_node != 0);
    }

    /* If we get here, we need to add a node for this callsite. */
    ag->nodes[ag->used_nodes].op = MVM_SPESH_GUARD_OP_CALLSITE;
    ag->nodes[ag->used_nodes].cs = cs;
    ag->nodes[ag->used_nodes].yes = 0;
    ag->nodes[ag->used_nodes].no = 0;
    if (have_fixup_node)
        ag->nodes[fixup_node].no = ag->used_nodes;
    return ag->used_nodes++;
}

/* Resolves or inserts the argument load node. This is a little complex, in
 * that we may (though it should be quite unusual) have multiple starting
 * points in the argument list to consider. For example, there may be for
 * ($obj, $obj) specializations of (Foo, <no guard>) and (<no guard>, Foo).
 * In that case, we tweak the previous tree(s) of other starting points so
 * any "no result" points to instead try the added subtree. */
MVMuint32 get_load_node(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMuint32 base_node,
                        MVMuint16 arg_idx) {
    if (ag->nodes[base_node].yes) {
        MVMuint32 check_node = ag->nodes[base_node].yes;
        if (ag->nodes[check_node].op == MVM_SPESH_GUARD_OP_LOAD_ARG) {
            if (ag->nodes[check_node].arg_index == arg_idx)
                return check_node;
            MVM_panic(1, "Spesh arg guard: unimplemented spare guard case");
        }
        else {
            MVM_panic(1, "Spesh arg guard: unexpected op in get_load_node");
        }
    }

    /* If we get here, need to add a new load node. */
    ag->nodes[ag->used_nodes].op = MVM_SPESH_GUARD_OP_LOAD_ARG;
    ag->nodes[ag->used_nodes].arg_index = arg_idx;
    ag->nodes[ag->used_nodes].yes = 0;
    ag->nodes[ag->used_nodes].no = 0;
    ag->nodes[base_node].yes = ag->used_nodes;
    return ag->used_nodes++;
}

/* Resolves or inserts a guard for the specified type information, rooted off
 * the given node. */
MVMuint32 get_type_node(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMuint32 base_node,
                        MVMSpeshStatsType *type, MVMuint16 arg_idx) {
    MVMuint32 current_node = get_load_node(tc, ag, base_node, arg_idx);
    /* TODO chain type checks */
    return current_node;
}

/* Inserts a guard for the specified types into the tree. */
void add_guard(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMCallsite *cs,
               MVMSpeshStatsType *types, MVMuint32 candidate) {
    MVMuint32 current_node = get_callsite_node(tc, ag, cs);
    MVMuint16 arg_idx = 0;
    MVMuint16 i;
    for (i = 0; i < cs->flag_count; i++) {
        if (cs->arg_flags[i] & MVM_CALLSITE_ARG_NAMED)
            arg_idx++; /* Skip over name */
        if (cs->arg_flags[i] & MVM_CALLSITE_ARG_OBJ) {
            MVMSpeshStatsType *type = &(types[i]);
            if (type->type)
                current_node = get_type_node(tc, ag, current_node, type, arg_idx);
        }
        arg_idx++;
    }
    ag->nodes[ag->used_nodes].op = MVM_SPESH_GUARD_OP_RESULT;
    ag->nodes[ag->used_nodes].result = candidate;
    ag->nodes[ag->used_nodes].yes = 0;
    ag->nodes[ag->used_nodes].no = 0;
    ag->nodes[current_node].yes = ag->used_nodes++;
}

/* Takes a pointer to a guard set. Replaces it with a guard set that also
 * includes a guard for the specified type tuple (passed with callsite to
 * know how many types are involved), and resolving to the specified spesh
 * candidate index. Any previous guard set will be scheduled for freeing at
 * the next safepoint. */
void MVM_spesh_arg_guard_add(MVMThreadContext *tc, MVMSpeshArgGuard **orig,
                             MVMCallsite *cs, MVMSpeshStatsType *types,
                             MVMuint32 candidate) {
    MVMSpeshArgGuard *new_guard = copy_and_extend(tc, *orig, max_new_nodes(cs, types));
    add_guard(tc, new_guard, cs, types, candidate);
    if (*orig) {
        MVMSpeshArgGuard *prev = *orig;
        *orig = new_guard;
        MVM_spesh_arg_guard_destroy(tc, prev, 1);
    }
    else {
        *orig = new_guard;
    }
}

/* Marks any objects held by an argument guard. */
void MVM_spesh_arg_guard_gc_mark(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                 MVMGCWorklist *worklist) {
    if (ag) {
        MVMuint32 i;
        for (i = 0; i < ag->used_nodes; i++) {
            switch (ag->nodes[i].op) {
                case MVM_SPESH_GUARD_OP_STABLE_CONC:
                case MVM_SPESH_GUARD_OP_STABLE_TYPE:
                    MVM_gc_worklist_add(tc, worklist, &(ag->nodes[i].st));
                    break;
            }
        }
    }
}

/* Frees the memory associated with an argument guard. If `safe` is set to a
 * non-zero value then the memory is freed at the next safepoint. If it is set
 * to zero, the memory is freed immediately. */
void MVM_spesh_arg_guard_destroy(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMuint32 safe) {
    if (ag) {
        size_t total_size = sizeof(MVMSpeshArgGuard) +
            ag->num_nodes * sizeof(MVMSpeshArgGuardNode);
        if (safe)
            MVM_fixed_size_free(tc, tc->instance->fsa, total_size, ag);
        else
            MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa, total_size, ag);
    }
}
