#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps NFA_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &NFA_this_repr, HOW);

    MVMROOT(tc, st) {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMNFA);
    }

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation NFA");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMNFABody *lb = (MVMNFABody *)data;
    MVMint64 i, j;

    MVM_gc_worklist_add(tc, worklist, &lb->fates);

    for (i = 0; i < lb->num_states; i++) {
        MVMint64 edges = lb->num_state_edges[i];
        for (j = 0; j < edges; j++) {
            switch (lb->states[i][j].act) {
                case MVM_NFA_EDGE_CHARLIST:
                case MVM_NFA_EDGE_CHARLIST_NEG:
                    MVM_gc_worklist_add(tc, worklist, &lb->states[i][j].arg.s);
            }
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMNFA *nfa = (MVMNFA *)obj;
    MVMint64 i;
    for (i = 0; i < nfa->body.num_states; i++)
        if (nfa->body.num_state_edges[i])
            MVM_free(nfa->body.states[i]);
    MVM_free(nfa->body.states);
    MVM_free(nfa->body.num_state_edges);
}


static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Serializes the data. */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMNFABody *body = (MVMNFABody *)data;
    MVMint64 i, j;

    /* Write fates. */
    MVM_serialization_write_ref(tc, writer, body->fates);

    /* Write number of states. */
    MVM_serialization_write_int(tc, writer, body->num_states);

    /* Write state edge list counts, skipping synthetic start node. */
    for (i = 0; i < body->num_states; i++) {
        MVMint64 sig_edges = body->num_state_edges[i];
        if (sig_edges && body->states[i][0].act == MVM_NFA_EDGE_SYNTH_CP_COUNT)
            sig_edges--;
        MVM_serialization_write_int(tc, writer, sig_edges);
    }

    /* Write state graph. */
    for (i = 0; i < body->num_states; i++) {
        for (j = 0; j < body->num_state_edges[i]; j++) {
            MVMint64 act = body->states[i][j].act;
            if (act == MVM_NFA_EDGE_SYNTH_CP_COUNT)
                continue;
            MVM_serialization_write_int(tc, writer, act);
            MVM_serialization_write_int(tc, writer, body->states[i][j].to);
            switch (act & 0xff) {
                case MVM_NFA_EDGE_FATE:
                    MVM_serialization_write_int(tc, writer, body->states[i][j].arg.i);
                    break;
                case MVM_NFA_EDGE_CODEPOINT:
                case MVM_NFA_EDGE_CODEPOINT_LL:
                case MVM_NFA_EDGE_CODEPOINT_NEG:
                case MVM_NFA_EDGE_CODEPOINT_M:
                case MVM_NFA_EDGE_CODEPOINT_M_NEG: {
                    MVMGrapheme32 g = body->states[i][j].arg.g;
                    if (g >= 0) {
                        /* Non-synthetic. */
                        MVM_serialization_write_int(tc, writer, g);
                    }
                    else {
                        /* Synthetic. Write the number of codepoints negated,
                         * and then each of the codepoints. */
                        MVMNFGSynthetic *si = MVM_nfg_get_synthetic_info(tc, g);
                        MVMint32 k;
                        MVM_serialization_write_int(tc, writer, -(si->num_codes));
                        for (k = 0; k < si->num_codes; k++)
                            MVM_serialization_write_int(tc, writer, si->codes[k]);
                    }
                    break;
                }
                case MVM_NFA_EDGE_CHARCLASS:
                case MVM_NFA_EDGE_CHARCLASS_NEG:
                    MVM_serialization_write_int(tc, writer, body->states[i][j].arg.i);
                    break;
                case MVM_NFA_EDGE_CHARLIST:
                case MVM_NFA_EDGE_CHARLIST_NEG:
                    MVM_serialization_write_str(tc, writer, body->states[i][j].arg.s);
                    break;
                case MVM_NFA_EDGE_CODEPOINT_I:
                case MVM_NFA_EDGE_CODEPOINT_I_LL:
                case MVM_NFA_EDGE_CODEPOINT_I_NEG:
                case MVM_NFA_EDGE_CODEPOINT_IM:
                case MVM_NFA_EDGE_CODEPOINT_IM_NEG:
                case MVM_NFA_EDGE_CHARRANGE:
                case MVM_NFA_EDGE_CHARRANGE_NEG:
                case MVM_NFA_EDGE_CHARRANGE_M:
                case MVM_NFA_EDGE_CHARRANGE_M_NEG: {
                    MVM_serialization_write_int(tc, writer, body->states[i][j].arg.uclc.lc);
                    MVM_serialization_write_int(tc, writer, body->states[i][j].arg.uclc.uc);
                    break;
                }
            }
        }
    }
}

/* Go through each state and see if there are at least four edges involving
 * a simple codepoint match. If so, sort them to the start and stick in a
 * synthetic edge which indicates the number of codepoint edges ahead. This
 * means that our matching can binary search with the codepoint it has, and
 * skip over any inappropriate edges. */
