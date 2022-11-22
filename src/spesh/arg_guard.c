#include "moar.h"

/* Per-callsite candidate information, used during tree construction. */
typedef struct {
    /* The callsite. */
    MVMCallsite *cs;
    /* Index of the certain specialization for this callsite, if any
     * (-1 if none). */
    MVMint32 certain_idx;
    /* Indices of observed or derived type specializations. */
    MVM_VECTOR_DECL(MVMuint32, typed_idxs);
} CallsiteCandidates;

/* Breakdown of arguments by type (this is used both for a breakdown by
 * container type and also by the type within the container). */
typedef struct {
    /* The type, or NULL if it's a derived specialization that does not
     * depend on this type. */
    MVMObject *type;
    /* Whether it's concrete or a type object. */
    MVMuint8 concrete;
    /* Whether this is a check on a container type. */
    MVMuint8 is_container;
    /* Only for containers, if it's rw. */
    MVMuint8 rw;
    /* Indices of candidates matching this type. */
    MVM_VECTOR_DECL(MVMuint32, cand_idxs);
} TypeCandidates;

/* Calculates the maxium number of new nodes that a typed specialization may
 * need in the tree, excluding that of the callsite and the result. */
static size_t max_typed_nodes(MVMCallsite *cs, MVMSpeshStatsType *types) {
    size_t needed = 0;
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

/* Allocates a spesh arg guard tree with the specified amounst of nodes. */
static MVMSpeshArgGuard * allocate_tree(MVMThreadContext *tc, MVMuint32 total_nodes) {
    /* Allocate as a single blob of memory. */
    size_t node_size = total_nodes * sizeof(MVMSpeshArgGuardNode);
    size_t size = sizeof(MVMSpeshArgGuard) + node_size;
    MVMSpeshArgGuard *tree = MVM_malloc(size);
    tree->nodes = (MVMSpeshArgGuardNode *)((char *)tree + sizeof(MVMSpeshArgGuard));
    tree->used_nodes = 0;
    tree->num_nodes = total_nodes;
    return tree;
}

/* Takes a callsite and a current argument index (-1 if we didn't start yet).
 * Finds the next argument index that is an object type and returns it.
 * Returns -1 if there are no further object arguments. */
static MVMint32 next_type_index(MVMCallsite *cs, MVMint32 cur_obj_arg_idx) {
    MVMuint16 i;
    for (i = 0; i < cs->flag_count; i++)
        if ((cs->arg_flags[i] & MVM_CALLSITE_ARG_OBJ) && i > cur_obj_arg_idx)
            return i;
    return -1;
}

/* Add a new result node and return its index. */
static MVMuint32 add_result_node(MVMThreadContext *tc, MVMSpeshArgGuard *tree,
        MVMuint32 result_index) {
    tree->nodes[tree->used_nodes].op = MVM_SPESH_GUARD_OP_RESULT;
    tree->nodes[tree->used_nodes].result = result_index;
    tree->nodes[tree->used_nodes].yes = 0;
    tree->nodes[tree->used_nodes].no = 0;
    return tree->used_nodes++;
}

/* Add a new load node and return its index. */
static MVMuint32 add_load_node(MVMThreadContext *tc, MVMSpeshArgGuard *tree,
        MVMCallsite *cs, MVMuint32 type_index) {
    tree->nodes[tree->used_nodes].op = MVM_SPESH_GUARD_OP_LOAD_ARG;
    tree->nodes[tree->used_nodes].arg_index = type_index;
    tree->nodes[tree->used_nodes].yes = 0;
    tree->nodes[tree->used_nodes].no = 0;
    return tree->used_nodes++;
}

/* Add a new type check node and return its index. */
static MVMuint32 add_type_check_node(MVMThreadContext *tc, MVMSpeshArgGuard *tree,
        MVMObject *type, MVMuint8 concrete) {
    tree->nodes[tree->used_nodes].op = concrete
        ? MVM_SPESH_GUARD_OP_STABLE_CONC
        : MVM_SPESH_GUARD_OP_STABLE_TYPE;
    assert(type->st != NULL);
    tree->nodes[tree->used_nodes].st = type->st;
    tree->nodes[tree->used_nodes].yes = 0;
    tree->nodes[tree->used_nodes].no = 0;
    return tree->used_nodes++;
}

/* Add a new rw-check node and return its index. */
static MVMuint32 add_rw_node(MVMThreadContext *tc, MVMSpeshArgGuard *tree) {
    tree->nodes[tree->used_nodes].op = MVM_SPESH_GUARD_OP_DEREF_RW;
    tree->nodes[tree->used_nodes].offset = 0; /* TODO populate this properly */
    tree->nodes[tree->used_nodes].yes = 0;
    tree->nodes[tree->used_nodes].no = 0;
    return tree->used_nodes++;
}

/* Add a new decont node and return its index. */
static MVMuint32 add_decont_node(MVMThreadContext *tc, MVMSpeshArgGuard *tree) {
    tree->nodes[tree->used_nodes].op = MVM_SPESH_GUARD_OP_DEREF_VALUE;
    tree->nodes[tree->used_nodes].offset = 0; /* TODO populate this properly */
    tree->nodes[tree->used_nodes].yes = 0;
    tree->nodes[tree->used_nodes].no = 0;
    return tree->used_nodes++;
}

/* Adds nodes for checking a particular object argument. Returns the node at
 * the start of the created tree. */
static MVMuint32 add_nodes_for_typed_argument(MVMThreadContext *tc,
        MVMSpeshArgGuard *tree, MVMSpeshCandidate **candidates,
        MVMCallsite *cs, MVMuint32 *valid_candidates,
        MVMuint32 num_valid_candidates, MVMint32 type_index,
        MVMint8 consider_decont_type, MVMuint32 certain_fallback) {
    MVMuint32 first_added = 0;

    /* If we've no next type index, then we reached the end of the
     * argument list. We should have only one result at this point. */
    if (type_index < 0) {
        if (num_valid_candidates != 1)
            MVM_panic(1, "Spesh arg guard: expected 1 candidate but got %d\n",
                num_valid_candidates);
        first_added = add_result_node(tc, tree, valid_candidates[0]);
    }

    /* Otherwise, we need to partition by type. Anything that needs to be
     * checked by container type will then need sub-partitioning, done by
     * a recursive call to this routine. */
    else {
        /* Do the partitioning. */
        MVM_VECTOR_DECL(TypeCandidates, by_type);
        MVMuint32 i, j, update_node = 0, derived_only;
        MVMint32 derived_idx, additional_update_node;
        MVM_VECTOR_INIT(by_type, num_valid_candidates);
        for (i = 0; i < num_valid_candidates; i++) {
            /* See if we already have a type for this. */
            MVMint32 found = -1;
            MVMSpeshStatsType type_info = candidates[valid_candidates[i]]->body.type_tuple[type_index];
            MVMObject *search_type = consider_decont_type
                ? type_info.decont_type
                : type_info.type;
            MVMuint8 search_concrete = consider_decont_type
                ? type_info.decont_type_concrete
                : type_info.type_concrete;
            MVMuint8 search_rw = consider_decont_type
                ? 0
                : type_info.rw_cont;
            MVMuint8 search_is_container = consider_decont_type
                ? 0
                : type_info.decont_type != NULL;
            for (j = 0; j < MVM_VECTOR_ELEMS(by_type); j++) {
                if (by_type[j].type == search_type &&
                        by_type[j].concrete == search_concrete &&
                        by_type[j].rw == search_rw &&
                        by_type[j].is_container == search_is_container) {
                    found = j;
                    break;
                }
            }

            /* If we didn't find such a node, create it. */
            if (found == -1) {
                TypeCandidates tc;
                tc.type = search_type;
                tc.concrete = search_concrete;
                tc.rw = search_rw;
                tc.is_container = search_is_container;
                MVM_VECTOR_INIT(tc.cand_idxs, num_valid_candidates);
                found = MVM_VECTOR_ELEMS(by_type);
                MVM_VECTOR_PUSH(by_type, tc);
            }

            /* Add this candidate to the set matching this type. */
            MVM_VECTOR_PUSH(by_type[found].cand_idxs, valid_candidates[i]);
        }

        /* We start with either a load arg or a decont node, unless this is
         * only a derived specialization and we don't check this node. */
        derived_only = MVM_VECTOR_ELEMS(by_type) == 1 && by_type[0].type == NULL;
        if (!derived_only) {
            if (consider_decont_type)
                first_added = update_node = add_decont_node(tc, tree);
            else
                first_added = update_node = add_load_node(tc, tree, cs, type_index);
        }

        /* Go through the types and add the tree nodes for each, except for
         * any wildcard (NULL type), which must come last. */
        derived_idx = -1;
        additional_update_node = -1;
        for (i = 0; i < MVM_VECTOR_ELEMS(by_type); i++) {
            if (by_type[i].type != NULL) {
                /* The basic type check. */
                MVMuint32 type_node = add_type_check_node(tc, tree, by_type[i].type,
                    by_type[i].concrete);
                if (update_node == first_added) {
                    tree->nodes[update_node].yes = type_node;
                }
                else {
                    tree->nodes[update_node].no = type_node;
                    if (additional_update_node != -1) {
                        tree->nodes[additional_update_node].no = type_node;
                        additional_update_node = -1;
                    }
                }
                update_node = type_node;

                /* Check for rw-ness of container if needed. */
                if (by_type[i].rw) {
                    MVMuint32 rw_node = add_rw_node(tc, tree);
                    tree->nodes[update_node].yes = rw_node;
                    additional_update_node = type_node;
                    update_node = rw_node;
                }

                /* Recurse for next level of guards, or add result. */
                if (by_type[i].is_container) {
                    /* We need to test what's in the container. */
                    tree->nodes[update_node].yes = add_nodes_for_typed_argument(tc,
                        tree, candidates, cs, by_type[i].cand_idxs,
                        MVM_VECTOR_ELEMS(by_type[i].cand_idxs),
                        type_index, 1, certain_fallback);
                }
                else {
                    /* Continue to the next typed argument (this also handles
                     * attaching the result node). */
                    tree->nodes[update_node].yes = add_nodes_for_typed_argument(tc,
                        tree, candidates, cs, by_type[i].cand_idxs,
                        MVM_VECTOR_ELEMS(by_type[i].cand_idxs),
                        next_type_index(cs, type_index), 0, certain_fallback);
                }
            }
            else {
                /* This is the wildcard one. */
                derived_idx = i;
            }
        }

        /* If we've a wildcard entry then continue on to the next argument;
         * if not, attach the certain specialization. This path also handles
         * the case where we only have a derived specialization and it does
         * not check the argument. */
        if (derived_idx != -1) {
            MVMuint32 added_node = add_nodes_for_typed_argument(tc,
                tree, candidates, cs, by_type[derived_idx].cand_idxs,
                MVM_VECTOR_ELEMS(by_type[derived_idx].cand_idxs),
                next_type_index(cs, type_index), 0, certain_fallback);
            if (derived_only)
                first_added = added_node;
            else if (update_node == first_added) {
                tree->nodes[update_node].yes = added_node;
            }
            else {
                tree->nodes[update_node].no = added_node;
                if (additional_update_node != -1)
                    tree->nodes[additional_update_node].no = added_node;
            }
        }
        else {
            /* No wildcard, so attach certain specialization. */
            tree->nodes[update_node].no = certain_fallback;
            if (additional_update_node != -1)
                tree->nodes[additional_update_node].no = certain_fallback;
        }
        
        /* Clean up. */
        for (i = 0; i < MVM_VECTOR_ELEMS(by_type); i++)
            MVM_VECTOR_DESTROY(by_type[i].cand_idxs);
        MVM_VECTOR_DESTROY(by_type);
    }

    return first_added;
}

/* For a given callsite, add nodes for each specialization according to it. */
static void add_nodes_for_callsite(MVMThreadContext *tc, MVMSpeshArgGuard *tree,
        MVMuint32 update_yes_node, MVMSpeshCandidate **candidates,
        CallsiteCandidates cc) {
    /* If we have a certain specialization, then add a node for it. If we fail
     * at any point in finding a typed guard, we'll fall back to looking at
     * this instead. And if there's no typed guards, we'll just use this. */
    MVMuint32 end_node = cc.certain_idx >= 0
        ? add_result_node(tc, tree, cc.certain_idx)
        : 0;

    /* Now add typed guards, or just point at the certain specialization. */
    tree->nodes[update_yes_node].yes = MVM_VECTOR_ELEMS(cc.typed_idxs)
        ? add_nodes_for_typed_argument(tc, tree, candidates, cc.cs, cc.typed_idxs,
                MVM_VECTOR_ELEMS(cc.typed_idxs), next_type_index(cc.cs, -1), 0,
                end_node)
        : end_node;
}

/* Produce and install an updated set of guards, incorporating the new
 * candidate. */
void MVM_spesh_arg_guard_regenerate(MVMThreadContext *tc, MVMSpeshArgGuard **guard_ptr,
        MVMSpeshCandidate **candidates, MVMuint32 num_spesh_candidates) {
    MVMSpeshArgGuard *tree;

    /* Make a first pass thorugh the candidates, grouping them by callsite.
     * Along the way, work out how much space, at most, we'll need to store
     * the tree (when there are multiple candidates, head sharing may mean
     * we need less than this). */
    MVMuint32 i, j;
    MVM_VECTOR_DECL(CallsiteCandidates, by_callsite);
    MVM_VECTOR_INIT(by_callsite, num_spesh_candidates);
    MVMuint32 tree_size = 0;
    for (i = 0; i < num_spesh_candidates; i++) {
        /* Skip discarded candidates. */
        MVMSpeshCandidate *cand = candidates[i];
        MVMint32 found = -1;
        if (cand->body.discarded)
            continue;

        /* See if we already have a candidate for this. */
        for (j = 0; j < MVM_VECTOR_ELEMS(by_callsite); j++) {
            if (by_callsite[j].cs == cand->body.cs) {
                found = j;
                break;
            }
        }

        /* If we didn't find such an entry... */
        if (found == -1) {
            /* Create the entry. */
            CallsiteCandidates cc;
            cc.cs = cand->body.cs;
            cc.certain_idx = -1;
            MVM_VECTOR_INIT(cc.typed_idxs, num_spesh_candidates);
            found = MVM_VECTOR_ELEMS(by_callsite);
            MVM_VECTOR_PUSH(by_callsite, cc);

            /* We're going to need a top-level tree entry for this. */
            tree_size++;
        }

        /* Add this specialization to it. */
        if (cand->body.type_tuple) {
            MVM_VECTOR_PUSH(by_callsite[found].typed_idxs, i);
            tree_size += max_typed_nodes(cand->body.cs, cand->body.type_tuple);
        }
        else {
            by_callsite[found].certain_idx = i;
        }

        /* Need a node for the result also. */
        tree_size++; 
    }

    /* Allocate the guards tree, and add a node for each callsite (we do it
     * this way so the callsite selection ones are bunched at the start); we
     * then make a second pass to attach per-callsite nodes. */
    tree = allocate_tree(tc, tree_size);
    for (i = 0; i < MVM_VECTOR_ELEMS(by_callsite); i++) {
        /* Set up this node to check the callsite. */
        tree->nodes[i].op = MVM_SPESH_GUARD_OP_CALLSITE;
        tree->nodes[i].cs = by_callsite[i].cs;

        /* If it's the final node, then it's "no" is 0 (no match); if not,
         * it's the next node. The "yes" will be set up in the next pass. */
        tree->nodes[i].no = i + 1 == MVM_VECTOR_ELEMS(by_callsite) ? 0 : i + 1;

        /* Count the added node. */
        tree->used_nodes++;
    }
    for (i = 0; i < MVM_VECTOR_ELEMS(by_callsite); i++)
        add_nodes_for_callsite(tc, tree, i, candidates, by_callsite[i]);
    assert(tree->used_nodes <= tree->num_nodes);

    /* Install the produced argument guard. */
    if (*guard_ptr) {
        MVMSpeshArgGuard *prev = *guard_ptr;
        *guard_ptr = tree;
        MVM_spesh_arg_guard_destroy(tc, prev, 1);
    }
    else {
        *guard_ptr = tree;
    }

    /* Clean up working state. */
    for (i = 0; i < MVM_VECTOR_ELEMS(by_callsite); i++)
        MVM_VECTOR_DESTROY(by_callsite[i].typed_idxs);
    MVM_VECTOR_DESTROY(by_callsite);
}

/* Runs the guard against a type tuple, which is used primarily for detecting
 * if an existing specialization already exists. Returns the index of that
 * specialization, or -1 if there is no match. */
MVMint32 MVM_spesh_arg_guard_run_types(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                        MVMCallsite *cs, MVMSpeshStatsType *types) {
    MVMuint32 current_node = 0;
    MVMSpeshStatsType *test = NULL;
    MVMuint32 use_decont_type = 0;
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
            case MVM_SPESH_GUARD_OP_RESULT:
                return agn->result;
        }
    } while (current_node != 0);
    return -1;
}

