/* Compares two strings, using the Unicode Collation Algorithm
 * Return values:
 *    0   The strings are identical for the collation levels requested
 * -1/1   String a is less than string b/String a is greater than string b
 *
 * `collation_mode` acts like a bitfield. Each of primary, secondary and tertiary
 * collation levels can be either: disabled, enabled, reversed.
 * In the table below, where + designates sorting normal direction and
 * - indicates reversed sorting for that collation level.
 *
 * Collation level | bitfield value
 *        Primary+ | 1
 *        Primary- | 2
 *      Secondary+ | 4
 *      Secondary- | 8
 *       Tertiary+ | 16
 *       Tertiary- | 32
 */
#include "collation.h"
MVMint32 MVM_unicode_collation_primary (MVMThreadContext *tc, MVMint32 codepoint) {
     return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_PRIMARY);
}
MVMint32 MVM_unicode_collation_secondary (MVMThreadContext *tc, MVMint32 codepoint) {
     return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_SECONDARY);
}
MVMint32 MVM_unicode_collation_tertiary (MVMThreadContext *tc, MVMint32 codepoint) {
     return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_TERTIARY);
}

MVMint32 MVM_unicode_collation_quickcheck (MVMThreadContext *tc, MVMint32 codepoint) {
    return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_QC);
}

/* coll_val is where the collation value will be placed. In the case the
 * collation order is reversed for that level, it will be placed in coll_val_rev */
#define collation_adjust(tc, coll_val, coll_val_rev, collation_mode, cp) {\
    if (collation_mode & 1)\
        coll_val.s.primary       += MVM_unicode_collation_primary(tc, cp);\
    if (collation_mode & 2)\
        coll_val_rev.s.primary   += MVM_unicode_collation_primary(tc, cp);\
    if (collation_mode & 4)\
        coll_val.s.secondary     += MVM_unicode_collation_secondary(tc, cp);\
    if (collation_mode & 8)\
        coll_val_rev.s.secondary += MVM_unicode_collation_secondary(tc, cp);\
    if (collation_mode & 16)\
        coll_val.s.tetriary      += MVM_unicode_collation_tertiary(tc, cp);\
    if (collation_mode & 32)\
        coll_val_rev.s.tetriary  += MVM_unicode_collation_tertiary(tc, cp);\
}
struct collation_key_s {
    MVMuint32 primary, secondary, tetriary, index;
};
union collation_key_u {
    struct collation_key_s s;
    MVMuint32              a[4];
};
typedef union collation_key_u collation_key;
struct collation_stack {
    collation_key keys[100];
    int stack_top;
};
typedef struct collation_stack collation_stack;
int init_stack (MVMThreadContext *tc, collation_stack *stack) {
    stack->stack_top = -1;
    return 1;
}
int stack_is_empty (MVMThreadContext *tc, collation_stack *stack) {
    return -1 == stack->stack_top;
}
#define collation_zero 1