static int classify_edge(MVMNFAStateInfo *e) {
    switch (e->act) {
        case MVM_NFA_EDGE_SYNTH_CP_COUNT:
            return 0;
        case MVM_NFA_EDGE_CODEPOINT:
        case MVM_NFA_EDGE_CODEPOINT_LL:
            return 1;
        default:
            return 2;
    }
}
static int opt_edge_comp(const void *av, const void *bv) {
    MVMNFAStateInfo *a = (MVMNFAStateInfo *)av;
    MVMNFAStateInfo *b = (MVMNFAStateInfo *)bv;
    MVMint32 type_a = classify_edge(a);
    MVMint32 type_b = classify_edge(b);
    if (type_a < type_b)
        return -1;
    if (type_a > type_b)
        return 1;
    if (type_a == 1) {
        return a->arg.g < b->arg.g ? -1 :
               a->arg.g > b->arg.g ?  1 :
                                      0;
    }
    else {
        return 0;
    }
}
static void sort_states_and_add_synth_cp_node(MVMThreadContext *tc, MVMNFABody *body) {
    MVMint64 s;
    for (s = 0; s < body->num_states; s++) {
        /* See if there's enough interesting edges to do the opt. */
        MVMint32 applicable_edges = 0;
        MVMint64 num_orig_edges = body->num_state_edges[s];
        if (num_orig_edges >= 4) {
            MVMint64 e;
            for (e = 0; e < num_orig_edges; e++) {
                MVMint64 act = body->states[s][e].act;
                if (act == MVM_NFA_EDGE_CODEPOINT || act == MVM_NFA_EDGE_CODEPOINT_LL)
                    applicable_edges++;
            }
        }

        /* If enough edges, insert synthetic and so the sort. */
        if (applicable_edges >= 4) {
            MVMint64 num_new_edges = num_orig_edges + 1;
            MVMNFAStateInfo *new_edges = MVM_malloc(num_new_edges * sizeof(MVMNFAStateInfo));
            new_edges[0].act = MVM_NFA_EDGE_SYNTH_CP_COUNT;
            new_edges[0].arg.i = applicable_edges;
            memcpy(new_edges + 1, body->states[s], num_orig_edges * sizeof(MVMNFAStateInfo));
            qsort(new_edges, num_new_edges, sizeof(MVMNFAStateInfo), opt_edge_comp);
            MVM_free(body->states[s]);
            body->states[s] = new_edges;
            body->num_state_edges[s] = num_new_edges;
        }
    }
}

/* Deserializes the data. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMNFABody *body = (MVMNFABody *)data;
    MVMint64 i, j;

    /* Read fates. */
    body->fates = MVM_serialization_read_ref(tc, reader);

    /* Read number of states. */
    body->num_states = MVM_serialization_read_int(tc, reader);

    if (body->num_states > 0) {
        /* Read state edge list counts. */
        body->num_state_edges = MVM_malloc(body->num_states * sizeof(MVMint64));
        for (i = 0; i < body->num_states; i++)
            body->num_state_edges[i] = MVM_serialization_read_int(tc, reader);

        /* Read state graph. */
        body->states = MVM_malloc(body->num_states * sizeof(MVMNFAStateInfo *));
        for (i = 0; i < body->num_states; i++) {
            MVMint64 edges = body->num_state_edges[i];
            if (edges > 0) {
                body->states[i] = MVM_malloc(edges * sizeof(MVMNFAStateInfo));
            }
            for (j = 0; j < edges; j++) {
                body->states[i][j].act = MVM_serialization_read_int(tc, reader);
                body->states[i][j].to = MVM_serialization_read_int(tc, reader);
                switch (body->states[i][j].act & 0xff) {
                    case MVM_NFA_EDGE_FATE:
                        body->states[i][j].arg.i = MVM_serialization_read_int(tc, reader);
                        break;
                    case MVM_NFA_EDGE_CODEPOINT:
                    case MVM_NFA_EDGE_CODEPOINT_LL:
                    case MVM_NFA_EDGE_CODEPOINT_NEG:
                    case MVM_NFA_EDGE_CODEPOINT_M:
                    case MVM_NFA_EDGE_CODEPOINT_M_NEG: {
                        MVMint64 cp_or_synth_count = MVM_serialization_read_int(tc, reader);
                        if (cp_or_synth_count >= 0) {
                            body->states[i][j].arg.g = (MVMGrapheme32)cp_or_synth_count;
                        }
                        else {
                            MVMint32 num_codes = -cp_or_synth_count;
                            MVMCodepoint *codes = MVM_malloc(num_codes * sizeof(MVMCodepoint));
                            MVMint32 k;
                            for (k = 0; k < num_codes; k++)
                                codes[k] = (MVMCodepoint)MVM_serialization_read_int(tc, reader);
                            body->states[i][j].arg.g = MVM_nfg_codes_to_grapheme(tc, codes, num_codes);
                            MVM_free(codes);
                        }
                        break;
                    }
                    case MVM_NFA_EDGE_CHARCLASS:
                    case MVM_NFA_EDGE_CHARCLASS_NEG:
                        body->states[i][j].arg.i = MVM_serialization_read_int(tc, reader);
                        break;
                    case MVM_NFA_EDGE_CHARLIST:
                    case MVM_NFA_EDGE_CHARLIST_NEG:
                        MVM_ASSIGN_REF(tc, &(root->header), body->states[i][j].arg.s, MVM_serialization_read_str(tc, reader));
                        break;
                    case MVM_NFA_EDGE_CODEPOINT_I:
                    case MVM_NFA_EDGE_CODEPOINT_I_LL:
                    case MVM_NFA_EDGE_CODEPOINT_I_NEG:
                    case MVM_NFA_EDGE_CODEPOINT_IM:
                    case MVM_NFA_EDGE_CODEPOINT_IM_NEG:
                    case MVM_NFA_EDGE_CHARRANGE:
                    case MVM_NFA_EDGE_CHARRANGE_NEG:
                    case MVM_NFA_EDGE_CHARRANGE_M:
                    case MVM_NFA_EDGE_CHARRANGE_M_NEG: {
                        body->states[i][j].arg.uclc.lc = MVM_serialization_read_int(tc, reader);
                        body->states[i][j].arg.uclc.uc = MVM_serialization_read_int(tc, reader);
                        break;
                    }
                }
            }
        }
    }

    sort_states_and_add_synth_cp_node(tc, body);
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMNFA);
}

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMNFABody *body = (MVMNFABody *)data;
    MVMuint64 total;
    MVMint64 i;

    total = body->num_states * sizeof(MVMint64); /* for num_state_edges */
    total += body->num_states * sizeof(MVMNFAStateInfo *); /* for states level 1 */
    for (i = 0; i < body->num_states; i++)
        total += body->num_state_edges[i] * sizeof(MVMNFAStateInfo);

    return total;
}