/* Evaluates the argument guards. Returns >= 0 if there is a matching spesh
 * candidate, or -1 if there is not. */
MVMint32 MVM_spesh_arg_guard_run(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                 MVMArgs args, MVMint32 *certain) {
    MVMuint32 current_node = 0;
    MVMObject *test = NULL;
    if (!ag)
        return -1;
    do {
        MVMSpeshArgGuardNode *agn = &(ag->nodes[current_node]);
        switch (agn->op) {
            case MVM_SPESH_GUARD_OP_CALLSITE:
                current_node = agn->cs == args.callsite ? agn->yes : agn->no;
                break;
            case MVM_SPESH_GUARD_OP_LOAD_ARG:
                test = args.source[args.map[agn->arg_index]].o;
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
            case MVM_SPESH_GUARD_OP_RESULT:
                return agn->result;
        }
    } while (current_node != 0);
    return -1;
}

/* Runs the guards using call information gathered by the optimizer. This is
 * used for finding existing candidates to emit fast calls to or inline. */
MVMint32 MVM_spesh_arg_guard_run_callinfo(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                          MVMSpeshCallInfo *arg_info) {
    MVMuint32 current_node = 0;
    MVMSpeshFacts *facts = NULL;
    MVMuint8 use_decont_facts = 0;
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
            case MVM_SPESH_GUARD_OP_RESULT:
                return agn->result;
        }
    } while (current_node != 0);
    return -1;
}