#define set_key(key, a, b, c) {\
    key.s.primary = a;\
    key.s.secondary = b;\
    key.s.tetriary = c;\
}
int print_stack (MVMThreadContext *tc, collation_stack *stack, char *name) {
    int i = 0;
    fprintf(stderr, "stack_%s print_stack() stack elems: %i\n", name, stack->stack_top + 1);
    for (i = 0; i < stack->stack_top + 1; i++) {
        fprintf(stderr, "stack_%s i: %i [%.4X.%.4X.%.4X]\n", name, i, stack->keys[i].s.primary, stack->keys[i].s.secondary, stack->keys[i].s.tetriary);
    }
    return 0;
}
int collation_push_int (MVMThreadContext *tc, collation_stack *stack, int *count, int primary, int secondary, int tertiary, char *name) {
    int i = stack->stack_top;
    fprintf(stderr, "stack_%s Before CLEAR\n", name);
    print_stack(tc, stack, name);
    i++;
    set_key(stack->keys[i],
        primary,
        secondary,
        tertiary
    );
    stack->stack_top = i;
    *count += 1;
    fprintf(stderr, "stack_%s After CLEAR\n", name);
    print_stack(tc, stack, name);
    return 1;
}
int push_special_collation_onto_stack (MVMThreadContext *tc, collation_stack *stack, sub_node node, char *name) {
    int j;
    int i = stack->stack_top;
    fprintf(stderr, "stack_%s push_special_collation_onto_stack() stack_top %i Before\n", name, stack->stack_top);
    print_stack(tc, stack, name);
    for (j = node.collation_key_link;
         j < node.collation_key_link + node.collation_key_elems;
         j++
        )
    {
        i++;
        set_key(stack->keys[i],
            special_collation_keys[j].primary,
            special_collation_keys[j].secondary,
            special_collation_keys[j].tertiary
        );
    }
    stack->stack_top = i;
    fprintf(stderr, "stack_%s push_special_collation_onto_stack() stack_top %i After\n", name, stack->stack_top);
    print_stack(tc, stack, name);
    return 1;
}
int push_MVM_collation_values (MVMThreadContext *tc, int cp, collation_stack *stack, char *name) {
    int coll[3] = {
        MVM_unicode_collation_primary(tc, cp), MVM_unicode_collation_secondary(tc, cp), MVM_unicode_collation_tertiary(tc, cp)
    };
    fprintf(stderr, "stack_%s getting value from MVMDATA\n", name);
    if (coll[0] <= 0 || coll[1] <= 0 || coll[2] <= 0) {
        MVM_exception_throw_adhoc(tc, "Can't find colation data!!!\n");
    }
    stack->stack_top++;
    set_key(stack->keys[stack->stack_top],
        MVM_unicode_collation_primary(tc, cp),
        MVM_unicode_collation_secondary(tc, cp),
        MVM_unicode_collation_tertiary(tc, cp)
    );
    return 1;
}
int process_terminal_node (MVMThreadContext *tc, int num_processed, sub_node node, collation_stack *stack, char *name) {
    int j;
    fprintf(stderr, "stack_%s collation_key_link %i collation_key_elems %i\n", name, node.collation_key_link, node.collation_key_elems);
    if (0 < node.collation_key_link) {
        for (j = node.collation_key_link;
             j < node.collation_key_link + node.collation_key_elems;
             j++
            )
        {
            stack->stack_top++;
            set_key(stack->keys[stack->stack_top],
                special_collation_keys[j].primary + 1,
                special_collation_keys[j].secondary + 1,
                special_collation_keys[j].tertiary + 1
            );
        }
    }
    else {
        if (num_processed == 1) {
            push_MVM_collation_values(tc, node.codepoint, stack, name);
        }
        else {
            // XXX TODO this won't push previous codepoitns only the last?
            MVM_exception_throw_adhoc(tc, "Unhandled no collation link here\n");
        }
    }
    return 0;

}
#define collation_push_from_iter -1
/* Returns the number of added collation keys */
//int coll_push (MVMThreadContext, *tc, *coll_key,
int collation_push_cp (MVMThreadContext *tc, collation_stack *stack, MVMCodepointIter *ci, int cp_maybe, char *name) {
    //int i = stack->stack_top;
    int j;
    int rtrn = 0;
    int cp = cp_maybe == collation_push_from_iter ? MVM_string_ci_get_codepoint(tc, ci) : cp_maybe;
    int query = -1;
    int *cps_thing[3];
    fprintf(stderr, "push orig stack_top %i\n", stack->stack_top);
    query = get_main_node(cp);
    // specialcase æ TODO have them all use normal queries
    if (cp == 230) {
        fprintf(stderr, "stack_%s getting value from specailcase\n", name);
        stack->stack_top++;
        //[.1C47.0020.0004][.0000.0110.0004][.1CAA.0020.0004]
        set_key(stack->keys[stack->stack_top],
            0x1C47 + 1,
            0x0020 + 1,
            0x0004 + 1
        );
        /*stack->stack_top++;
        set_key(stack->keys[stack->stack_top],
            0x0000 + 1,
            0x0110 + 1,
            0x0004 + 1
        );
        */
        stack->stack_top++;
        set_key(stack->keys[stack->stack_top],
            0x1CAA + 1,
            0x0020 + 1,
            0x0004 + 1
        );
        rtrn = 2;
    }
    else if (cp == 198) {
        //[.1C47.0020.000A][.0000.0110.0004][.1CAA.0020.000A]
        stack->stack_top++;
        set_key(stack->keys[stack->stack_top],
            0x1C47 + 1,
            0x0020 + 1,
            0x000A + 1
        );
        /*
        set_key(stack->keys[stack->stack_top],
           0x0000 + 1,
           0x0110 + 1,
           0x0004 + 1
        );
        */
        stack->stack_top++;
        set_key(stack->keys[stack->stack_top],
            0x1CAA + 1,
            0x0020 + 1,
            0x000A + 1
        );
        rtrn = 2;
    }
    /* If query was not -1 we were able to find a main_nodes element to match the
     * first codepoint */
    else if (query != -1) {
        fprintf(stderr, "stack_%s getting value from special_collation_keys\n", name);
        /* If there are no sub_node_elems that means we don't need to look at
         * the next codepoint, we are already at the correct node
         * If there's no more codepoints in the iterator we also are done here */
        if (main_nodes[query].sub_node_elems < 1 || !MVM_string_ci_has_more(tc, ci)) {
            fprintf(stderr, "stack_%s collation_key_link %i collation_key_elems %i\n", name, main_nodes[query].collation_key_link, main_nodes[query].collation_key_elems);
            process_terminal_node(tc, 1, main_nodes[query], stack, name);
        }
        /* Otherwise we need to check the next codepoint(s) (0 < sub_node_elems) */
        else {
            int cp2 = MVM_string_ci_get_codepoint(tc, ci);
            int result = get_first_subnode_elem_no_recurse(main_nodes[query],cp2,  -1 );
            fprintf(stderr, "main_nodes[query] = ");
            print_sub_node(main_nodes[query]);
            cps_thing[0] = &cp2;
            fprintf(stderr, "cp2 = %i, cps_thing[0] = %i\n", cp2, *cps_thing[0]);
            fprintf(stderr, "get no recurse %i\n", result );
            if (result < 0) {
                fprintf(stderr, "Couldn't find {%i,%i}, running collation_push_cp for this cp\n", cp, cp2);
                collation_push_cp(tc, stack, ci, cp2, name);
            }
            else if (sub_nodes[result].sub_node_elems < 1) {
                fprintf(stderr, "Pushing {%i, %i}'s collation onto the stack\n", cp, cp2);
                push_special_collation_onto_stack(tc, stack, sub_nodes[result], name);
            }
        }

    }
    else {
        rtrn = push_MVM_collation_values(tc, cp, stack, name);
    }
    return rtrn;
}
#define onexit_stack fprintf(stderr, "Grabbing from the stack. a_pushed %i b_pushed %i\n", *a_keys_pushed, *b_keys_pushed)