/* Initializes the representation. */
const MVMREPROps * MVMNFA_initialize(MVMThreadContext *tc) {
    return &NFA_this_repr;
}

static const MVMREPROps NFA_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    serialize,
    deserialize,
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    deserialize_stable_size,
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "NFA", /* name */
    MVM_REPR_ID_NFA,
    unmanaged_size,
    NULL, /* describe_refs */
};

/* We may be provided a grapheme as a codepoint for non-synthetics, or as a
 * 1-char string for synthetics. */
static MVMGrapheme32 get_grapheme(MVMThreadContext *tc, MVMObject *obj) {
    /* Handle null and non-concrete case. */
    if (MVM_UNLIKELY(MVM_is_null(tc, obj) || !IS_CONCRETE(obj))) {
        MVM_exception_throw_adhoc(tc,
            "NFA must be provided with a concrete string or integer for graphemes");
    }

    /* Otherwise, guess something appropriate. */
    else {
        const MVMStorageSpec *ss = REPR(obj)->get_storage_spec(tc, STABLE(obj));
        if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_INT)
            return REPR(obj)->box_funcs.get_int(tc, STABLE(obj), obj, OBJECT_BODY(obj));
        else if (ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR)
            return MVM_string_get_grapheme_at(tc,
                REPR(obj)->box_funcs.get_str(tc, STABLE(obj), obj, OBJECT_BODY(obj)),
                0);
        else
            MVM_exception_throw_adhoc(tc,
                "NFA must be provided with a string or integer for graphemes");
    }
}