/* Marks any objects held by an argument guard. */
void MVM_spesh_arg_guard_gc_mark(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                 MVMGCWorklist *worklist) {
    if (ag) {
        MVMuint32 i;
        for (i = 0; i < ag->used_nodes; i++) {
            switch (ag->nodes[i].op)
                case MVM_SPESH_GUARD_OP_STABLE_CONC:
                case MVM_SPESH_GUARD_OP_STABLE_TYPE: {
                    MVM_gc_worklist_add(tc, worklist, &(ag->nodes[i].st));
                    break;
                default: break;
            }
        }
    }
}

void MVM_spesh_arg_guard_gc_describe(MVMThreadContext *tc, MVMHeapSnapshotState *ss,
                                     MVMSpeshArgGuard *ag) {
    if (ag) {
        MVMuint32 i;
        for (i = 0; i < ag->used_nodes; i++) {
            switch (ag->nodes[i].op) {
                case MVM_SPESH_GUARD_OP_STABLE_CONC:
                case MVM_SPESH_GUARD_OP_STABLE_TYPE:
                    MVM_profile_heap_add_collectable_rel_idx(tc, ss,
                        (MVMCollectable*)(ag->nodes[i].st), i);
                    break;
                default: break;
            }
        }
    }
}

/* Frees the memory associated with an argument guard. If `safe` is set to a
 * non-zero value then the memory is freed at the next safepoint. If it is set
 * to zero, the memory is freed immediately. */
void MVM_spesh_arg_guard_destroy(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMuint32 safe) {
    if (ag) {
        if (safe)
            MVM_free_at_safepoint(tc, ag);
        else
            MVM_free(ag);
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
