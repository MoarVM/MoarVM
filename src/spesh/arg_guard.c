#include "moar.h"

/* Calculates the maxium number of new nodes that might be needed to add a
 * guard for the specified callsite and types. (It may be less in reality
 * due to head sharing.) */
static size_t max_new_nodes(MVMCallsite *cs, MVMSpeshStatsType *types) {
    size_t needed = 2; /* One for callsite, one for result */
    if (types) {
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
    }
    return needed + 1;
}

/* Allocates a new set of spesh argument guards extended by the extras amount
 * of nodes specified. Copies the original argument guards into it. */
static MVMSpeshArgGuard * copy_and_extend(MVMThreadContext *tc, MVMSpeshArgGuard *orig,
                                          size_t extra) {
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
static MVMuint32 get_callsite_node(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMCallsite *cs) {
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
static MVMuint32 get_load_node(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMuint32 base_node,
                               MVMuint16 arg_idx) {
    MVMuint16 new_no = 0;
    if (ag->nodes[base_node].yes) {
        MVMuint32 check_node = ag->nodes[base_node].yes;
        MVMSpeshArgGuardOp op = ag->nodes[check_node].op;
        if (op == MVM_SPESH_GUARD_OP_LOAD_ARG) {
            if (ag->nodes[check_node].arg_index == arg_idx)
                return check_node;
            MVM_panic(1, "Spesh arg guard: unimplemented sparse guard case");
        }
        else if (op == MVM_SPESH_GUARD_OP_RESULT) {
            new_no = check_node;
        }
        else {
            MVM_panic(1, "Spesh arg guard: unexpected op %d in get_load_node", op);
        }
    }

    /* If we get here, need to add a new load node. */
    ag->nodes[ag->used_nodes].op = MVM_SPESH_GUARD_OP_LOAD_ARG;
    ag->nodes[ag->used_nodes].arg_index = arg_idx;
    ag->nodes[ag->used_nodes].yes = 0;
    ag->nodes[ag->used_nodes].no = new_no;
    ag->nodes[base_node].yes = ag->used_nodes;
    return ag->used_nodes++;
}

/* Resolves or inserts a node for testing the curernt type loaded into the
 * test buffer. If it needs to insert a new node, it chains it on to the
 * end of the existing set of type tests. */
static MVMuint32 get_type_check_node(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                     MVMuint32 base_node, MVMObject *type, MVMuint8 concrete) {
    MVMuint32 current_node = ag->nodes[base_node].yes;
    MVMuint32 have_fixup_node = 0;
    MVMuint32 fixup_node;
    while (current_node != 0) {
        MVMSpeshArgGuardNode *agn = &(ag->nodes[current_node]);
        if (agn->op == MVM_SPESH_GUARD_OP_STABLE_CONC) {
            /* If it matches, we've found it. */
            if (concrete && agn->st == type->st)
                return current_node;

             /* Otherwise, treat this as the working fixup node, and take
             * the no branch. */
            fixup_node = current_node;
            have_fixup_node = 1;
            current_node = agn->no;
        }
        else if (agn->op == MVM_SPESH_GUARD_OP_STABLE_TYPE) {
            /* If it matches, we've found it. */
            if (!concrete && agn->st == type->st)
                return current_node;

             /* Otherwise, treat this as the working fixup node, and take
             * the no branch. */
            fixup_node = current_node;
            have_fixup_node = 1;
            current_node = agn->no;
        }
        else {
            /* We only expect type matching nodes at the top level. */
            MVM_panic(1, "Spesh arg guard: unexpected type structure in tree");
        }
    }

    /* If we get here, we need to add a node for this callsite. */
    ag->nodes[ag->used_nodes].op = concrete
        ? MVM_SPESH_GUARD_OP_STABLE_CONC
        : MVM_SPESH_GUARD_OP_STABLE_TYPE;
    ag->nodes[ag->used_nodes].st = type->st;
    ag->nodes[ag->used_nodes].yes = 0;
    ag->nodes[ag->used_nodes].no = 0;
    if (have_fixup_node)
        ag->nodes[fixup_node].no = ag->used_nodes;
    else
        ag->nodes[base_node].yes = ag->used_nodes;
    return ag->used_nodes++;
}

/* Resolves or inserts a guard for "is this an rw container" hanging off the
 * specified base node. We will always have the rw-or-not check right after
 * the container type check, so if there's already a "yes" branch off the base
 * node that is not an rw container check, we'll add the rw container check in
 * its place and attach the "no" branch to where it used to point. This means
 * we can know if there is such a node for this container type by just looking
 * at the "yes" branch of the base node we are passed. */
static MVMuint32 get_rw_cont_node(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                  MVMuint32 base_node) {
    MVMuint32 yes_node = ag->nodes[base_node].yes;
    if (yes_node && ag->nodes[yes_node].op == MVM_SPESH_GUARD_OP_DEREF_RW)
        return yes_node;
    ag->nodes[ag->used_nodes].op = MVM_SPESH_GUARD_OP_DEREF_RW;
    ag->nodes[ag->used_nodes].offset = 0; /* TODO populate this properly */
    ag->nodes[ag->used_nodes].yes = 0;
    ag->nodes[ag->used_nodes].no = yes_node;
    ag->nodes[base_node].yes = ag->used_nodes;
    return ag->used_nodes++;
}

/* Resolves or inserts a guard op that decontainerizes the current test
 * register content. We only do this once before a possible chain of nodes
 * that test the decontainerized type. Therefore, we can expect that such a
 * node is already in the tree at this point, *or* that there is an RW
 * guard node and *then* the one we're looking for. */
static MVMuint32 get_decont_node(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                 MVMuint32 base_node) {
    MVMuint32 check_node = ag->nodes[base_node].yes;
    MVMuint32 update_no_node = 0;
    if (check_node) {
        if (ag->nodes[check_node].op == MVM_SPESH_GUARD_OP_DEREF_VALUE) {
            return check_node;
        }
        else if (ag->nodes[check_node].op == MVM_SPESH_GUARD_OP_DEREF_RW) {
            MVMuint32 no_node = ag->nodes[check_node].no;
            if (no_node) {
                if (ag->nodes[no_node].op == MVM_SPESH_GUARD_OP_DEREF_VALUE)
                    return no_node;
            }
            else {
                update_no_node = check_node;
            }
        }
        if (!update_no_node)
            MVM_panic(1, "Spesh arg guard: unexpected tree structure adding deref value");
    }
    ag->nodes[ag->used_nodes].op = MVM_SPESH_GUARD_OP_DEREF_VALUE;
    ag->nodes[ag->used_nodes].offset = 0; /* TODO populate this properly */
    ag->nodes[ag->used_nodes].yes = 0;
    ag->nodes[ag->used_nodes].no = 0;
    if (update_no_node)
        ag->nodes[update_no_node].no = ag->used_nodes;
    else
        ag->nodes[base_node].yes = ag->used_nodes;
    return ag->used_nodes++;
}

/* Resolves or inserts guards for the specified type information, rooted off
 * the given node. */
static MVMuint32 get_type_node(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMuint32 base_node,
                               MVMSpeshStatsType *type, MVMuint16 arg_idx) {
    MVMuint32 current_node = get_load_node(tc, ag, base_node, arg_idx);
    current_node = get_type_check_node(tc, ag, current_node, type->type, type->type_concrete);
    if (type->rw_cont)
        current_node = get_rw_cont_node(tc, ag, current_node);
    if (type->decont_type) {
        current_node = get_decont_node(tc, ag, current_node);
        current_node = get_type_check_node(tc, ag, current_node, type->decont_type,
            type->decont_type_concrete);
    }
    return current_node;
}

/* Inserts a guard for the specified types into the tree. */
static MVMint32 try_add_guard(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMCallsite *cs,
                              MVMSpeshStatsType *types, MVMuint32 candidate) {
    MVMuint32 current_node = get_callsite_node(tc, ag, cs);
    if (types) {
        /* We're adding a type-based result, and thus for a speculative
         * specialization. Certain specializations come ahead of those, and
         * hang off the callsite node; skip over any such node. */
        MVMuint16 arg_idx = 0;
        MVMuint16 i;
        if (ag->nodes[ag->nodes[current_node].yes].op == MVM_SPESH_GUARD_OP_CERTAIN_RESULT)
            current_node = ag->nodes[current_node].yes;
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
        if (ag->nodes[current_node].yes)
            return 0;
        ag->nodes[ag->used_nodes].op = MVM_SPESH_GUARD_OP_RESULT;
        ag->nodes[ag->used_nodes].yes = 0;
        ag->nodes[ag->used_nodes].no = 0;
    }
    else {
        /* We're adding a certain result. If there already is such a node, we
         * already have that specialization. Otherwise, we need to insert it
         * and redirect the current_node's .yes to point to it, and it to
         * point to whatever current_node's .yes used to point to (so it goes
         * in ahead of type guards etc.). */
        if (ag->nodes[ag->nodes[current_node].yes].op == MVM_SPESH_GUARD_OP_CERTAIN_RESULT)
            return 0;
        ag->nodes[ag->used_nodes].op = MVM_SPESH_GUARD_OP_CERTAIN_RESULT;
        ag->nodes[ag->used_nodes].yes = ag->nodes[current_node].yes;
        ag->nodes[ag->used_nodes].no = 0;
    }
    ag->nodes[ag->used_nodes].result = candidate;
    ag->nodes[current_node].yes = ag->used_nodes++;
    return 1;
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
    if (!try_add_guard(tc, new_guard, cs, types, candidate))
        MVM_panic(1, "Spesh arg guard: trying to add duplicate result for same guard");
    if (*orig) {
        MVMSpeshArgGuard *prev = *orig;
        *orig = new_guard;
        MVM_spesh_arg_guard_destroy(tc, prev, 1);
    }
    else {
        *orig = new_guard;
    }
}

/* Checks if we already have a guard that precisely matches the specified
 * pair of callsite and type tuple. This is a more exact check that "would
 * the guard match", since a less precise specialization would match if we
 * just ran the guard tree against the arguments. This answers the question of
 * "if I added this, would it collide with an existing entry" instead. */
MVMint32 MVM_spesh_arg_guard_exists(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                    MVMCallsite *cs, MVMSpeshStatsType *types) {
    MVMSpeshArgGuard *try_guard = copy_and_extend(tc, ag, max_new_nodes(cs, types));
    MVMint32 exists = !try_add_guard(tc, try_guard, cs, types, 0);
    MVM_spesh_arg_guard_destroy(tc, try_guard, 0);
    return exists;
}

/* Runs the guard against a type tuple, which is used primarily for detecting
 * if an existing specialization already exists. Returns the index of that
 * specialization, or -1 if there is no match. */
MVMint32 MVM_spesh_arg_guard_run_types(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                        MVMCallsite *cs, MVMSpeshStatsType *types) {
    MVMuint32 current_node = 0;
    MVMSpeshStatsType *test = NULL;
    MVMuint32 use_decont_type = 0;
    MVMint32 current_result = -1;
    if (!ag)
        return -1;
    do {
        MVMSpeshArgGuardNode *agn = &(ag->nodes[current_node]);
        switch (agn->op) {
            case MVM_SPESH_GUARD_OP_CALLSITE:
                current_node = agn->cs == cs ? agn->yes : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_LOAD_ARG: {
                test = &(types[agn->arg_index < cs->num_pos
                    ? agn->arg_index
                    : cs->num_pos + (((agn->arg_index - 1) - cs->num_pos) / 2)]);
                use_decont_type = 0;
                current_node = agn->yes;
                break;
            case MVM_SPESH_GUARD_OP_STABLE_CONC:
                if (use_decont_type)
                    current_node = test->decont_type_concrete && test->decont_type &&
                            test->decont_type->st == agn->st
                        ? agn->yes
                        : agn->no;
                else
                    current_node = test->type_concrete && test->type &&
                            test->type->st == agn->st
                        ? agn->yes
                        : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_STABLE_TYPE:
                if (use_decont_type)
                    current_node = !test->decont_type_concrete && test->decont_type &&
                            test->decont_type->st == agn->st
                        ? agn->yes
                        : agn->no;
                else
                    current_node = !test->type_concrete && test->type &&
                            test->type->st == agn->st
                        ? agn->yes
                        : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_DEREF_VALUE:
                if (test->decont_type) {
                    use_decont_type = 1;
                    current_node = agn->yes;
                }
                else {
                    current_node = agn->no;
                }
                break;
            }
            case MVM_SPESH_GUARD_OP_DEREF_RW:
                current_node = test->rw_cont
                    ? agn->yes
                    : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_CERTAIN_RESULT:
                current_result = agn->result;
                current_node = agn->yes;
                break;
            case MVM_SPESH_GUARD_OP_RESULT:
                return agn->result;
        }
    } while (current_node != 0);
    return current_result;
}

/* Evaluates the argument guards. Returns >= 0 if there is a matching spesh
 * candidate, or -1 if there is not. */
MVMint32 MVM_spesh_arg_guard_run(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                 MVMCallsite *cs, MVMRegister *args,
                                 MVMint32 *certain) {
    MVMuint32 current_node = 0;
    MVMObject *test = NULL;
    MVMint32 current_result = -1;
    if (!ag)
        return -1;
    do {
        MVMSpeshArgGuardNode *agn = &(ag->nodes[current_node]);
        switch (agn->op) {
            case MVM_SPESH_GUARD_OP_CALLSITE:
                current_node = agn->cs == cs ? agn->yes : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_LOAD_ARG:
                test = args[agn->arg_index].o;
                current_node = agn->yes;
                break;
            case MVM_SPESH_GUARD_OP_STABLE_CONC:
                current_node = IS_CONCRETE(test) && test->st == agn->st
                    ? agn->yes
                    : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_STABLE_TYPE:
                current_node = !IS_CONCRETE(test) && test->st == agn->st
                    ? agn->yes
                    : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_DEREF_VALUE: {
                /* TODO Use offset approach later to avoid these calls. */
                MVMRegister dc;
                test->st->container_spec->fetch(tc, test, &dc);
                test = dc.o;
                current_node = test ? agn->yes : agn->no;
                break;
            }
            case MVM_SPESH_GUARD_OP_DEREF_RW:
                /* TODO Use offset approach later to avoid these calls. */
                current_node = STABLE(test)->container_spec->can_store(tc, test)
                    ? agn->yes
                    : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_CERTAIN_RESULT:
                current_result = agn->result;
                if (certain)
                    *certain = agn->result;
                current_node = agn->yes;
                break;
            case MVM_SPESH_GUARD_OP_RESULT:
                return agn->result;
        }
    } while (current_node != 0);
    return current_result;
}

/* Runs the guards using call information gathered by the optimizer. This is
 * used for finding existing candidates to emit fast calls to or inline. */
MVMint32 MVM_spesh_arg_guard_run_callinfo(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                          MVMSpeshCallInfo *arg_info) {
    MVMuint32 current_node = 0;
    MVMSpeshFacts *facts = NULL;
    MVMuint8 use_decont_facts = 0;
    MVMint32 current_result = -1;
    if (!ag)
        return -1;
    do {
        MVMSpeshArgGuardNode *agn = &(ag->nodes[current_node]);
        switch (agn->op) {
            case MVM_SPESH_GUARD_OP_CALLSITE:
                current_node = agn->cs == arg_info->cs ? agn->yes : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_LOAD_ARG:
                if (agn->arg_index >= MAX_ARGS_FOR_OPT)
                    return -1;
                facts = arg_info->arg_facts[agn->arg_index];
                use_decont_facts = 0;
                current_node = agn->yes;
                break;
            case MVM_SPESH_GUARD_OP_STABLE_CONC:
                if (use_decont_facts) {
                    current_node = facts->flags & MVM_SPESH_FACT_DECONT_CONCRETE &&
                            facts->flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE &&
                            facts->decont_type->st == agn->st
                        ? agn->yes
                        : agn->no;
                }
                else {
                    current_node = facts->flags & MVM_SPESH_FACT_CONCRETE &&
                            facts->flags & MVM_SPESH_FACT_KNOWN_TYPE &&
                            facts->type->st == agn->st
                        ? agn->yes
                        : agn->no;
                }
                break;
            case MVM_SPESH_GUARD_OP_STABLE_TYPE:
                if (use_decont_facts) {
                    current_node = facts->flags & MVM_SPESH_FACT_DECONT_TYPEOBJ &&
                            facts->flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE &&
                            facts->decont_type->st == agn->st
                        ? agn->yes
                        : agn->no;
                }
                else {
                    current_node = facts->flags & MVM_SPESH_FACT_TYPEOBJ &&
                            facts->flags & MVM_SPESH_FACT_KNOWN_TYPE &&
                            facts->type->st == agn->st
                        ? agn->yes
                        : agn->no;
                }
                break;
            case MVM_SPESH_GUARD_OP_DEREF_VALUE: {
                if (facts->flags & MVM_SPESH_FACT_KNOWN_DECONT_TYPE) {
                    use_decont_facts = 1;
                    current_node = agn->yes;
                }
                else {
                    current_node = agn->no;
                }
                break;
            }
            case MVM_SPESH_GUARD_OP_DEREF_RW:
                current_node = facts->flags & MVM_SPESH_FACT_RW_CONT
                    ? agn->yes
                    : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_CERTAIN_RESULT:
                current_result = agn->result;
                current_node = agn->yes;
                break;
            case MVM_SPESH_GUARD_OP_RESULT:
                return agn->result;
        }
    } while (current_node != 0);
    return current_result;
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
            MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa, total_size, ag);
        else
            MVM_fixed_size_free(tc, tc->instance->fsa, total_size, ag);
    }
}

/* Discards an arg guard held on a static frame, if any, NULLing it out so the
 * candidates will no longer be reachable. */
void MVM_spesh_arg_guard_discard(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMStaticFrameSpesh *spesh = sf->body.spesh;
    if (spesh && spesh->body.spesh_arg_guard) {
        MVM_spesh_arg_guard_destroy(tc, spesh->body.spesh_arg_guard, 1);
        spesh->body.spesh_arg_guard = NULL;
    }
}