MVMObject * MVM_nfa_from_statelist(MVMThreadContext *tc, MVMObject *states, MVMObject *nfa_type) {
    MVMObject  *nfa_obj;
    MVMNFABody *nfa;
    MVMint64    i, j, num_states;

    MVMROOT2(tc, states, nfa_type) {
        /* Create NFA object. */
        nfa_obj = MVM_repr_alloc_init(tc, nfa_type);
        nfa = (MVMNFABody *)OBJECT_BODY(nfa_obj);

        /* The first state entry is the fates list. */
        nfa->fates = MVM_repr_at_pos_o(tc, states, 0);

        /* Go over the rest and convert to the NFA. */
        num_states = MVM_repr_elems(tc, states) - 1;
        nfa->num_states = num_states;
        if (num_states > 0) {
            nfa->num_state_edges = MVM_calloc(num_states, sizeof(MVMint64));
            nfa->states = MVM_calloc(num_states, sizeof(MVMNFAStateInfo *));
        }
        for (i = 0; i < num_states; i++) {
            MVMObject *edge_info = MVM_repr_at_pos_o(tc, states, i + 1);
            MVMint64   elems     = MVM_repr_elems(tc, edge_info);
            MVMint64   edges     = elems / 3;
            MVMint64   cur_edge  = 0;

            nfa->num_state_edges[i] = edges;
            if (edges > 0) {
                nfa->states[i] = MVM_malloc(edges * sizeof(MVMNFAStateInfo));
            }

            for (j = 0; j < elems; j += 3) {
                MVMint64 act = MVM_coerce_simple_intify(tc,
                    MVM_repr_at_pos_o(tc, edge_info, j));
                MVMint64 to  = MVM_coerce_simple_intify(tc,
                    MVM_repr_at_pos_o(tc, edge_info, j + 2));
                if (to <= 0 && act != MVM_NFA_EDGE_FATE)
                    MVM_exception_throw_adhoc(tc, "Invalid to edge %"PRId64" in NFA statelist", to);

                nfa->states[i][cur_edge].act = act;
                nfa->states[i][cur_edge].to = to;

                switch (act & 0xff) {
                case MVM_NFA_EDGE_FATE:
                    nfa->states[i][cur_edge].arg.i = MVM_coerce_simple_intify(tc,
                        MVM_repr_at_pos_o(tc, edge_info, j + 1));
                    break;
                case MVM_NFA_EDGE_CODEPOINT:
                case MVM_NFA_EDGE_CODEPOINT_LL:
                case MVM_NFA_EDGE_CODEPOINT_NEG:
                case MVM_NFA_EDGE_CODEPOINT_M:
                case MVM_NFA_EDGE_CODEPOINT_M_NEG:
                    nfa->states[i][cur_edge].arg.g = get_grapheme(tc,
                        MVM_repr_at_pos_o(tc, edge_info, j + 1));
                    break;
                case MVM_NFA_EDGE_CHARCLASS:
                case MVM_NFA_EDGE_CHARCLASS_NEG:
                    nfa->states[i][cur_edge].arg.i = MVM_coerce_simple_intify(tc,
                        MVM_repr_at_pos_o(tc, edge_info, j + 1));
                    break;
                case MVM_NFA_EDGE_CHARLIST:
                case MVM_NFA_EDGE_CHARLIST_NEG:
                    MVM_ASSIGN_REF(tc, &(nfa_obj->header),
                        nfa->states[i][cur_edge].arg.s,
                        MVM_repr_get_str(tc, MVM_repr_at_pos_o(tc, edge_info, j + 1)));
                    break;
                case MVM_NFA_EDGE_CODEPOINT_I:
                case MVM_NFA_EDGE_CODEPOINT_I_LL:
                case MVM_NFA_EDGE_CODEPOINT_I_NEG:
                case MVM_NFA_EDGE_CODEPOINT_IM:
                case MVM_NFA_EDGE_CODEPOINT_IM_NEG:
                /* That is not about uppercase/lowercase here, but lower and upper bounds
                   of our range. */
                case MVM_NFA_EDGE_CHARRANGE:
                case MVM_NFA_EDGE_CHARRANGE_NEG:
                case MVM_NFA_EDGE_CHARRANGE_M:
                case MVM_NFA_EDGE_CHARRANGE_M_NEG: {
                    MVMObject *arg = MVM_repr_at_pos_o(tc, edge_info, j + 1);
                    nfa->states[i][cur_edge].arg.uclc.lc = MVM_coerce_simple_intify(tc,
                        MVM_repr_at_pos_o(tc, arg, 0));
                    nfa->states[i][cur_edge].arg.uclc.uc = MVM_coerce_simple_intify(tc,
                        MVM_repr_at_pos_o(tc, arg, 1));
                    break;
                }
                }

                cur_edge++;
            }
        }
    }

    sort_states_and_add_synth_cp_node(tc, nfa);

    return nfa_obj;
}

static MVMint32 in_done(MVMuint32 *done, MVMuint32 numdone, MVMuint32 st) {
    MVMuint32 i = 0;
    for (i = 0; i < numdone; i++)
        if (done[i] == st)
            return 1;
    return 0;
}

/* Does a run of the NFA. Produces a list of integers indicating the
 * chosen ordering. */