int grab_from_stack
(
    MVMThreadContext *tc,
    int *keys_pushed,
    MVMCodepointIter *ci,
    collation_stack *stack,
    char *name
)
{
    fprintf(stderr, "Grabbing from the stack. %s_keys_pushed %i. stack_elems %i\n", name, *keys_pushed, stack->stack_top +1);
    if (!MVM_string_ci_has_more(tc, ci)) {
        return 0;
    }
    *keys_pushed += collation_push_cp(tc, stack, ci, collation_push_from_iter, name);
    fprintf(stderr, "Finished one grapheme grab to stack. %s_keys_pushed %i, stack_elems %i\n", name, *keys_pushed, stack->stack_top +1);
    return 1;
}
/* These are not found in the collation data and must be synthesized */
int synthesize_values (int cp) {
    /* ASSIGNED Block=Tangut/Tangut_Components */
    int collation_key_Tangut[2][3] = {
        {0xFB00, 0x20, 0x2}, {((cp - 0x17000) | 0x8000), 0x0, 0x0}
    };
    /* ASSIGNED Block=Nushu */
    int collation_key_Nushu[2][3] = {
        {0xFB01, 0x20, 0x2}, {((cp - 0x1B170) | 0x8000), 0x0, 0x0}
    };
    /* Unified_Ideograph=True AND ((Block=CJK_Unified_Ideograph) OR (Block=CJK_Compatibility_Ideographs)) */
    int collation_key_Ideograph[2][3] = {
        {(0xFB40+ (cp >> 15)), 0x20, 0x2}, {((cp - 0x1B170) | 0x8000), 0x0, 0x0}
    };

    return 0;
}
/* MVM_unicode_string_compare supports synthetic graphemes but in case we have
 * a codepoint without any collation value, we do not yet decompose it and
 * then use the decomposed codepoint's weights. */
