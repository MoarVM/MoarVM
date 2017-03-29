#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps NFA_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &NFA_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMNFA);
    });

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

    /* Write state edge list counts. */
    for (i = 0; i < body->num_states; i++)
        MVM_serialization_write_int(tc, writer, body->num_state_edges[i]);

    /* Write state graph. */
    for (i = 0; i < body->num_states; i++) {
        for (j = 0; j < body->num_state_edges[i]; j++) {
            MVM_serialization_write_int(tc, writer, body->states[i][j].act);
            MVM_serialization_write_int(tc, writer, body->states[i][j].to);
            switch (body->states[i][j].act & 0xff) {
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
                        MVM_serialization_write_int(tc, writer, -(si->num_combs + 1));
                        MVM_serialization_write_int(tc, writer, si->base);
                        for (k = 0; k < si->num_combs; k++)
                            MVM_serialization_write_int(tc, writer, si->combs[k]);
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
            if (edges > 0)
                body->states[i] = MVM_malloc(edges * sizeof(MVMNFAStateInfo));
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
    if (MVM_is_null(tc, obj) || !IS_CONCRETE(obj)) {
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

    MVMROOT(tc, states, {
    MVMROOT(tc, nfa_type, {
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
            if (edges > 0)
                nfa->states[i] = MVM_malloc(edges * sizeof(MVMNFAStateInfo));

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
    });
    });

    return nfa_obj;
}

/* Does a run of the NFA. Produces a list of integers indicating the
 * chosen ordering. */
static MVMint64 * nqp_nfa_run(MVMThreadContext *tc, MVMNFABody *nfa, MVMString *target, MVMint64 offset, MVMint64 *total_fates_out) {
    MVMint64  eos     = MVM_string_graphs(tc, target);
    MVMint64  gen     = 1;
    MVMint64  numcur  = 0;
    MVMint64  numnext = 0;
    MVMint64 *done, *fates, *curst, *nextst, *longlit;
    MVMint64  i, fate_arr_len, num_states, total_fates, prev_fates, usedlonglit;
    MVMint64  orig_offset = offset;
    int nfadeb = tc->instance->nfa_debug_enabled;

    /* Obtain or (re)allocate "done states", "current states" and "next
     * states" arrays. */
    num_states = nfa->num_states;
    if (tc->nfa_alloc_states < num_states) {
        size_t alloc   = (num_states + 1) * sizeof(MVMint64);
        tc->nfa_done   = (MVMint64 *)MVM_realloc(tc->nfa_done, alloc);
        tc->nfa_curst  = (MVMint64 *)MVM_realloc(tc->nfa_curst, alloc);
        tc->nfa_nextst = (MVMint64 *)MVM_realloc(tc->nfa_nextst, alloc);
        tc->nfa_alloc_states = num_states;
    }
    done   = tc->nfa_done;
    curst  = tc->nfa_curst;
    nextst = tc->nfa_nextst;
    memset(done, 0, (num_states + 1) * sizeof(MVMint64));

    /* Allocate fates array. */
    fate_arr_len = 1 + MVM_repr_elems(tc, nfa->fates);
    if (tc->nfa_fates_len < fate_arr_len) {
        tc->nfa_fates     = (MVMint64 *)MVM_realloc(tc->nfa_fates, sizeof(MVMint64) * fate_arr_len);
        tc->nfa_fates_len = fate_arr_len;
    }
    fates = tc->nfa_fates;
    total_fates = 0;
    if (nfadeb) fprintf(stderr,"======================================\nStarting with %d fates in %d states\n", (int)fate_arr_len, (int)num_states) ;

    /* longlit will be updated on a fate whenever NFA passes through final char of a literal. */
    /* These edges are specially marked to indicate which fate they influence the fate of. */
    if (tc->nfa_longlit_len < fate_arr_len) {
        tc->nfa_longlit = (MVMint64 *)MVM_realloc(tc->nfa_longlit, sizeof(MVMint64) * fate_arr_len);
        tc->nfa_longlit_len  = fate_arr_len;
    }
    longlit = tc->nfa_longlit;
    usedlonglit = 0;

    nextst[numnext++] = 1;
    while (numnext && offset <= eos) {
        /* Swap next and current */
        MVMint64 *temp = curst;
        curst   = nextst;
        nextst  = temp;
        numcur  = numnext;
        numnext = 0;

        /* Save how many fates we have before this position is considered. */
        prev_fates = total_fates;

        if (nfadeb) {
            if (offset < eos) {
                MVMGrapheme32 cp = MVM_string_get_grapheme_at_nocheck(tc, target, offset);
                fprintf(stderr,"%c with %ds target %lx offset %"PRId64"\n",cp,(int)numcur, (long)target, offset);
            }
            else {
                fprintf(stderr,"EOS with %ds\n",(int)numcur);
            }
        }
        while (numcur) {
            MVMNFAStateInfo *edge_info;
            MVMint64         edge_info_elems;

            MVMint64 st = curst[--numcur];
            if (st <= num_states) {
                if (done[st] == gen)
                    continue;
                done[st] = gen;
            }

            edge_info = nfa->states[st - 1];
            edge_info_elems = nfa->num_state_edges[st - 1];
            if (nfadeb)
                fprintf(stderr,"\t%d\t%d\t",(int)st, (int)edge_info_elems);
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
                        if (nfadeb)
                            fprintf(stderr, "fate(%016llx) ", (long long unsigned int)arg);
                        for (j = 0; j < total_fates; j++) {
                            if (found_fate)
                                fates[j - found_fate] = fates[j];
                            if ((fates[j] & 0xffffff) == arg) {
                                found_fate++;
                                if (j < prev_fates)
                                    prev_fates--;
                            }
                        }
                        total_fates -= found_fate;
                        if (arg < usedlonglit)
                            arg -= longlit[arg] << 24;
                        if (++total_fates > fate_arr_len) {
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
                    else if (act == MVM_NFA_EDGE_EPSILON && to <= num_states && done[to] != gen) {
                        if (to)
                            curst[numcur++] = to;
                        else if (nfadeb)  /* XXX should turn into a "can't happen" after rebootstrap */
                            fprintf(stderr, "  oops, ignoring epsilon to 0\n");
                        continue;
                    }
                }

                if (offset >= eos) {
                    /* Can't match, so drop state. */
                    continue;
                }
                else {
                    switch (act) {
                        case MVM_NFA_EDGE_CODEPOINT_LL: {
                            MVMGrapheme32 arg = edge_info[i].arg.g;
                            if (MVM_string_get_grapheme_at_nocheck(tc, target, offset) == arg) {
                                MVMint64 fate = (edge_info[i].act >> 8) & 0xfffff;
                                nextst[numnext++] = to;
                                while (usedlonglit <= fate)
                                    longlit[usedlonglit++] = 0;
                                longlit[fate] = offset - orig_offset + 1;
                                if (nfadeb)
                                    fprintf(stderr, "%d->%d ", (int)i, (int)to);
                            }
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT: {
                            MVMGrapheme32 arg = edge_info[i].arg.g;
                            if (MVM_string_get_grapheme_at_nocheck(tc, target, offset) == arg) {
                                nextst[numnext++] = to;
                                if (nfadeb)
                                    fprintf(stderr, "%d->%d ", (int)i, (int)to);
                            }
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT_NEG: {
                            MVMGrapheme32 arg = edge_info[i].arg.g;
                            if (MVM_string_get_grapheme_at_nocheck(tc, target, offset) != arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARCLASS: {
                            MVMint64 arg = edge_info[i].arg.i;
                            if (MVM_string_is_cclass(tc, arg, target, offset))
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARCLASS_NEG: {
                            MVMint64 arg = edge_info[i].arg.i;
                            if (!MVM_string_is_cclass(tc, arg, target, offset))
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARLIST: {
                            MVMString *arg    = edge_info[i].arg.s;
                            MVMGrapheme32 cp = MVM_string_get_grapheme_at_nocheck(tc, target, offset);
                            if (MVM_string_index_of_grapheme(tc, arg, cp) >= 0)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARLIST_NEG: {
                            MVMString *arg    = edge_info[i].arg.s;
                            MVMGrapheme32 cp = MVM_string_get_grapheme_at_nocheck(tc, target, offset);
                            if (MVM_string_index_of_grapheme(tc, arg, cp) < 0)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT_I_LL: {
                            MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            MVMGrapheme32 ord    = MVM_string_get_grapheme_at_nocheck(tc, target, offset);
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
                            MVMGrapheme32 ord    = MVM_string_get_grapheme_at_nocheck(tc, target, offset);
                            if (ord == lc_arg || ord == uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CODEPOINT_I_NEG: {
                            MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            MVMGrapheme32 ord    = MVM_string_get_grapheme_at_nocheck(tc, target, offset);
                            if (ord != lc_arg && ord != uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARRANGE: {
                            MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            MVMGrapheme32 ord    = MVM_string_get_grapheme_at_nocheck(tc, target, offset);
                            if (ord >= lc_arg && ord <= uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARRANGE_NEG: {
                            MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            MVMGrapheme32 ord    = MVM_string_get_grapheme_at_nocheck(tc, target, offset);
                            if (ord < lc_arg || ord > uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_SUBRULE:
                            if (nfadeb)
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
                            MVMGrapheme32 ord    = MVM_string_ord_basechar_at(tc, target, offset);

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
                            MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            MVMGrapheme32 ord    = MVM_string_ord_basechar_at(tc, target, offset);
                            if (ord >= lc_arg && ord <= uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                        case MVM_NFA_EDGE_CHARRANGE_M_NEG: {
                            MVMGrapheme32 uc_arg = edge_info[i].arg.uclc.uc;
                            MVMGrapheme32 lc_arg = edge_info[i].arg.uclc.lc;
                            MVMGrapheme32 ord    = MVM_string_ord_basechar_at(tc, target, offset);
                            if (ord < lc_arg || ord > uc_arg)
                                nextst[numnext++] = to;
                            continue;
                        }
                    }
                }
            }
            if (nfadeb) fprintf(stderr,"\n");
        }

        /* Move to next character and generation. */
        offset++;
        gen++;
    }
    /* strip any literal lengths, leaving only fates */
    if (usedlonglit || nfadeb) {
        if (nfadeb) fprintf(stderr,"Final\n");
        for (i = 0; i < total_fates; i++) {
            if (nfadeb) fprintf(stderr, "  %08llx\n", (long long unsigned int)fates[i]);
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

    /* Push the results onto the bstack. */
    MVMint64 caps = cstack && IS_CONCRETE(cstack)
        ? MVM_repr_elems(tc, cstack)
        : 0;
    for (i = 0; i < total_fates; i++) {
        MVM_repr_push_i(tc, bstack, MVM_repr_at_pos_i(tc, labels, fates[i]));
        MVM_repr_push_i(tc, bstack, offset);
        MVM_repr_push_i(tc, bstack, 0);
        MVM_repr_push_i(tc, bstack, caps);
    }
}