static MVMint64 * nqp_nfa_run(MVMThreadContext *tc, MVMNFABody *nfa, MVMString *target, MVMint64 offset, MVMint64 *total_fates_out) {
    MVMint64  eos     = MVM_string_graphs(tc, target);

    /* done, curst, and nextst are stacks of state indices.
     * done records a set of states that have already been handled this step.
     * curst is a stack of state indices that are still to be handled this step.
     * nextst is a stack of state indices that will become curst in the
     * following step.
     */
    MVMint64  numdone = 0;
    MVMint64  numcur  = 0;
    MVMint64  numnext = 0;

    MVMuint32 *done, *curst, *nextst;

    /* fates records the list of activated fates. The fates are sorted by the
     * offset they have been activated at, with later offsets giving a "better"
     * score (but see also longlit) and lower fate numbers mean earlier
     * definition order, so are also better.
     *
     * total_fates is the number of fates in the "fates" array, and prev_fates
     * is used to differentiate which fates were just added this step, and
     * therefore need to be compared against when activating new states in a
     * step. Fates that were added in an earlier step remain in the order they
     * were at the end of the last step.
     *
     * When fates are activated, a previous entry for the same fate is thrown
     * away, so it can get a new spot in the "new fates this step" portion of
     * the array.
     */
    MVMint64  *fates;

    /* Longlit is an array of zeroes of the length of all fates (often longer,
     * as the arrays are not destroyed in between runs).
     * In the position longlit[fate-index] we record the offset from start-of-
     * match where an _LL edge with the fate encoded in its "act" attribute was
     * matched.
     * When a "fate edge" of the same fate-idx (in that case in the "v"
     * attribute) is matched, we take the corresponding value in "longlit" (if
     * one is present) and reduce the number we put into the fate array by the
     * longlit value shifted 24 bits up. This gives us a negative number, so
     * that simple comparison will give a fate with a higher longlit a better
     * spot.
     * We use "usedlonglit" to zero out spots in the longlit array when they
     * are first passed by a higher fate getting a longlit entry. */
    MVMint64  *longlit;

    MVMint64  fate_arr_len, total_fates, prev_fates, usedlonglit;

    /* Number of states total that the NFA has. */
    MVMint64 num_states;

    /* The index into the array of edges of the state that is currently being
     * handled. */
    MVMint64 i;

    /* Offset into the string that we started matching at. */
    MVMint64  orig_offset = offset;

    /* We used a cached grapheme iterator since we often request the same
     * grapheme multiple times, most common after that is requesting the next
     * grapheme. */
    MVMGraphemeIter_cached gic;

    /* Settable through an environment variable, this outputs information about
     * an NFA run as it unfolds. */
    int nfadeb = tc->instance->nfa_debug_enabled;

    /* Obtain or (re)allocate "done states", "current states" and "next
     * states" arrays. */
    num_states = nfa->num_states;
    if (tc->nfa_alloc_states < num_states) {
        size_t alloc   = (num_states + 1) * sizeof(MVMuint32);
        tc->nfa_done   = (MVMuint32 *)MVM_realloc(tc->nfa_done, alloc);
        tc->nfa_curst  = (MVMuint32 *)MVM_realloc(tc->nfa_curst, alloc);
        tc->nfa_nextst = (MVMuint32 *)MVM_realloc(tc->nfa_nextst, alloc);
        tc->nfa_alloc_states = num_states;
    }
    done   = tc->nfa_done;
    curst  = tc->nfa_curst;
    nextst = tc->nfa_nextst;

    /* Allocate fates array. */
    fate_arr_len = 1 + MVM_repr_elems(tc, nfa->fates);
    if (tc->nfa_fates_len < fate_arr_len) {
        tc->nfa_fates     = (MVMint64 *)MVM_realloc(tc->nfa_fates, sizeof(MVMint64) * fate_arr_len);
        tc->nfa_fates_len = fate_arr_len;
    }
    fates = tc->nfa_fates;

    total_fates = 0;

    if (MVM_UNLIKELY(nfadeb)) fprintf(stderr,"======================================\nStarting with %d fates in %d states\n", (int)fate_arr_len, (int)num_states) ;

    /* longlit will be updated on a fate whenever NFA passes through final char of a literal. */
    /* These edges are specially marked to indicate which fate they influence the fate of. */
    if (tc->nfa_longlit_len < fate_arr_len) {
        tc->nfa_longlit = (MVMint64 *)MVM_realloc(tc->nfa_longlit, sizeof(MVMint64) * fate_arr_len);
        tc->nfa_longlit_len  = fate_arr_len;
    }
    longlit = tc->nfa_longlit;
    usedlonglit = 0;

    /* Install the starting state of the NFA, which is just "1" by convention.
     */
    nextst[numnext++] = 1;

    /* In NFA could be called with a string that has 0 graphemes in it. Guard
     * against this so we don't init for the empty string. */
    if (target->body.num_graphs) MVM_string_gi_cached_init(tc, &gic, target, 0);

    while (numnext && offset <= eos) {
        /* Swap next and current */
        MVMuint32 *temp = curst;
        curst   = nextst;
        nextst  = temp;
        numcur  = numnext;

        /* We start with an empty array of states for the upcoming step,
         * and an empty set of states marked as "already handled". */
        numnext = 0;
        numdone = 0;

        /* Save how many fates we have before this position is considered. */
        prev_fates = total_fates;

        if (MVM_UNLIKELY(nfadeb)) {
            if (offset < eos) {
                MVMGrapheme32 cp = MVM_string_get_grapheme_at_nocheck(tc, target, offset);
                fprintf(stderr,"%c with %"PRId64"s target %"PRIXPTR" offset %"PRId64"\n", cp, numcur, (uintptr_t)target, offset);
            }
            else {
                fprintf(stderr,"EOS with %"PRId64"s\n", numcur);
            }
        }

        /* Keep handling states until there are no more active states on
         * the "curst" stack. */
        while (numcur) {
            MVMNFAStateInfo *edge_info;
            MVMint64         edge_info_elems;

            /* st is a state index relative to the array used to create the
             * NFA, which has in its 0th slot the fates array.
             * For the nfa->states array on the other hand, we don't include
             * an entry at index 0 for the fates. That means that in order to
             * access nfa->states, we have to use st - 1. */
            MVMint64 st = curst[--numcur];

            /* There is not really a reason for a number above num_states to
             * get into the curst stack, but better to throw than to crash. */
            if (MVM_UNLIKELY(st > num_states)) {
                MVM_exception_throw_adhoc(tc, "Invalid state index %"PRId64" (allowed up to %"PRId64") got into the active states list.", st, num_states);
            }

            /* An already handled state doesn't need to be handled again */
            if (in_done(done, numdone, st))
                continue;
            /* We can already add it to the done "stack" */
            done[numdone++] = st;

            edge_info = nfa->states[st - 1];
            edge_info_elems = nfa->num_state_edges[st - 1];

            if (MVM_UNLIKELY(nfadeb))
                fprintf(stderr,"\t%"PRIi64"\t%"PRIi64"\t", st, edge_info_elems);

            for (i = 0; i < edge_info_elems; i++) {
                MVMint64 act = edge_info[i].act;
                MVMint64 to  = edge_info[i].to;

                /* All the special cases are under one test. */
                if (act <= MVM_NFA_EDGE_EPSILON) {
                    if (act < 0) {
                        /* Negative indicates a fate is encoded in the act of the codepoint edge. */
                        /* These will redispatch to one of the _LL cases below */
                        act &= 0xff;
                    }
                    else if (act == MVM_NFA_EDGE_FATE) {
                        /* Crossed a fate edge. Check if we already saw this fate, and
                         * if so remove the entry so we can re-add at the new token length. */
                        MVMint64 arg = edge_info[i].arg.i;
                        MVMint64 j;
                        MVMint64 found_fate = 0;
                        if (MVM_UNLIKELY(nfadeb))
                            fprintf(stderr, "fate(%016llx) ", (long long unsigned int)arg);

                        /* We go backwards through the entire fates array, which
                         * is conceptually made up of two parts, indicated by
                         * the prev_fates number. */
                        for (j = 0; j < total_fates; j++) {
                            /* If we're past the spot where we found the fate,
                             * we start shifting everything to the left. */
                            if (found_fate)
                                fates[j - found_fate] = fates[j];
                            /* Fates with a longlit encoded need the longlit
                             * value masked for the comparison. */
                            if ((fates[j] & 0xffffff) == arg) {
                                found_fate++;
                                /* Move the separation point back one step as
                                 * well, if needed. */
                                if (j < prev_fates)
                                    prev_fates--;
                            }
                        }

                        /* Adjust total_fates to the new number */
                        total_fates -= found_fate;

                        /* If there has possibly already been a longlit
                         * recorded for this fate */
                        if (arg < usedlonglit)
                            /* Encode longlit in the "arg" value. */
                            arg -= longlit[arg] << 24;
                        if (MVM_UNLIKELY(++total_fates > fate_arr_len)) {
                            /* should never happen if nfa->fates is correct and dedup above works right */
                            fprintf(stderr, "oops adding %016llx to\n", (long long unsigned int)arg);
                            for (j = 0; j < total_fates - 1; j++) {
                                fprintf(stderr, "  %016llx\n", (long long unsigned int)fates[j]);
                            }
                            fate_arr_len      = total_fates + 10;
                            tc->nfa_fates     = (MVMint64 *)MVM_realloc(tc->nfa_fates,
                                sizeof(MVMint64) * fate_arr_len);
                            tc->nfa_fates_len = fate_arr_len;
                            fates             = tc->nfa_fates;
                        }
                        /* a small insertion sort */
                        j = total_fates - 1;
                        while (--j >= prev_fates && fates[j] < arg) {
                            fates[j + 1] = fates[j];
                        }
                        fates[++j] = arg;
                        continue;
                    }
                    else if (act == MVM_NFA_EDGE_EPSILON && to <= num_states &&
                            !in_done(done, numdone, to)) {
                        /* An epsilon edge immediately goes into curst, instead
                         * of nextst like non-epsilon edges. */
                        if (to)
                            curst[numcur++] = to;
                        else if (MVM_UNLIKELY(nfadeb))  /* XXX should turn into a "can't happen" after rebootstrap */
                            fprintf(stderr, "  oops, ignoring epsilon to 0\n");
                        continue;
                    }
                }

                if (eos <= offset) {
                    /* Can't match, so drop state. */
                    continue;
                }
                else {
                    switch (act) {
                        case MVM_NFA_EDGE_CODEPOINT_LL: {
                            const MVMGrapheme32 arg = edge_info[i].arg.g;
                            if (MVM_string_gi_cached_get_grapheme(tc, &gic, offset) == arg) {
                                MVMint64 fate = (edge_info[i].act >> 8) & 0xfffff;
                                nextst[numnext++] = to;
                                /* Fill up longlit array with zeroes if
                                 * necessary */
                                while (usedlonglit <= fate)
                                    longlit[usedlonglit++] = 0;
                                longlit[fate] = offset - orig_offset + 1;
                                if (MVM_UNLIKELY(nfadeb))
                                    fprintf(stderr, "%d->%d ", (int)i, (int)to);
                            }
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT: {
                            const MVMGrapheme32 arg = edge_info[i].arg.g;
                            if (MVM_string_gi_cached_get_grapheme(tc, &gic, offset) == arg) {
                                nextst[numnext++] = to;
                                if (MVM_UNLIKELY(nfadeb))
                                    fprintf(stderr, "%d->%d ", (int)i, (int)to);
                            }
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT_NEG: {
                            const MVMGrapheme32 arg = edge_info[i].arg.g;
                            if (MVM_string_gi_cached_get_grapheme(tc, &gic, offset) != arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARCLASS: {
                            const MVMint64 arg = edge_info[i].arg.i;
                            if (MVM_string_grapheme_is_cclass(tc, arg, MVM_string_gi_cached_get_grapheme(tc, &gic, offset)))
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARCLASS_NEG: {
                            const MVMint64 arg = edge_info[i].arg.i;
                            if (!MVM_string_grapheme_is_cclass(tc, arg, MVM_string_gi_cached_get_grapheme(tc, &gic, offset)))
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARLIST: {
                            MVMString *arg   = edge_info[i].arg.s;
                            MVMGrapheme32 cp = MVM_string_gi_cached_get_grapheme(tc, &gic, offset);
                            if (MVM_string_index_of_grapheme(tc, arg, cp) >= 0)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARLIST_NEG: {
                            MVMString *arg    = edge_info[i].arg.s;
                            const MVMGrapheme32 cp = MVM_string_gi_cached_get_grapheme(tc, &gic, offset);
                            if (MVM_string_index_of_grapheme(tc, arg, cp) < 0)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT_I_LL: {
                            const MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            const MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            const MVMGrapheme32 ord    = MVM_string_gi_cached_get_grapheme(tc, &gic, offset);
                            if (ord == lc_arg || ord == uc_arg) {
                                MVMint64 fate = (edge_info[i].act >> 8) & 0xfffff;
                                nextst[numnext++] = to;
                                while (usedlonglit <= fate)
                                    longlit[usedlonglit++] = 0;
                                longlit[fate] = offset - orig_offset + 1;
                            }
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT_I: {
                            MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            MVMGrapheme32 ord    = MVM_string_gi_cached_get_grapheme(tc, &gic, offset);
                            /* CODEPOINT_I is used for case-insensitive matches.
                             * However, the uc and lc arg can be arbitrary. */
                            if (ord == lc_arg || ord == uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT_I_NEG: {
                            const MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            const MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            const MVMGrapheme32 ord    = MVM_string_gi_cached_get_grapheme(tc, &gic, offset);
                            if (ord != lc_arg && ord != uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARRANGE: {
                            MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            MVMGrapheme32 ord    = MVM_string_gi_cached_get_grapheme(tc, &gic, offset);
                            if (ord >= lc_arg && ord <= uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARRANGE_NEG: {
                            const MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            const MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            const MVMGrapheme32 ord    = MVM_string_gi_cached_get_grapheme(tc, &gic, offset);
                            if (ord < lc_arg || uc_arg < ord)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_SUBRULE:
                            /* SUBRULE edges are only needed for NFAs that will
                             * be stored and used together with other parts of
                             * a grammar. The form with subrules in it is
                             * "translated" to a form with everything looked up
                             * and merged, and then cached. The cache is
                             * cleared whenever a mixin into a Grammar class
                             * happens.
                             * See NQP's mergesubrule and mergesubstates */
                            if (MVM_UNLIKELY(nfadeb))
                                fprintf(stderr, "IGNORING SUBRULE\n");
                            continue;
                        case MVM_NFA_EDGE_CODEPOINT_M:
                        case MVM_NFA_EDGE_CODEPOINT_M_NEG: {
                            MVMNormalizer norm;
                            MVMint32 ready;
                            MVMGrapheme32 ga = edge_info[i].arg.g;
                            MVMGrapheme32 gb = MVM_string_ord_basechar_at(tc, target, offset);

                            MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFD);
                            ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, ga, &ga);
                            MVM_unicode_normalizer_eof(tc, &norm);
                            if (!ready)
                                ga = MVM_unicode_normalizer_get_grapheme(tc, &norm);

                            if (((act == MVM_NFA_EDGE_CODEPOINT_M)     && (ga == gb))
                             || ((act == MVM_NFA_EDGE_CODEPOINT_M_NEG) && (ga != gb)))
                                nextst[numnext++] = to;
                            MVM_unicode_normalizer_cleanup(tc, &norm);
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT_IM:
                        case MVM_NFA_EDGE_CODEPOINT_IM_NEG: {
                            MVMNormalizer norm;
                            MVMint32 ready;
                            MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            const MVMGrapheme32 ord = MVM_string_ord_basechar_at(tc, target, offset);

                            MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFD);
                            ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, uc_arg, &uc_arg);
                            MVM_unicode_normalizer_eof(tc, &norm);
                            if (!ready)
                                uc_arg = MVM_unicode_normalizer_get_grapheme(tc, &norm);
                            MVM_unicode_normalizer_cleanup(tc, &norm);

                            MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFD);
                            ready = MVM_unicode_normalizer_process_codepoint_to_grapheme(tc, &norm, lc_arg, &lc_arg);
                            MVM_unicode_normalizer_eof(tc, &norm);
                            if (!ready)
                                lc_arg = MVM_unicode_normalizer_get_grapheme(tc, &norm);

                            if (((act == MVM_NFA_EDGE_CODEPOINT_IM)     && (ord == lc_arg || ord == uc_arg))
                             || ((act == MVM_NFA_EDGE_CODEPOINT_IM_NEG) && (ord != lc_arg && ord != uc_arg)))
                                nextst[numnext++] = to;
                            MVM_unicode_normalizer_cleanup(tc, &norm);
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARRANGE_M: {
                            const MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            const MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            const MVMGrapheme32 ord    = MVM_string_ord_basechar_at(tc, target, offset);
                            if (ord >= lc_arg && ord <= uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARRANGE_M_NEG: {
                            const MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            const MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            const MVMGrapheme32 ord    = MVM_string_ord_basechar_at(tc, target, offset);
                            if (ord < lc_arg || uc_arg < ord)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_SYNTH_CP_COUNT: {
                            /* Binary search the edges ahead for the grapheme. */
                            const MVMGrapheme32 search = MVM_string_gi_cached_get_grapheme(tc, &gic, offset);
                            const MVMint64 num_possibilities = edge_info[i].arg.i;
                            const MVMint64 end = i + num_possibilities;
                            MVMint64 l = i + 1;
                            MVMint64 r = end;
                            MVMint64 found = -1;
                            while (l <= r) {
                                const MVMint64 m = l + (r - l) / 2;
                                const MVMGrapheme32 test = edge_info[m].arg.g;
                                if (test == search) {
                                    /* We found it, but important we get the first edge
                                     * that matches. */
                                    found = m;
                                    while (found > i + 1 && edge_info[found - 1].arg.g == search)
                                        found--;
                                    break;
                                }
                                if (test < search)
                                    l = m + 1;
                                else
                                    r = m - 1;
                            }
                            if (found == -1) {
                                /* Binary search failed to find a match, so just skip all
                                 * the nodes. */
                                i += num_possibilities;
                            }
                            else {
                                /* Add all states that match. */
                                while (found <= end && edge_info[found].arg.g == search) {
                                    to = edge_info[found].to;
                                    if (edge_info[found].act == MVM_NFA_EDGE_CODEPOINT) {
                                        nextst[numnext++] = to;
                                        if (MVM_UNLIKELY(nfadeb))
                                            fprintf(stderr, "%d->%d ", (int)found, (int)to);
                                    }
                                    else {
                                        const MVMint64 fate = (edge_info[found].act >> 8) & 0xfffff;
                                        nextst[numnext++] = to;
                                        while (usedlonglit <= fate)
                                            longlit[usedlonglit++] = 0;
                                        longlit[fate] = offset - orig_offset + 1;
                                        if (MVM_UNLIKELY(nfadeb))
                                            fprintf(stderr, "%d->%d ", (int)found, (int)to);
                                    }
                                    found++;
                                }
                                /* Skip past all the edges that belong to the
                                 * binary search "synthetic" edge. */
                                i += num_possibilities;
                            }
                            break;
                        }
                    }
                }
            }
            if (MVM_UNLIKELY(nfadeb)) fprintf(stderr,"\n");
        }

        /* Move to next character. */
        offset++;
    }
    /* strip any literal lengths, leaving only fates */
    if (usedlonglit || nfadeb) {
        if (MVM_UNLIKELY(nfadeb)) fprintf(stderr,"Final\n");
        for (i = 0; i < total_fates; i++) {
            if (MVM_UNLIKELY(nfadeb)) fprintf(stderr, "  %08llx\n", (long long unsigned int)fates[i]);
            fates[i] &= 0xffffff;
        }
    }

    *total_fates_out = total_fates;
    return fates;
}

/* Takes an NFA, a target string in and an offset. Runs the NFA and returns
 * the order to try the fates in. */
MVMObject * MVM_nfa_run_proto(MVMThreadContext *tc, MVMObject *nfa, MVMString *target, MVMint64 offset) {
    /* Run the NFA. */
    MVMint64  total_fates, i;
    MVMint64 *fates = nqp_nfa_run(tc, (MVMNFABody *)OBJECT_BODY(nfa), target, offset, &total_fates);

    /* Copy results into an integer array. */
    MVMObject *fateres = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIntArray);
    /* No need to pre-size the array, since total_fates never seems to get higher than 5 when building Rakudo. */
    for (i = 0; i < total_fates; i++)
        MVM_repr_bind_pos_i(tc, fateres, i, fates[i]);

    return fateres;
}

/* Takes an NFA, target string and offset. Runs the NFA, and uses the output
 * to update the bstack with backtracking points to try the alternation
 * branches in the correct order. The current capture stack is needed for its
 * height. */
void MVM_nfa_run_alt(MVMThreadContext *tc, MVMObject *nfa, MVMString *target,
        MVMint64 offset, MVMObject *bstack, MVMObject *cstack, MVMObject *labels) {
    /* Run the NFA. */
    MVMint64  total_fates, i;
    MVMint64 *fates = nqp_nfa_run(tc, (MVMNFABody *)OBJECT_BODY(nfa), target, offset, &total_fates);

    if (total_fates > 0) {
        /* Push the results onto the bstack. */
        MVMint64 caps = cstack && IS_CONCRETE(cstack)
            ? MVM_repr_elems(tc, cstack)
            : 0;
        MVMint64 belems = bstack && IS_CONCRETE(bstack)
            ? MVM_repr_elems(tc, bstack)
            : 0;
        REPR(bstack)->pos_funcs.set_elems(tc, STABLE(bstack), bstack,
                        OBJECT_BODY(bstack), belems + 4*total_fates);
        for (i = 0; i < total_fates; i++) {
            MVM_repr_bind_pos_i(tc, bstack, belems + 4*i + 0, MVM_repr_at_pos_i(tc, labels, fates[i]));
            MVM_repr_bind_pos_i(tc, bstack, belems + 4*i + 1, offset);
            MVM_repr_bind_pos_i(tc, bstack, belems + 4*i + 2, 0);
            MVM_repr_bind_pos_i(tc, bstack, belems + 4*i + 3, caps);
        }
    }
}