MVMint64 MVM_unicode_string_compare
        (MVMThreadContext *tc, MVMString *a, MVMString *b,
         MVMint64 collation_mode, MVMint64 lang_mode, MVMint64 country_mode) {
    MVMStringIndex alen, blen;
    /* Iteration variables */
    MVMCodepointIter a_ci, b_ci;
    MVMGraphemeIter *s_has_more_gi;
    MVMGrapheme32 ai, bi;
    int a_keys_pushed = 0;
    int b_keys_pushed = 0;
    /* default of everything being off */
    int level_eval_settings[3][3] = {
         {0,0,0}, {0,0,0}, {0,0,0}
    };
    /* Collation order numbers */
    collation_key ai_coll_val = {0,0,0};
    collation_key bi_coll_val = {0,0,0};
    collation_stack stack_a;
    collation_stack stack_b = {
{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1}
};
    if (collation_mode & 1) {
        level_eval_settings[0][0] += -1;
        level_eval_settings[0][1] +=  0;
        level_eval_settings[0][2] +=  1;
    }
    if (collation_mode & 2) {
        level_eval_settings[0][0] +=  1;
        level_eval_settings[0][1] +=  0;
        level_eval_settings[0][2] += -1;
    }
    if (collation_mode & 4) {
        level_eval_settings[1][0] += -1;
        level_eval_settings[1][1] +=  0;
        level_eval_settings[1][2] +=  1;
    }
    if (collation_mode & 8) {
        level_eval_settings[1][0] +=  1;
        level_eval_settings[1][1] +=  0;
        level_eval_settings[1][2] += -1;
    }
    if (collation_mode & 16) {
        level_eval_settings[2][0] += -1;
        level_eval_settings[2][1] +=  0;
        level_eval_settings[2][2] +=  1;
    }
    if (collation_mode & 32) {
        level_eval_settings[2][0] +=  1;
        level_eval_settings[2][1] +=  0;
        level_eval_settings[2][2] += -1;
    }
    fprintf(stderr, "Setting: %li\n", collation_mode);
    fprintf(stderr, "Setting primary {%i,%i,%i}\n", level_eval_settings[0][0], level_eval_settings[0][1], level_eval_settings[0][2]);
    fprintf(stderr, "Setting secondary {%i,%i,%i}\n", level_eval_settings[1][0], level_eval_settings[1][1], level_eval_settings[1][2]);
    fprintf(stderr, "Setting tertiary {%i,%i,%i}\n", level_eval_settings[2][0], level_eval_settings[2][1], level_eval_settings[2][2]);

    init_stack(tc, &stack_a);
    init_stack(tc, &stack_b);
    fprintf(stderr, "is_empty? %d\n", stack_is_empty(tc, &stack_a));
    MVM_string_check_arg(tc, a, "compare");
    MVM_string_check_arg(tc, b, "compare");
    /* Simple cases when one or both are zero length. */
    alen = MVM_string_graphs_nocheck(tc, a);
    blen = MVM_string_graphs_nocheck(tc, b);
    if (alen == 0)
        return blen == 0 ? 0 : -1;
    if (blen == 0)
        return 1;
    /* Initialize a codepoint iterator */
    MVMROOT(tc, a_ci, {
        MVM_string_ci_init(tc, &a_ci, a, 0);
        MVMROOT(tc, b_ci, {
            MVM_string_ci_init(tc, &b_ci, b, 0);
        });
    });


    /* Otherwise, need to iterate by codepoint */
    fprintf(stderr, "Pushing initial collation elements to the stack\n");
    grab_from_stack(tc, &b_keys_pushed, &b_ci, &stack_b, "b");
    fprintf(stderr, "After Initial grab b\n");
    print_stack(tc, &stack_b, "b");
    grab_from_stack(tc, &a_keys_pushed, &a_ci, &stack_a, "a");
    fprintf(stderr, "After Initial grab a\n");
    print_stack(tc, &stack_a, "a");

    int pos_a = 0, pos_b = 0, i = 0, rtrn = 0;
    int grab_a_rtrn = 1, grab_b_rtrn = 1;
    int grab_a_done = 0, grab_b_done = 0;
    /* From 0 to 2 for primary, secondary, tertiary levels */
    int a_level = 0;
    int b_level = 0;
    while (rtrn == 0) {
        while (pos_a < a_keys_pushed && pos_b < b_keys_pushed) {
            fprintf(stderr, "stack_a index %i is value %X\n", pos_a, stack_a.keys[pos_a].s.primary);
            fprintf(stderr, "stack_b index %i is value %X\n", pos_b, stack_b.keys[pos_b].s.primary);

            fprintf(stderr, "checking a_level %i at pos_a %i b_level %i at pos_b %i\n", a_level, pos_a, b_level, pos_b);
            /* If collation values are not equal */
            if (stack_a.keys[pos_a].a[a_level] != stack_b.keys[pos_b].a[b_level]) {
                rtrn = stack_a.keys[pos_a].a[a_level] < stack_b.keys[pos_b].a[b_level] ?  level_eval_settings[0][0] :
                       stack_a.keys[pos_a].a[a_level] > stack_b.keys[pos_b].a[b_level] ?  level_eval_settings[0][2] :
                                                                                          level_eval_settings[0][1] ;
                  print_stack(tc, &stack_a, "a");
                  print_stack(tc, &stack_b, "b");
            }
            if (rtrn != 0) {
                fprintf(stderr, "\nDONE decided rtrn=%i\npos_a=%i pos_b=%i a_keys_pushed=%i b_keys_pushed=%i\n", rtrn, pos_a, pos_b, a_keys_pushed, b_keys_pushed);
                return rtrn;
            }
            pos_a++;
            pos_b++;
        }
        if (!grab_b_done) {
            fprintf(stderr, "Pushing b NON_INITIAL collation elements to the stack\n");
            grab_b_rtrn = grab_from_stack(tc, &b_keys_pushed, &b_ci, &stack_b, "b");
            if (!grab_b_rtrn) {
                fprintf(stderr, "Pushing null value collation array to stack_b\n");
                collation_push_int(tc, &stack_b, &b_keys_pushed, collation_zero, collation_zero, 0, "b");
                grab_b_done = 1;
            }
        }
        if (!grab_a_done) {
            fprintf(stderr, "Pushing a NON_INITIAL collation elements to the stack\n");
            grab_a_rtrn = grab_from_stack(tc, &a_keys_pushed, &a_ci, &stack_a, "a");
            if (!grab_a_rtrn) {
                fprintf(stderr, "Pushing null value collation array to stack_a\n");
                /* No collation seperator needed for tertiary so have a real 0 for this */
                collation_push_int(tc, &stack_a, &a_keys_pushed, collation_zero, collation_zero, 0, "a");
                grab_a_done = 1;
            }
        }
        /* Here we wrap to the next level of collation elements */
        if (grab_a_done && a_keys_pushed < pos_a + 1) {
            if (a_level < 2) {
                pos_a = 0;
                a_level++;
                fprintf(stderr, "Setting a_level to %i and pos_a to %i\n", a_level, pos_a);
            }
            else {
                fprintf(stderr, "Can't wrap a anymore so breaking\n");
                break;
            }
        }
        if (grab_b_done && b_keys_pushed < pos_b + 1) {
            if (b_level < 2) {
                pos_b = 0;
                b_level++;
                fprintf(stderr, "Setting b_level to %i and pos_b to %i\n", b_level, pos_b);
            }
            else {
                fprintf(stderr, "Can't wrap b anymore so breaking\n");
                break;
            }
        }
    }
    /* Hackish thing to make higher cp win */
    if (rtrn == 0) {
        MVMCodepointIter a_ci2, b_ci2;
        MVMROOT(tc, a_ci2, {
            MVM_string_ci_init(tc, &a_ci2, a, 0);
            MVMROOT(tc, b_ci2, {
                MVM_string_ci_init(tc, &b_ci2, b, 0);
            });
        });
        while (MVM_string_ci_has_more(tc, &a_ci2) && MVM_string_ci_has_more(tc, &b_ci2)) {
            int rtrn_two = 0;
            int cpa;
            int cpb;
            cpa = MVM_string_ci_get_codepoint(tc, &a_ci2);
            cpb = MVM_string_ci_get_codepoint(tc, &b_ci2);
            rtrn_two = cpa < cpb ? -1 :
                        cpa > cpb ?  1 :
                                     0 ;
            if (rtrn_two != 0) {
                fprintf(stderr, "DECIDING BY CP\n");
                return rtrn_two;
            }

        }


    }
    /* If we don't have quaternary collation level set (we throw away codepoint info)
     * we should return 0 because we have gone through all codepoints we have */
    if ( !( collation_mode & (128 + 64) ) )
        return 0;

    /* If we get here, then collation values were equal and we have
     * quaternary level enabled, so return by length */

    /* If quaternary level is both enabled AND reversed, this negates itself
     * and it is thus ignored */
    if (collation_mode & 64 && collation_mode & 128) {
        return 0;
    }
    else if (collation_mode & 64) {
        return alen < blen ? -1 :
               alen > blen ?  1 :
                              0 ;
    }
    else if (collation_mode & 128) {
        return alen < blen ?  1 :
               alen > blen ? -1 :
                              0 ;
    }
    MVM_exception_throw_adhoc(tc, "unicmp_s end of function should not be reachable\n");
}

