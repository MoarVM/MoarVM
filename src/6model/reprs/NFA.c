#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st;
    MVMObject *obj;

    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVMROOT(tc, st, {
        obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMNFA);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation NFA");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMNFABody *lb = (MVMNFABody *)data;

}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMNFA *nfa = (MVMNFA *)obj;

}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMNFA);
}

/* Initializes the representation. */
MVMREPROps * MVMNFA_initialize(MVMThreadContext *tc) {
    /* Allocate and populate the representation function table. */
    if (!this_repr) {
        this_repr = malloc(sizeof(MVMREPROps));
        memset(this_repr, 0, sizeof(MVMREPROps));
        this_repr->type_object_for = type_object_for;
        this_repr->allocate = allocate;
        this_repr->initialize = initialize;
        this_repr->copy_to = copy_to;
        this_repr->gc_mark = gc_mark;
        this_repr->gc_free = gc_free;
        this_repr->get_storage_spec = get_storage_spec;
        this_repr->compose = compose;
        this_repr->deserialize_stable_size = deserialize_stable_size;
    }
    return this_repr;
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
            nfa->num_state_edges = malloc(num_states * sizeof(MVMint64));
            nfa->states = malloc(num_states * sizeof(MVMNFAStateInfo *));
        }
        for (i = 0; i < num_states; i++) {
            MVMObject *edge_info = MVM_repr_at_pos_o(tc, states, i + 1);
            MVMint64   elems     = MVM_repr_elems(tc, edge_info);
            MVMint64   edges     = elems / 3;
            MVMint64   cur_edge  = 0;

            nfa->num_state_edges[i] = edges;
            if (edges > 0)
                nfa->states[i] = malloc(edges * sizeof(MVMNFAStateInfo));

            for (j = 0; j < elems; j += 3) {
                MVMint64 act = MVM_coerce_simple_intify(tc,
                    MVM_repr_at_pos_o(tc, edge_info, j));
                MVMint64 to  = MVM_coerce_simple_intify(tc,
                    MVM_repr_at_pos_o(tc, edge_info, j + 2));

                nfa->states[i][cur_edge].act = act;
                nfa->states[i][cur_edge].to = to;

                switch (act) {
                case MVM_NFA_EDGE_FATE:
                case MVM_NFA_EDGE_CODEPOINT:
                case MVM_NFA_EDGE_CODEPOINT_NEG:
                case MVM_NFA_EDGE_CHARCLASS:
                case MVM_NFA_EDGE_CHARCLASS_NEG:
                    nfa->states[i][cur_edge].arg.i = MVM_coerce_simple_intify(tc,
                        MVM_repr_at_pos_o(tc, edge_info, j + 1));
                    break;
                case MVM_NFA_EDGE_CHARLIST:
                case MVM_NFA_EDGE_CHARLIST_NEG:
                    MVM_ASSIGN_REF(tc, nfa_obj,
                        nfa->states[i][cur_edge].arg.s,
                        MVM_repr_get_str(tc, MVM_repr_at_pos_o(tc, edge_info, j + 1)));
                    break;
                case MVM_NFA_EDGE_CODEPOINT_I:
                case MVM_NFA_EDGE_CODEPOINT_I_NEG: {
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

/* This public-domain C quick sort implementation by Darel Rex Finley. */
static MVMint64 quicksort(MVMint64 *arr, MVMint64 elements) {
    #define MAX_LEVELS 100
    MVMint64 piv, beg[MAX_LEVELS], end[MAX_LEVELS], i = 0, L, R ;
    beg[0] = 0;
    end[0] = elements;
    while (i >= 0) {
        L = beg[i];
        R = end[i] - 1;
        if (L < R) {
            piv = arr[L];
            if (i == MAX_LEVELS - 1)
                return 0;
            while (L < R) {
                while (arr[R] >= piv && L < R)
                    R--;
                if (L < R)
                    arr[L++] = arr[R];
                while (arr[L] <= piv && L < R)
                    L++;
                if (L < R)
                    arr[R--]  =arr[L];
            }
            arr[L] = piv;
            beg[i+1] = L + 1;
            end[i+1] = end[i];
            end[i++] = L;
        }
        else {
            i--;
        }
    }
    return 1;
}

/* Does a run of the NFA. Produces a list of integers indicating the
 * chosen ordering. */
static MVMint64 * nqp_nfa_run(MVMThreadContext *tc, MVMNFABody *nfa, MVMString *target, MVMint64 offset, MVMint64 *total_fates_out) {
    MVMint64  eos     = NUM_GRAPHS(target);
    MVMint64  gen     = 1;
    MVMint64  numcur  = 0;
    MVMint64  numnext = 0;
    MVMint64 *done, *fates, *curst, *nextst;
    MVMint64  i, fate_arr_len, num_states, total_fates, prev_fates;

    /* Allocate "done states", "current states" and "next states" arrays. */
    num_states = nfa->num_states;
    done   = (MVMint64 *)malloc((num_states + 1) * sizeof(MVMint64));
    curst  = (MVMint64 *)malloc((num_states + 1) * sizeof(MVMint64));
    nextst = (MVMint64 *)malloc((num_states + 1) * sizeof(MVMint64));
    memset(done, 0, (num_states + 1) * sizeof(MVMint64));

    /* Allocate fates array. */
    fate_arr_len = 1 + MVM_repr_elems(tc, nfa->fates);
    fates = (MVMint64 *)malloc(sizeof(MVMint64) * fate_arr_len);
    total_fates = 0;

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
            for (i = 0; i < edge_info_elems; i++) {
                MVMint64 act = edge_info[i].act;
                MVMint64 to  = edge_info[i].to;

                if (act == MVM_NFA_EDGE_FATE) {
                    /* Crossed a fate edge. Check if we already saw this, and
                     * if so bump the entry we already saw. */
                    MVMint64 arg = edge_info[i].arg.i;
                    MVMint64 j;
                    MVMint64 found_fate = 0;
                    for (j = 0; j < total_fates; j++) {
                        if (found_fate)
                            fates[j - 1] = fates[j];
                        if (fates[j] == arg) {
                            found_fate = 1;
                            if (j < prev_fates)
                                prev_fates--;
                        }
                    }
                    if (found_fate) {
                        fates[total_fates - 1] = arg;
                    }
                    else {
                        if (total_fates >= fate_arr_len) {
                            fate_arr_len = total_fates + 1;
                            fates = (MVMint64 *)realloc(fates,
                                sizeof(MVMint64) * fate_arr_len);
                        }
                        fates[total_fates++] = arg;
                    }
                }
                else if (act == MVM_NFA_EDGE_EPSILON && to <= num_states && done[to] != gen) {
                    curst[numcur++] = to;
                }
                else if (offset >= eos) {
                    /* Can't match, so drop state. */
                }
                else if (act == MVM_NFA_EDGE_CODEPOINT) {
                    MVMint64 arg = edge_info[i].arg.i;
                    if (MVM_string_get_codepoint_at_nocheck(tc, target, offset) == arg)
                        nextst[numnext++] = to;
                }
                else if (act == MVM_NFA_EDGE_CODEPOINT_NEG) {
                    MVMint64 arg = edge_info[i].arg.i;
                    if (MVM_string_get_codepoint_at_nocheck(tc, target, offset) != arg)
                        nextst[numnext++] = to;
                }
                else if (act == MVM_NFA_EDGE_CHARCLASS) {
                    MVMint64 arg = edge_info[i].arg.i;
                    if (MVM_string_iscclass(tc, arg, target, offset))
                        nextst[numnext++] = to;
                }
                else if (act == MVM_NFA_EDGE_CHARCLASS_NEG) {
                    MVMint64 arg = edge_info[i].arg.i;
                    if (!MVM_string_iscclass(tc, arg, target, offset))
                        nextst[numnext++] = to;
                }
                else if (act == MVM_NFA_EDGE_CHARLIST) {
                    MVMString *arg    = edge_info[i].arg.s;
                    MVMCodepoint32 cp = MVM_string_get_codepoint_at_nocheck(tc, target, offset);
                    if (MVM_string_index_of_codepoint(tc, arg, cp) >= 0)
                        nextst[numnext++] = to;
                }
                else if (act == MVM_NFA_EDGE_CHARLIST_NEG) {
                    MVMString *arg    = edge_info[i].arg.s;
                    MVMCodepoint32 cp = MVM_string_get_codepoint_at_nocheck(tc, target, offset);
                    if (MVM_string_index_of_codepoint(tc, arg, cp) < 0)
                        nextst[numnext++] = to;
                }
                else if (act == MVM_NFA_EDGE_CODEPOINT_I) {
                    MVMCodepoint32 uc_arg = edge_info[i].arg.uclc.uc;
                    MVMCodepoint32 lc_arg = edge_info[i].arg.uclc.lc;
                    MVMCodepoint32 ord    = MVM_string_get_codepoint_at_nocheck(tc, target, offset);
                    if (ord == lc_arg || ord == uc_arg)
                        nextst[numnext++] = to;
                }
                else if (act == MVM_NFA_EDGE_CODEPOINT_I_NEG) {
                    MVMCodepoint32 uc_arg = edge_info[i].arg.uclc.uc;
                    MVMCodepoint32 lc_arg = edge_info[i].arg.uclc.lc;
                    MVMCodepoint32 ord    = MVM_string_get_codepoint_at_nocheck(tc, target, offset);
                    if (ord != lc_arg && ord != uc_arg)
                        nextst[numnext++] = to;
                }
            }
        }

        /* Move to next character and generation. */
        offset++;
        gen++;

        /* If we got multiple fates at this offset, sort them by the
         * declaration order (represented by the fate number). In the
         * future, we'll want to factor in longest literal prefix too. */
        if (total_fates - prev_fates > 1) {
            MVMint64 char_fates = total_fates - prev_fates;
            for (i = total_fates - char_fates; i < total_fates; i++)
                fates[i] = -fates[i];
            quicksort(&fates[total_fates - char_fates], char_fates);
            for (i = total_fates - char_fates; i < total_fates; i++)
                fates[i] = -fates[i];
        }
    }
    free(done);
    free(curst);
    free(nextst);

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
    MVMObject *fateres = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTIntArray);
    for (i = 0; i < total_fates; i++)
        MVM_repr_bind_pos_i(tc, fateres, i, fates[i]);
    free(fates);

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
    free(fates);
}