/* Looks up a codepoint by name. Lazily constructs a hash. */
MVMGrapheme32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    char *cname = MVM_string_utf8_encode_C_string(tc, name);
    size_t cname_len = strlen((const char *) cname );
    MVMUnicodeNameRegistry *result;
    if (!codepoints_by_name) {
        generate_codepoints_by_name(tc);
    }
    HASH_FIND(hash_handle, codepoints_by_name, cname, cname_len, result);

    MVM_free(cname);
    return result ? result->codepoint : -1;
}

MVMString * MVM_unicode_get_name(MVMThreadContext *tc, MVMint64 codepoint) {
    const char *name;

    /* Catch out-of-bounds code points. */
    if (codepoint < 0) {
        name = "<illegal>";
    }
    else if (codepoint > 0x10ffff) {
        name = "<unassigned>";
    }

    /* Look up name. */
    else {
        MVMuint32 codepoint_row = MVM_codepoint_to_row_index(tc, codepoint);
        if (codepoint_row != -1) {
            name = codepoint_names[codepoint_row];
            if (!name) {
                while (codepoint_row && !codepoint_names[codepoint_row])
                    codepoint_row--;
                name = codepoint_names[codepoint_row];
                if (!name || name[0] != '<')
                    name = "<reserved>";
            }
        }
        else {
            name = "<illegal>";
        }
    }

    return MVM_string_ascii_decode(tc, tc->instance->VMString, name, strlen(name));
}

MVMString * MVM_unicode_codepoint_get_property_str(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code) {
    const char * const str = MVM_unicode_get_property_str(tc, codepoint, property_code);

    if (!str)
        return tc->instance->str_consts.empty;

    return MVM_string_ascii_decode(tc, tc->instance->VMString, str, strlen(str));
}

const char * MVM_unicode_codepoint_get_property_cstr(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code) {
    return MVM_unicode_get_property_str(tc, codepoint, property_code);
}

MVMint64 MVM_unicode_codepoint_get_property_int(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code) {
    if (property_code == 0)
        return 0;
    return (MVMint64)MVM_unicode_get_property_int(tc, codepoint, property_code);
}

MVMint64 MVM_unicode_codepoint_get_property_bool(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code) {
    if (property_code == 0)
        return 0;
    return (MVMint64)MVM_unicode_get_property_int(tc, codepoint, property_code) != 0;
}

MVMint64 MVM_unicode_codepoint_has_property_value(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code, MVMint64 property_value_code) {
    if (property_code == 0)
        return 0;
    return (MVMint64)MVM_unicode_get_property_int(tc,
        codepoint, property_code) == property_value_code ? 1 : 0;
}

/* Looks if there is a case change for the provided codepoint. Since a case
 * change may produce multiple codepoints occasionally, then we return 0 if
 * the case change is a no-op, and otherwise the number of codepoints. The
 * codepoints argument will be set to a pointer to a buffer where those code
 * points can be read from. The caller must not mutate the buffer, nor free
 * it. */
MVMuint32 MVM_unicode_get_case_change(MVMThreadContext *tc, MVMCodepoint codepoint, MVMint32 case_,
                                      const MVMCodepoint **result) {
    if (case_ == MVM_unicode_case_change_type_fold) {
        MVMint32 folding_index = MVM_unicode_get_property_int(tc,
            codepoint, MVM_UNICODE_PROPERTY_CASE_FOLDING);
        if (folding_index) {
            MVMint32 is_simple = MVM_unicode_get_property_int(tc,
                codepoint, MVM_UNICODE_PROPERTY_CASE_FOLDING_SIMPLE);
            if (is_simple) {
                *result = &(CaseFolding_simple_table[folding_index]);
                return 1;
            }
            else {
                MVMint32 i = 3;
                while (i > 0 && CaseFolding_grows_table[folding_index][i - 1] == 0)
                    i--;
                *result = &(CaseFolding_grows_table[folding_index][0]);
                return i;
            }
        }
    }
    else {
        MVMint32 special_casing_index = MVM_unicode_get_property_int(tc,
            codepoint, MVM_UNICODE_PROPERTY_SPECIAL_CASING);
        if (special_casing_index) {
            MVMint32 i = 3;
                while (i > 0 && SpecialCasing_table[special_casing_index][case_][i - 1] == 0)
                    i--;
                *result = SpecialCasing_table[special_casing_index][case_];
                return i;
        }
        else {
            MVMint32 changes_index = MVM_unicode_get_property_int(tc,
                codepoint, MVM_UNICODE_PROPERTY_CASE_CHANGE_INDEX);
            if (changes_index) {
                const MVMCodepoint *found = &(case_changes[changes_index][case_]);
                if (*found != 0) {
                    *result = found;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* XXX make all the statics members of the global MVM instance instead? */
static MVMUnicodeNameRegistry *property_codes_by_names_aliases;
static MVMUnicodeGraphemeNameRegistry *property_codes_by_seq_names;

static void generate_property_codes_by_names_aliases(MVMThreadContext *tc) {
    MVMuint32 num_names = num_unicode_property_keypairs;

    while (num_names--) {
        MVMUnicodeNameRegistry *entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
        entry->name = (char *)unicode_property_keypairs[num_names].name;
        entry->codepoint = unicode_property_keypairs[num_names].value;
        HASH_ADD_KEYPTR(hash_handle, property_codes_by_names_aliases,
            entry->name, strlen(entry->name), entry);
    }
}
static void generate_property_codes_by_seq_names(MVMThreadContext *tc) {
    MVMuint32 num_names = num_unicode_seq_keypairs;

    while (num_names--) {
        MVMUnicodeGraphemeNameRegistry *entry = MVM_malloc(sizeof(MVMUnicodeGraphemeNameRegistry));
        entry->name = (char *)uni_seq_pairs[num_names].name;
        entry->structindex = uni_seq_pairs[num_names].value;
        HASH_ADD_KEYPTR(hash_handle, property_codes_by_seq_names,
            entry->name, strlen(entry->name), entry);
    }
}

MVMint32 MVM_unicode_name_to_property_code(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    char *cname = MVM_string_ascii_encode(tc, name, &size, 0);
    MVMUnicodeNameRegistry *result;
    if (!property_codes_by_names_aliases) {
        generate_property_codes_by_names_aliases(tc);
    }
    HASH_FIND(hash_handle, property_codes_by_names_aliases, cname, strlen((const char *)cname), result);
    MVM_free(cname); /* not really codepoint, really just an index */
    return result ? result->codepoint : 0;
}

static void generate_unicode_property_values_hashes(MVMThreadContext *tc) {
    MVMUnicodeNameRegistry **hash_array = MVM_calloc(MVM_NUM_PROPERTY_CODES, sizeof(MVMUnicodeNameRegistry *));
    MVMuint32 index = 0;
    MVMUnicodeNameRegistry *entry = NULL, *binaries = NULL;
    for ( ; index < num_unicode_property_value_keypairs; index++) {
        MVMint32 property_code = unicode_property_value_keypairs[index].value >> 24;
        entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
        entry->name = (char *)unicode_property_value_keypairs[index].name;
        entry->codepoint = unicode_property_value_keypairs[index].value & 0xFFFFFF;
        HASH_ADD_KEYPTR(hash_handle, hash_array[property_code],
            entry->name, strlen(entry->name), entry);
    }
    for (index = 0; index < MVM_NUM_PROPERTY_CODES; index++) {
        if (!hash_array[index]) {
            if (!binaries) {
                MVMUnicodeNamedValue yes[8] = { {"T",1}, {"Y",1},
                    {"Yes",1}, {"yes",1}, {"True",1}, {"true",1}, {"t",1}, {"y",1} };
                MVMUnicodeNamedValue no [8] = { {"F",0}, {"N",0},
                    {"No",0}, {"no",0}, {"False",0}, {"false",0}, {"f",0}, {"n",0} };
                MVMuint8 i;
                for (i = 0; i < 8; i++) {
                    entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
                    entry->name = (char *)yes[i].name;
                    entry->codepoint = yes[i].value;
                    HASH_ADD_KEYPTR(hash_handle, binaries, yes[i].name, strlen(yes[i].name), entry);
                }
                for (i = 0; i < 8; i++) {
                    entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
                    entry->name = (char *)no[i].name;
                    entry->codepoint = no[i].value;
                    HASH_ADD_KEYPTR(hash_handle, binaries, no[i].name, strlen(no[i].name), entry);
                }
            }
            hash_array[index] = binaries;
        }
    }
    unicode_property_values_hashes = hash_array;
}

MVMint32 MVM_unicode_name_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, MVMString *name) {
    if (property_code <= 0 || property_code >= MVM_NUM_PROPERTY_CODES) {
        return 0;
    }
    else {
        MVMuint64 size;
        char *cname = MVM_string_ascii_encode(tc, name, &size, 0);
        MVMUnicodeNameRegistry *result;

        HASH_FIND(hash_handle, unicode_property_values_hashes[property_code], cname, strlen((const char *)cname), result);
        MVM_free(cname); /* not really codepoint, really just an index */
        return result ? result->codepoint : 0;
    }
}

MVMint32 MVM_unicode_cname_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, const char *cname, size_t cname_length) {
    if (property_code <= 0 || property_code >= MVM_NUM_PROPERTY_CODES) {
        return 0;
    }
    else {
        MVMuint64 size;
        MVMUnicodeNameRegistry *result;

        HASH_FIND(hash_handle, unicode_property_values_hashes[property_code], cname, cname_length, result);
        return result ? result->codepoint : 0;
    }
}

/* Look up the primary composite for a pair of codepoints, if it exists.
 * Returns 0 if not. */
MVMCodepoint MVM_unicode_find_primary_composite(MVMThreadContext *tc, MVMCodepoint l, MVMCodepoint c) {
    MVMint32 lower = l & 0xFF;
    MVMint32 upper = (l >> 8) & 0xFF;
    MVMint32 plane = (l >> 16) & 0xF;
    const MVMint32 *pcs  = comp_p[plane][upper][lower];
    if (pcs) {
        MVMint32 entries = pcs[0];
        MVMint32 i;
        for (i = 1; i < entries; i += 2)
            if (pcs[i] == c)
                return pcs[i + 1];
    }
    return 0;
}

static uv_mutex_t property_hash_count_mutex;
static int property_hash_count = 0;
static uv_once_t property_hash_count_guard = UV_ONCE_INIT;

static void setup_property_mutex(void)
{
    uv_mutex_init(&property_hash_count_mutex);
}

void MVM_unicode_init(MVMThreadContext *tc)
{
    uv_once(&property_hash_count_guard, setup_property_mutex);

    uv_mutex_lock(&property_hash_count_mutex);
    if (property_hash_count == 0) {
        generate_unicode_property_values_hashes(tc);
    }
    property_hash_count++;
    uv_mutex_unlock(&property_hash_count_mutex);
}

void MVM_unicode_release(MVMThreadContext *tc)
{
    uv_mutex_lock(&property_hash_count_mutex);
    property_hash_count--;
    if (property_hash_count == 0) {
        int i;

        for (i = 0; i < MVM_NUM_PROPERTY_CODES; i++) {
            MVMUnicodeNameRegistry *entry;
            MVMUnicodeNameRegistry *tmp;
            unsigned bucket_tmp;
            int j;

            if (!unicode_property_values_hashes[i]) {
                continue;
            }

            for(j = i + 1; j < MVM_NUM_PROPERTY_CODES; j++) {
                if (unicode_property_values_hashes[i] == unicode_property_values_hashes[j]) {
                    unicode_property_values_hashes[j] = NULL;
                }
            }

            HASH_ITER(hash_handle, unicode_property_values_hashes[i], entry, tmp, bucket_tmp) {
                HASH_DELETE(hash_handle, unicode_property_values_hashes[i], entry);
                MVM_free(entry);
            }
            assert(!unicode_property_values_hashes[i]);
        }

        MVM_free(unicode_property_values_hashes);

        unicode_property_values_hashes = NULL;
    }
    uv_mutex_unlock(&property_hash_count_mutex);
}
/* Looks up a codepoint sequence or codepoint by name (case insensitive).
 First tries to look it up by codepoint with MVM_unicode_lookup_by_name and if
 not found as a named codepoint, lazily constructs a hash of the codepoint
 sequences and looks up the sequence name */
MVMString * MVM_unicode_string_from_name(MVMThreadContext *tc, MVMString *name) {
    MVMString * name_uc = MVM_string_uc(tc, name);
    char * cname;
    MVMUnicodeGraphemeNameRegistry *result;

    MVMGrapheme32 result_graph = MVM_unicode_lookup_by_name(tc, name_uc);
    /* If it's just a codepoint, return that */
    if (result_graph >= 0) {
        return MVM_string_chr(tc, result_graph);
    }
    /* Otherwise look up the sequence */
    else {
        const MVMint32 * uni_seq;
        cname = MVM_string_utf8_encode_C_string(tc, name_uc);
        if (!property_codes_by_seq_names) {
            generate_property_codes_by_seq_names(tc);
        }
        HASH_FIND(hash_handle, property_codes_by_seq_names, cname, strlen((const char *)cname), result);
        MVM_free(cname);
        /* If we can't find a result return an empty string */
        if (!result)
            return tc->instance->str_consts.empty;

        uni_seq = uni_seq_enum[result->structindex];
        /* The first element is the number of codepoints in the sequence */
        return MVM_unicode_codepoints_c_array_to_nfg_string(tc, (MVMCodepoint *) uni_seq + 1, uni_seq[0]);
    }

}
