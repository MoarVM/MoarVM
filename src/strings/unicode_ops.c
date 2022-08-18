/* Compares two strings, using the Unicode Collation Algorithm
 * Return values:
 *    0   The strings are identical for the collation levels requested
 * -1/1   String a is less than string b/String a is greater than string b
 *
 * `collation_mode` acts like a bitmask. Each of primary, secondary and tertiary
 * collation levels can be either: disabled, enabled, reversed.
 * In the table below, where + designates sorting normal direction and
 * - indicates reversed sorting for that collation level.
 *
 * Collation level | bitmask value
 *        Primary+ |   1
 *        Primary- |   2
 *      Secondary+ |   4
 *      Secondary- |   8
 *       Tertiary+ |  16
 *       Tertiary- |  32
 *     Quaternary+ |  64
 *     Quaternary- | 128
 */
/* Finds the lowest codepoint of the next subnode. If there's no next subnode,
 * returns -1 */
#define MVM_COLLATION_PRIMARY_POSITIVE      1
#define MVM_COLLATION_PRIMARY_NEGATIVE      2
#define MVM_COLLATION_SECONDARY_POSITIVE    4
#define MVM_COLLATION_SECONDARY_NEGATIVE    8
#define MVM_COLLATION_TERTIARY_POSITIVE    16
#define MVM_COLLATION_TERTIARY_NEGATIVE    32
#define MVM_COLLATION_QUATERNARY_POSITIVE  64
#define MVM_COLLATION_QUATERNARY_NEGATIVE 128

MVM_STATIC_INLINE MVMint64 next_node_min (sub_node node) {
    return node.sub_node_elems
        ? main_nodes[node.sub_node_link].codepoint
        : -1;
}
/* Finds the highest codepoint of the next subnode. If there's no next subnode,
 * returns -1 */
MVM_STATIC_INLINE MVMint64 next_node_max (sub_node node) {
    return node.sub_node_elems
        ? main_nodes[node.sub_node_link + node.sub_node_elems - 1].codepoint
        : -1;
}
typedef union collation_key_u collation_key;
struct collation_stack {
    collation_key *keys;
    MVMint64 stack_top;
    MVMint64 stack_size;
};
typedef struct collation_stack collation_stack;

struct collation_key_s {
    MVMuint32 primary, secondary, tertiary, index;
};
union collation_key_u {
    struct collation_key_s s;
    MVMuint32              a[4];
};
struct level_eval_s2 {
    MVMint32 Less, Same, More;
};
union level_eval_u2 {
    MVMint32 a2[3];
    struct level_eval_s2 s2;
};
struct level_eval_s {
    union level_eval_u2 primary, secondary, tertiary, quaternary;
};
union level_eval_u {
    struct level_eval_s s;
    union  level_eval_u2 a[4];
};
typedef union level_eval_u level_eval;
#define initial_stack_size 100
#define collation_zero 1
struct ring_buffer {
    MVMCodepoint codes[codepoint_sequence_no_max];
    MVMuint32    count;
    MVMint32  location;
    MVMCodepoint codes_out[codepoint_sequence_no_max];
    MVMuint32    codes_out_count;
};
typedef struct ring_buffer ring_buffer;
#ifdef COLLATION_DEBUG
    static void print_sub_node (sub_node subnode) {
        MVMint64 min    = next_node_min(subnode);
        char * min_sign = min < 0 ? "-" : "";
        MVMint64 max    = next_node_max(subnode);
        char * max_sign = max < 0 ? "-" : "";
        max = max < 0 ? -max : max;
        min = min < 0 ? -min : min;
        fprintf(stderr,
            "{codepoint 0x%X, next_node_min %s0x%"PRIX64", next_node_max %s0x%"PRIX64", "
                "sub_node_elems %i, sub_node_link %i, "
                    "collation_key_elems %i, collation_key_link %i}\n",
            subnode.codepoint, min_sign, min, max_sign, max,
                subnode.sub_node_elems, subnode.sub_node_link,
                    subnode.collation_key_elems, subnode.collation_key_link);
    }
    static void print_stack (MVMThreadContext *tc, collation_stack *stack, char *name, char *details) {
        int i = 0;
        fprintf(stderr, "stack_%s “%s” print_stack() stack elems: %li\n", name, details, stack->stack_top + 1);
        for (i = 0; i < stack->stack_top + 1; i++) {
            fprintf(stderr, "stack_%s i: %i [%.4X.%.4X.%.4X]\n", name, i, stack->keys[i].s.primary, stack->keys[i].s.secondary, stack->keys[i].s.tertiary);
            if (30 < i) {
                fprintf(stderr, "Not printing any more of the stack. Too large\n");
                break;
            }
        }
    }
    static void print_ring_buffer(MVMThreadContext *tc, ring_buffer *buffer) {
        MVMint64 i;
        fprintf(stderr, "Buffer: count: %"PRIu32" location %"PRIi32"\nBuffer contents: ", buffer->count, buffer->location);
        for (i = 0; i < buffer->count && i < codepoint_sequence_no_max; i++) {
            fprintf(stderr, "i: %"PRIi64": cp: 0x%X ", i, buffer->codes[i]);
            if (30 < i) {
                fprintf(stderr, "Not printing any more of the buffer. Too large\n");
                break;
            }
        }
        fprintf(stderr, "\n");
    }
    #define DEBUG_COLLATION_MODE_PRINT(level_eval_settings) {\
        fprintf(stderr, "Setting collation_mode: %li\nSetting primary   {%i,%i,%i}\n"\
            "Setting secondary {%i,%i,%i}\nSetting tertiary  {%i,%i,%i}\n", collation_mode,\
            level_eval_settings.a[0].a2[0], level_eval_settings.a[0].a2[1], level_eval_settings.a[0].a2[2],\
            level_eval_settings.a[1].a2[0], level_eval_settings.a[1].a2[1], level_eval_settings.a[1].a2[2],\
            level_eval_settings.a[2].a2[0], level_eval_settings.a[2].a2[1], level_eval_settings.a[2].a2[2]);\
    }
    #define DEBUG_PRINT_SPECIAL_PUSHED(what, name, cp) fprintf(stderr, "Special Pushed 0x%X %s onto stack_%s\n", cp, what, name);
    #define DEBUG_PRINT_SUB_NODE(subnode) print_sub_node(subnode);
    #define DEBUG_SPECIAL_PUSHED(block_pushed, name) block_pushed = name;
    #define DEBUG_PRINT_STACK(tc, stack, name, details) print_stack(tc, stack, name, details);
    #define DEBUG_PRINT_RING_BUFFER(tc, buffer) print_ring_buffer(tc, buffer);
    #define DEBUG_PRINT(...) fprintf (stderr, __VA_ARGS__)
#else
    #define DEBUG_PRINT_SUB_NODE(subnode)
    #define DEBUG_PRINT_STACK(tc, stack, name, details)
    #define DEBUG_SPECIAL_PUSHED(block_pushed, name)
    #define DEBUG_PRINT_SPECIAL_PUSHED(what, name, cp)
    #define DEBUG_PRINT_RING_BUFFER(tc, buffer)
    #define DEBUG_COLLATION_MODE_PRINT(level_eval_settings)
    #define DEBUG_PRINT(...)
#endif
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
static MVMint64 collation_push_cp (MVMThreadContext *tc, collation_stack *stack, MVMCodepointIter *ci, int *cp_maybe, int cp_num, char *name);
static void init_stack (MVMThreadContext *tc, collation_stack *stack) {
    stack->keys       = MVM_malloc(sizeof(collation_key) * initial_stack_size);
    stack->stack_top  = -1;
    stack->stack_size = initial_stack_size;
}
static void cleanup_stack (MVMThreadContext *tc, collation_stack *stack) {
    if (stack->keys != NULL) {
        MVM_free_null(stack->keys);
    }
}
static void push_key_to_stack(collation_stack *stack, MVMuint32 primary, MVMuint32 secondary, MVMuint32 tertiary) {\
    stack->stack_top++;
    if (stack->stack_size <= stack->stack_top) {
        stack->keys = MVM_realloc(stack->keys,
            (stack->stack_size + initial_stack_size) * sizeof(collation_stack));
        stack->stack_size += initial_stack_size;
    }
    stack->keys[stack->stack_top].s.primary   = primary;
    stack->keys[stack->stack_top].s.secondary = secondary;
    stack->keys[stack->stack_top].s.tertiary  = tertiary;
}
static MVMint64 collation_push_level_separator (MVMThreadContext *tc, collation_stack *stack, char *name) {
    push_key_to_stack(stack, 0, 0, 0);
    DEBUG_PRINT_STACK(tc, stack, name, "After collation_push_level_separator()");
    return 1;
}
/* Pushes collation keys from a collation_key struct and adds 1 to each level. (This is for places where
 * we store the native DUCET values and we add one because values on the stack are one more) */
static MVMint64 push_onto_stack (MVMThreadContext *tc, collation_stack *stack, collation_key *keys, int keys_to_push, char *name) {
    int j;
    DEBUG_PRINT_STACK(tc, stack, name, "push_onto_stack() Before");
    for (j = 0; j < keys_to_push; j++)
        push_key_to_stack(stack, keys[j].s.primary + 1, keys[j].s.secondary + 1, keys[j].s.tertiary + 1);

    DEBUG_PRINT_STACK(tc, stack, name, "push_onto_stack() After");
    return 1;
}
/* TODO write a script to generate this code */
MVM_STATIC_INLINE MVMint32 compute_AAAA(MVMCodepoint cp, int offset) {
    return (offset + (cp >> 15));
}
MVM_STATIC_INLINE MVMint32 compute_BBBB_offset(MVMCodepoint cp, int offset) {
    return ((cp - offset) | 0x8000);
}
MVM_STATIC_INLINE MVMint32 compute_BBBB_and(MVMCodepoint cp) {
    return ((cp & 0x7FFF) | 0x8000);
}
#define initial_collation_norm_buf_size 5
static MVMint32 NFD_and_push_collation_values (MVMThreadContext *tc, MVMCodepoint cp, collation_stack *stack, MVMCodepointIter *ci, char *name) {
    MVMNormalizer norm;
    MVMCodepoint cp_out;
    MVMint32 ready,
             result_pos  = 0;
    MVMCodepoint *result = MVM_malloc(sizeof(MVMCodepoint) * initial_collation_norm_buf_size);
    MVMint32 result_size = initial_collation_norm_buf_size;
    MVMint64 rtrn        = 0;
    MVM_unicode_normalizer_init(tc, &norm, MVM_NORMALIZE_NFD);
    ready = MVM_unicode_normalizer_process_codepoint(tc, &norm, cp, &cp_out);
    if (ready) {
        if (result_size <= result_pos + ready)
            result = MVM_realloc(result, sizeof(MVMCodepoint) * (result_size += initial_collation_norm_buf_size));
        result[result_pos++] = cp_out;
        while (0 < --ready)
            result[result_pos++] = MVM_unicode_normalizer_get_codepoint(tc, &norm);
    }
    MVM_unicode_normalizer_eof(tc, &norm);
    ready = MVM_unicode_normalizer_available(tc, &norm);
    while (ready--) {
        if (result_size <= result_pos + ready + 1)
            result = MVM_realloc(result, sizeof(MVMCodepoint) * (result_size += initial_collation_norm_buf_size));
        result[result_pos++] = MVM_unicode_normalizer_get_codepoint(tc, &norm);
    }
    /* If the codepoint changed or we now have more than before */
    if (result[0] != cp || 1 < result_pos)
        rtrn = collation_push_cp(tc, stack, ci, result, result_pos, name);
    if (result)
        MVM_free(result);
    return rtrn;
}
/* Returns the number of collation elements pushed onto the stack */
static void collation_push_MVM_values (MVMThreadContext *tc, MVMCodepoint cp, collation_stack *stack, MVMCodepointIter *ci, char *name) {
    collation_key MVM_coll_key = {
        MVM_unicode_collation_primary(tc, cp), MVM_unicode_collation_secondary(tc, cp), MVM_unicode_collation_tertiary(tc, cp), 0
    };
    /* For some reason some Tangut Block items return values, so test that too.
     * Eventually we might want to restructure this code here */
    if (is_Block_Tangut(cp) || MVM_coll_key.s.primary <= 0 || MVM_coll_key.s.secondary <= 0 || MVM_coll_key.s.tertiary <= 0) {
        MVMuint32 AAAA, BBBB;
#ifdef COLLATION_DEBUG
        char *block_pushed = NULL;
#endif
        collation_key calculated_key[2] = {
            {0, 0x20, 0x2, 0},
            {0, 0x00, 0x0, 0}
        };
        /* Block=Tangut+Block=Tangut_Components 0x17000..0x18AFF */
        if (is_Block_Tangut(cp)) {
            AAAA = 0xFB00;
            BBBB = compute_BBBB_offset(cp, 0x17000);
            DEBUG_SPECIAL_PUSHED(block_pushed, "Block_Tangut_and_Tangut_Components");
        }
        /* Assigned_Block=Nushu 0x1B170..1B2FF (*/
        else if (is_Assigned_Block_Nushu(cp)) {
            AAAA = 0xFB01;
            BBBB = compute_BBBB_offset(cp, 0x1B170);
            DEBUG_SPECIAL_PUSHED(block_pushed, "Assigned_Block_Nushu");
        }
        /* Unified_Ideograph=True */
        else if (is_unified_ideograph(cp)) {
            if (is_Block_CJK_Unified_Ideographs_OR_CJK_Compatibility_Ideographs(cp)) {
                AAAA = compute_AAAA(cp, 0xFB40);
                BBBB = compute_BBBB_and(cp);
                DEBUG_SPECIAL_PUSHED(block_pushed, "Ideograph_CJK_Compatibility_OR_Unified");
            }
            /* All other Unified_Ideograph's */
            else {
                AAAA = compute_AAAA(cp, 0xFB80);
                BBBB = compute_BBBB_and(cp);
                DEBUG_SPECIAL_PUSHED(block_pushed, "Ideograph_NOT_CJK_Compatibility_OR_Unified");
            }
        }
        else {
            MVMint32 NFD_rtrn = NFD_and_push_collation_values(tc, cp, stack, ci, name);
            if (NFD_rtrn) {
                return;
            }
            else {
                AAAA = compute_AAAA(cp, 0xFBC0);
                BBBB = compute_BBBB_and(cp);
                DEBUG_SPECIAL_PUSHED(block_pushed, "Unassigned");
            }
        }
        calculated_key[0].s.primary = AAAA;
        calculated_key[1].s.primary = BBBB;
        DEBUG_PRINT_SPECIAL_PUSHED(block_pushed, name, cp);
        push_onto_stack(tc, stack, calculated_key, 2, name);
    }
    else {
        push_key_to_stack(stack,
            MVM_coll_key.s.primary,
            MVM_coll_key.s.secondary,
            MVM_coll_key.s.tertiary);
    }
}
/* This is passed the terminal node and it adds the collation elements linked from
 * that node to the collation stack
 * Returns:
    1 collation elements from last_node were used
    0 collation elements from the first node were used, or it fell back and used collation_push_MVM_values
 * Essentially the return value lets you know if it ended up pushing collation values for the last codepoint
 * in the sequence or if it only pushed collation values for fallback_cp
*/
static int collation_add_keys_from_node (MVMThreadContext *tc, sub_node *last_node, collation_stack *stack, MVMCodepointIter *ci, char *name, MVMCodepoint fallback_cp, sub_node *first_node) {
    MVMint64 j;
    MVMint64 rtrn = 0;
    sub_node *choosen_node = NULL;
    /* If there are any collation elements */
    if (last_node && last_node->collation_key_elems) {
        choosen_node = last_node;
        rtrn = 1;
    }
    else if (first_node && first_node->collation_key_elems) {
        choosen_node = first_node;
    }
    if (choosen_node) {
        for (j = choosen_node->collation_key_link;
             j < choosen_node->collation_key_link + choosen_node->collation_key_elems;
             j++) {
            push_key_to_stack(stack, special_collation_keys[j].primary   + 1,
                special_collation_keys[j].secondary + 1,
                special_collation_keys[j].tertiary  + 1
            );
        }
        return rtrn;
    }
    /* Terminal node doesn't have any collation data. Fall back to using collation_push_MVM_values() */
    collation_push_MVM_values(tc, fallback_cp, stack, ci, name);
    return rtrn;
}
static MVMint64 find_next_node (MVMThreadContext *tc, sub_node node, MVMCodepoint next_cp) {
    MVMint64 next_min, next_max;
    MVMint64 i;
    /* There is nowhere else to go */
    if (!node.sub_node_elems)
        return -1;
    next_min = next_node_min(node);
    next_max = next_node_max(node);
    /* It's not within bounds */
    if (next_cp < next_min || next_max < next_cp)
        return -1;
    for (i = node.sub_node_link; i < node.sub_node_link + node.sub_node_elems; i++) {
        if (main_nodes[i].codepoint == next_cp)
            return i;
    }
    return -1;
}
static MVMint64 get_main_node (MVMThreadContext *tc, int cp, int range_min, int range_max) {
    MVMint64 i;
    MVMint64 rtrn = -1;
    /* Decrement range_min because binary search defaults to 1..* not 0..* */
    range_min--;
    /* starter_main_nodes_elems are all the nodes which are the origin nodes
     * searches using binary search */
    /* Start range_min at -1 since the lowest node we have to find is at 0, not
     * 1 (needed for binary search to work) */
    for (range_min = -1, range_max = starter_main_nodes_elems; 1 < range_max - range_min;) {
        i = (range_min + range_max) / 2;
        if (cp  <= main_nodes[i].codepoint)
            range_max = i;
        else range_min = i;
    }
    /* Final check is here. If we found it, it will match, otherwise not */
    if (main_nodes[range_max].codepoint == cp)
        rtrn = range_max;
    return rtrn;
}
/* Returns the number of added collation keys */
static MVMint64 collation_push_cp (MVMThreadContext *tc, collation_stack *stack, MVMCodepointIter *ci, int *cp_maybe, int cp_num, char *name) {
    MVMCodepoint cps[10];
    MVMint64 num_cps_processed = 0;
    int query = -1;
    /* If supplied -1 that means we need to grab it from the codepoint iterator. Otherwise
     * the value we were passed is the codepoint we should process */
    if (cp_num == 0) {
        cps[0] = MVM_string_ci_get_codepoint(tc, ci);
        cp_num = 1;
    }
    else {
        MVMint32 i;
        for (i = 0; i < cp_num; i++) {
            cps[i] = cp_maybe[i];
        }
    }
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    query = get_main_node(tc, cps[0], 0, starter_main_nodes_elems);
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif
    if (query != -1) {
        DEBUG_PRINT_SUB_NODE(main_nodes[query]);
        /* If there are no sub_node_elems that means we don't need to look at
         * the next codepoint, we are already at the correct node
         * If there's no more codepoints in the iterator we also are done here */
        if (main_nodes[query].sub_node_elems < 1 || (cp_num < 2 && !MVM_string_ci_has_more(tc, ci))) {
            collation_add_keys_from_node(tc, NULL, stack, ci, name, cps[0],  &main_nodes[query]);
            num_cps_processed++;
        }
        /* Otherwise we need to check the next codepoint(s) (0 < sub_node_elems) */
        else {
            MVMint64 last_good_i      =  0,
                     last_good_result = -1;
            MVMint64 i, result = query;
            DEBUG_PRINT_SUB_NODE(main_nodes[query]);
            for (i = 0; result != -1 && MVM_string_ci_has_more(tc, ci) && i < 9;) {
                i++;
                /* Only grab a codepoint if it doesn't already exist in the array */
                if (cp_num <= i) {
                    cps[i] = MVM_string_ci_get_codepoint(tc, ci);
                    cp_num++;
                }
                result = find_next_node(tc, main_nodes[result], cps[i]);
                /* If we got something other than -1 and it has collation elements
                 * store the value so we know how far is valid */
                if (result != -1 && main_nodes[result].collation_key_elems != 0) {
                    last_good_i      = i;
                    last_good_result = result;
                }
                if (result != -1) {
                    DEBUG_PRINT_SUB_NODE(main_nodes[result]);
                }
            }
            /* If there is no last_good_result we should return a value from main_nodes */
            DEBUG_PRINT_SUB_NODE( (last_good_result == -1 ? main_nodes[query] : main_nodes[last_good_result]) );
            /* If the terminal_subnode can't be processed then that means it will push the starter codepoint ( cp[0] )'s value onto
             * the stack, and we must set last_good_i to 0 since it didn't work out */
            if (!collation_add_keys_from_node(tc, (last_good_result == -1 ? NULL : &main_nodes[last_good_result]), stack, ci, name, cps[0], &main_nodes[query])) {
                /* If we get 0 from collation_add_keys_from_node then we only processed
                 * a single codepoint so set last_good_i to 0 */
                last_good_i = 0;
            }
            num_cps_processed = last_good_i + 1;
        }
    }
    else {
        /* Push the first codepoint onto the stack */
        collation_push_MVM_values(tc, cps[0], stack, ci, name);
        num_cps_processed = 1;
    }
    /* If there are any more codepoints remaining call collation_push_cp on the remaining */
    if (num_cps_processed < cp_num) {
        return num_cps_processed + collation_push_cp(tc, stack, ci, cps + num_cps_processed, cp_num - num_cps_processed, name);
    }
    return num_cps_processed;
}
static int grab_from_stack(MVMThreadContext *tc, MVMCodepointIter *ci, collation_stack *stack, char *name) {
    if (!MVM_string_ci_has_more(tc, ci))
        return 0;
    collation_push_cp(tc, stack, ci, NULL, 0, name);
    return 1;
}
static void init_ringbuffer (MVMThreadContext *tc,  ring_buffer *buffer) {
    buffer->count           =  0;
    buffer->location        = -1;
    buffer->codes_out_count =  0;
}
static void add_to_ring_buffer(MVMThreadContext *tc, ring_buffer *buffer, MVMCodepoint cp) {
    buffer->location++;
    if (codepoint_sequence_no_max <= buffer->location)
        buffer->location = 0;
    buffer->codes[buffer->location] = cp;
    buffer->count++;
}
/* Takes the ring buffer and puts the codepoints in order */
static void ring_buffer_done(MVMThreadContext *tc, ring_buffer *buffer) {
    buffer->codes_out_count = codepoint_sequence_no_max < buffer->count ? codepoint_sequence_no_max : buffer->count;
    /* If the buffer hasn't been wraped yet or the last codepoint was pushed onto the last buffer element,
     * use memcpy to copy it to the out buffer */
    if (buffer->count <= codepoint_sequence_no_max || buffer->location == codepoint_sequence_no_max - 1) {
        memcpy(buffer->codes_out, buffer->codes, sizeof(MVMCodepoint) * buffer->codes_out_count);
    }
    /* Otherwise we need to copy it manually */
    else {
        /* Copy backwards from the last copied to the first copied codepoint in the ring buffer */
        MVMint32 out_location     = buffer->codes_out_count - 1;
        MVMint32 buf_location     = buffer->location;
        for (; 0 <= out_location; out_location--) {
            buffer->codes_out[out_location] = buffer->codes[buf_location];
            buf_location--;
            if (buf_location < 0)
                buf_location = codepoint_sequence_no_max - 1;
        }
    }
}
static MVMint64 collation_return_by_quaternary(MVMThreadContext *tc, level_eval *level_eval_settings,
    MVMStringIndex length_a, MVMStringIndex length_b, MVMint64 compare_by_cp_rtrn) {
    if (compare_by_cp_rtrn) {
        return compare_by_cp_rtrn == -1 ? level_eval_settings->s.quaternary.s2.Less :
               compare_by_cp_rtrn ==  1 ? level_eval_settings->s.quaternary.s2.More :
                                          level_eval_settings->s.quaternary.s2.Same ;
    }
    else {
        return length_a < length_b ? level_eval_settings->s.quaternary.s2.Less :
               length_b < length_a ? level_eval_settings->s.quaternary.s2.More :
                                     level_eval_settings->s.quaternary.s2.Same ;
    }
}
/* MVM_unicode_string_compare implements the Unicode Collation Algorthm */
MVMint64 MVM_unicode_string_compare(MVMThreadContext *tc, MVMString *a, MVMString *b,
         MVMint64 collation_mode, MVMint64 lang_mode, MVMint64 country_mode) {
    MVMStringIndex alen, blen;
    /* Iteration variables */
    MVMCodepointIter a_ci, b_ci;
    /* Set it all to 0 to start with. We alter this based on the collation_mode later on */
    level_eval level_eval_settings = {
        { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} }
    };
    /* The default level_eval settings, used between two non-equal levels */
    union level_eval_u2 level_eval_default = {
        {-1, 0, 1}
    };
    /* Collation stacks */
    collation_stack stack_a;
    collation_stack stack_b;

    ring_buffer buf_a, buf_b;
    /* This value stores what the return value would be if the strings were compared
     * by codepoint. This is used to break collation value ties */
    MVMint64 compare_by_cp_rtrn = 0;
    MVMint64 pos_a = 0, pos_b = 0, rtrn = 0;
    MVMint16 grab_a_done = 0, grab_b_done = 0;
    /* From 0 to 2 for primary, secondary, tertiary levels */
    MVMint16   level_a = 0,   level_b = 0;
    MVMint64 skipped_a = 0, skipped_b = 0;
    /* This code sets up level_eval_settings based on the collation_mode */
    #define setmodeup(mode, level, Less, Same, More) {\
        if (collation_mode & mode) {\
            level_eval_settings.a[level].a2[0] +=  Less;\
            level_eval_settings.a[level].a2[1] +=  Same;\
            level_eval_settings.a[level].a2[2] +=  More;\
        }\
    }
    /* Primary */
    setmodeup(MVM_COLLATION_PRIMARY_POSITIVE,    0, -1, 0,  1);
    setmodeup(MVM_COLLATION_PRIMARY_NEGATIVE,    0,  1, 0, -1);
    /* Secondary */
    setmodeup(MVM_COLLATION_SECONDARY_POSITIVE,  1, -1, 0,  1);
    setmodeup(MVM_COLLATION_SECONDARY_NEGATIVE,  1,  1, 0, -1);
    /* Tertiary */
    setmodeup(MVM_COLLATION_TERTIARY_POSITIVE,   2, -1, 0,  1);
    setmodeup(MVM_COLLATION_TERTIARY_NEGATIVE,   2,  1, 0, -1);
    /* Quaternary */
    setmodeup(MVM_COLLATION_QUATERNARY_POSITIVE, 3, -1, 0,  1);
    setmodeup(MVM_COLLATION_QUATERNARY_NEGATIVE, 3,  1, 0, -1);
    DEBUG_COLLATION_MODE_PRINT(level_eval_settings);

    init_stack(tc, &stack_a);
    init_stack(tc, &stack_b);
    MVM_string_check_arg(tc, a, "compare");
    MVM_string_check_arg(tc, b, "compare");
    /* Simple cases when one or both are zero length. */
    alen = MVM_string_graphs_nocheck(tc, a);
    blen = MVM_string_graphs_nocheck(tc, b);
    if (alen == 0 || blen == 0)
        return collation_return_by_quaternary(tc, &level_eval_settings, alen, blen, 0);

    /* Initialize a codepoint iterator
     * For now we decompose utf8-c8 synthetics. Eventually we may want to pass
     * them back and choose some way to generate sorting info for them, similar
     * to how Unassigned codepoints are dealt with */
    MVM_string_ci_init(tc, &a_ci, a, 0, 0);
    MVM_string_ci_init(tc, &b_ci, b, 0, 0);

    init_ringbuffer(tc, &buf_a);
    init_ringbuffer(tc, &buf_b);
    /* The ring buffers hold the exact number of codepoints which comprise the longest
     * sequence of codepoints which map to its own collation keys in the Unicode
     * Collation Algorithm. As of Unicode 9.0 this number was 3. The number is
     * generated by the script that generates the C data so we only need to retain
     * that many codepoints. TODO actually generate codepoint_sequence_no_max */
    while (MVM_string_ci_has_more(tc, &a_ci) && MVM_string_ci_has_more(tc, &b_ci)) {
        MVMCodepoint cp_a = MVM_string_ci_get_codepoint(tc, &a_ci);
        MVMCodepoint cp_b = MVM_string_ci_get_codepoint(tc, &b_ci);
        add_to_ring_buffer(tc, &buf_a, cp_a);
        add_to_ring_buffer(tc, &buf_b, cp_b);
        if (cp_a != cp_b) {
            compare_by_cp_rtrn = cp_a < cp_b ? -1 :
                                 cp_b < cp_a ?  1 :
                                                0 ;
            break;
        }
    }
    DEBUG_PRINT_RING_BUFFER(tc, &buf_a);
    DEBUG_PRINT_RING_BUFFER(tc, &buf_b);
    ring_buffer_done(tc, &buf_a);
    ring_buffer_done(tc, &buf_b);
    collation_push_cp(tc, &stack_b, &b_ci, buf_b.codes_out, buf_b.codes_out_count, "b");
    DEBUG_PRINT_STACK(tc, &stack_b, "b", "After Initial grab b");
    collation_push_cp(tc, &stack_a, &a_ci, buf_a.codes_out, buf_a.codes_out_count, "a");
    DEBUG_PRINT_STACK(tc, &stack_a, "a", "After Initial grab a");

    while (rtrn == 0) {
        while (pos_a <= stack_a.stack_top && pos_b <= stack_b.stack_top) {
            /* Collation values are set as 1 (collation_zero) higher than what Unicode designates. So a collation value of 1 is ignored as
             * a 0 in DUCET would be. Whereas a collation value of 0 cannot be skipped and is used only for the tertiary level
             * of a level separator so it is evaluated as the end of the string, causing the shorter string to win */
            if (stack_a.keys[pos_a].a[level_a] == collation_zero) {
                pos_a++;
                skipped_a++;
                continue;
            }
            if (stack_b.keys[pos_b].a[level_b] == collation_zero) {
                pos_b++;
                skipped_b++;
                continue;
            }
            /* If collation values are not equal */
            if (stack_a.keys[pos_a].a[level_a] != stack_b.keys[pos_b].a[level_b]) {
                union level_eval_u2 effective_level_eval = level_eval_default;
                /* Aside from ignored (collation_zero) levels, when all primary values
                 * are greater than any secondary values. All secondary values are greater
                 * than any tertiary values. Because of this, we only need to tailor effective_level_evals if the levels match */
                if (level_a == level_b)
                    effective_level_eval = level_eval_settings.a[level_a];
                rtrn = stack_a.keys[pos_a].a[level_a] < stack_b.keys[pos_b].a[level_b] ?  effective_level_eval.s2.Less :
                       stack_a.keys[pos_a].a[level_a] > stack_b.keys[pos_b].a[level_b] ?  effective_level_eval.s2.More :
                                                                                          effective_level_eval.s2.Same ;
                DEBUG_PRINT_STACK(tc, &stack_a, "a", "Collation values found not equal");
                DEBUG_PRINT_STACK(tc, &stack_b, "b", "Collation values found not equal");
            }
            if (rtrn != 0) {
                cleanup_stack(tc, &stack_a);
                cleanup_stack(tc, &stack_b);
                return rtrn;
            }
            pos_a++;
            pos_b++;
        }
        #define if_grab_done(grab_done, stack, ci, pos, level, name) {\
            /* If we haven't grabbed all the collation elements we should grab them */\
            if (!grab_done) {\
                if (!grab_from_stack(tc, &ci, &stack, name)) {\
                    collation_push_level_separator(tc, &stack, name);\
                    grab_done = 1;\
                }\
            }\
            /* Here we check if we've already grabbed everything. If we have \
             * grabbed we need to move to the next collation level for that \
             * stack only      TODO, right hand side of conditional needed or not? */  \
            if (grab_done && stack.stack_top < pos) {\
                /* Only if we're already not on the highest level */\
                if (level < 2) {\
                    pos = 0;\
                    level++;\
                    DEBUG_PRINT("Setting level_%s to %"PRIi16" and pos_%s to %"PRIi64". %s_keys_pushed: %li\n",\
                                            name,      level,   name, pos,name, stack.stack_top + 1);\
                }\
                else {\
                    /* TODO get the names of the strings can't wrap in the debug */ \
                    DEBUG_PRINT_STACK(tc, &stack_a, "a", "Can't wrap string anymore so breaking");\
                    DEBUG_PRINT_STACK(tc, &stack_b, "b", "Can't wrap string anymore so breaking");\
                    break;\
                }\
            }\
        }
        /* Here we wrap to the next level of collation elements if needed */
        if_grab_done(grab_b_done, stack_b, b_ci, pos_b, level_b, "b");
        if_grab_done(grab_a_done, stack_a, a_ci, pos_a, level_a, "a");
    }
    cleanup_stack(tc, &stack_a);
    cleanup_stack(tc, &stack_b);
    /* If we get here, they tied for all levels including the level separator
     * [0001.0001.0000] The primary and secondary of the level separator we push
     * get ignored, but the tertiary level value 0 is not ignored. No other values
     * have 0, so that means those levels must have matched up.*/

    /* The tie must be broken by codepoint or length. Use the return value we computed at
     * the beginning of the function while we were pushing onto the ring buffers */
    return collation_return_by_quaternary(tc, &level_eval_settings, alen, blen, compare_by_cp_rtrn);
}

/* Looks up a codepoint by name. Lazily constructs a hash. */
MVMGrapheme32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name) {
    char *cname = MVM_string_utf8_encode_C_string(tc, name);
    if (MVM_uni_hash_is_empty(tc, &tc->instance->codepoints_by_name)) {
        generate_codepoints_by_name(tc);
    }
    struct MVMUniHashEntry *result = MVM_uni_hash_fetch(tc, &tc->instance->codepoints_by_name, cname);
    if (!result) {
        #define prefixes_len 7
        const char *prefixes[prefixes_len] = {
            "CJK UNIFIED IDEOGRAPH-",
            "CJK COMPATIBILITY IDEOGRAPH-",
            "<CONTROL-",
            "<RESERVED-",
            "<SURROGATE-",
            "<PRIVATE-USE-",
            "TANGUT IDEOGRAPH-"
        };
        size_t cname_len = strlen((const char *) cname );
        int i;
        for (i = 0; i < prefixes_len; i++) {
            size_t str_len = strlen(prefixes[i]);
            if (cname_len <= str_len)
                continue;
            /* Make sure to catch conditions which strtoll is ok with but we
             * don't want to allow. So don't allow leading space, or -/+ and don't
             * allow 0x */
            if (cname[str_len] == '-' || cname[str_len] == '+' || cname[str_len] == ' ')
                continue;
            if (cname_len >= str_len + 2 && cname[str_len+1] == 'X')
                continue;
            if (!strncmp(cname, prefixes[i], str_len)) {
                char *reject = NULL;
                MVMint64 rtrn = strtol(cname + strlen(prefixes[i]), &reject, 16);
                if (prefixes[i][0] == '<' && *reject == '>' && (size_t)(reject - cname + 1) == cname_len) {
                    MVM_free(cname);
                    return rtrn;
                }
                else if ((*reject == '\0' && !(rtrn == 0 && reject == cname + str_len))) {
                    MVM_free(cname);
                    return rtrn;
                }
            }
        }
    }
    MVM_free(cname);
    return result ? result->value : -1;
}
/* Quickly determines the length of a number 6.5x faster than doing log10 after
 * compiler optimization */
MVM_STATIC_INLINE size_t length_of_num (size_t number) {
    if (number < 10) return 1;
    return 1 + length_of_num(number / 10);
}
MVM_STATIC_INLINE size_t length_of_num_16 (size_t number) {
    if (number < 16) return 1;
    return 1 + length_of_num_16(number / 16);
}
MVMString * MVM_unicode_get_name(MVMThreadContext *tc, MVMint64 codepoint) {
    const char *name = NULL;
    size_t name_len = 0;

    /* Catch out-of-bounds code points. */
    if (codepoint < 0) {
        name = "<illegal>";
    }
    else if (0x10FFFF < codepoint) {
        name = "<unassigned>";
    }
    if (name)
        name_len = strlen(name);
    /* Look up name. */
    else {
        MVMint32 codepoint_row = MVM_codepoint_to_row_index(tc, codepoint);
        if (codepoint_row != -1) {
            name = codepoint_names[codepoint_row];
        }
        if (!name) {
            /* U+FDD0..U+FDEF and the last two codepoints of each block
             * are noncharacters (U+FFFE U+FFFF U+1FFFE U+1FFFF U+2FFFE etc.) */
            if ((0xFDD0 <= codepoint && codepoint <= 0xFDEF) || (0xFFFE & codepoint) == 0xFFFE)
                name = "<noncharacter>";
            else
                name = "<reserved>";
        }
        name_len = strlen(name);
        /* Turn non-unique codepoint names into unique ones by adding the
         * codepoint
         * i.e. <CJK UNIFIED IDEOGRAPH> → CJK UNIFIED IDEOGRAPH-20000
         *      <control> → <control-0000> */
        if (name && name[0] == '<') {
            size_t i, new_length, num_len = length_of_num_16(codepoint);
            char *new_name = NULL;
            int remove_brack = !strncmp(name, "<CJK", 4) ||
                !strncmp(name, "<TANGUT", 7) ? 1 : 0;
            /* We pad to 4 width, so make sure the number is accurate */
            num_len = num_len < 4 ? 4 : num_len;
            /* The new_length is 1 more than we need since snprintf adds a null */
            new_length = !remove_brack + name_len + num_len * sizeof(char);
            new_name = alloca(new_length);
            for (i = 0; i < name_len; i++) {
                if (name[i] == '>') {
                    snprintf(new_name + i - remove_brack, new_length - (i - remove_brack) ,
                        "-%.4"PRIX32"", (MVMuint32)codepoint);
                    if (!remove_brack) {
                        new_name[new_length-1] = '>';
                    }
                    /* snprintf adds a null terminator at the end. We don't need
                     * this, so replace with a > instead of using snprintf to add
                     * it. Note: new has no NULL terminator */
                    break;
                }
                new_name[i] = name[i+remove_brack];
            }
            name     = new_name;
            name_len = new_length - remove_brack;
        }
    }

    return MVM_string_ascii_decode(tc, tc->instance->VMString, name, name_len);
}

MVMString * MVM_unicode_codepoint_get_property_str(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    const char * const str = MVM_unicode_get_property_str(tc, codepoint, property_code);

    if (!str)
        return tc->instance->str_consts.empty;

    return MVM_string_ascii_decode(tc, tc->instance->VMString, str, strlen(str));
}

const char * MVM_unicode_codepoint_get_property_cstr(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    return MVM_unicode_get_property_str(tc, codepoint, property_code);
}
MVMint64 MVM_unicode_codepoint_get_property_int(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    if (MVM_LIKELY(property_code != 0))
        return (MVMint64)MVM_unicode_get_property_int(tc, codepoint, property_code);
    return 0;
}

MVMint64 MVM_unicode_codepoint_get_property_bool(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    if (MVM_LIKELY(property_code != 0))
        return (MVMint64)MVM_unicode_get_property_int(tc, codepoint, property_code) != 0;
    return 0;
}

MVMint64 MVM_unicode_codepoint_has_property_value(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code, MVMint64 property_value_code) {
    if (MVM_LIKELY(property_code != 0)) {
        return (MVMint64)MVM_unicode_get_property_int(tc,
            codepoint, property_code) == property_value_code;
    }
    return 0;
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
                while (0 < i && CaseFolding_grows_table[folding_index][i - 1] == 0)
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
                while (0 < i && SpecialCasing_table[special_casing_index][case_][i - 1] == 0)
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

static void generate_property_codes_by_names_aliases(MVMThreadContext *tc) {
    MVMuint32 num_names = num_unicode_property_keypairs;

    uv_mutex_lock(&tc->instance->mutex_property_codes_hash_setup);
    if (MVM_uni_hash_is_empty(tc, &tc->instance->property_codes_by_names_aliases)) {
        MVM_uni_hash_build(tc, &tc->instance->property_codes_by_names_aliases, num_names);

        while (num_names--) {
            MVM_uni_hash_insert(tc, &tc->instance->property_codes_by_names_aliases,
                                unicode_property_keypairs[num_names].name,
                                unicode_property_keypairs[num_names].value);
        }
    }
    uv_mutex_unlock(&tc->instance->mutex_property_codes_hash_setup);
}
static void generate_property_codes_by_seq_names(MVMThreadContext *tc) {
    MVMuint32 num_names = num_unicode_seq_keypairs;

    uv_mutex_lock(&tc->instance->mutex_property_codes_hash_setup);
    if (MVM_uni_hash_is_empty(tc, &tc->instance->property_codes_by_seq_names)) {
        MVM_uni_hash_build(tc, &tc->instance->property_codes_by_seq_names, num_names);

        while (num_names--) {
            MVM_uni_hash_insert(tc, &tc->instance->property_codes_by_seq_names,
                                uni_seq_pairs[num_names].name,
                                uni_seq_pairs[num_names].value);
        }
    }
    uv_mutex_unlock(&tc->instance->mutex_property_codes_hash_setup);
}

MVMint64 MVM_unicode_name_to_property_code(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    char *cname = MVM_string_ascii_encode(tc, name, &size, 0);
    if (MVM_uni_hash_is_empty(tc, &tc->instance->property_codes_by_names_aliases)) {
        generate_property_codes_by_names_aliases(tc);
    }
    struct MVMUniHashEntry *result = MVM_uni_hash_fetch(tc, &tc->instance->property_codes_by_names_aliases, cname);
    MVM_free(cname);
    return result ? result->value : 0;
}

void MVM_unicode_init(MVMThreadContext *tc) {
    /* A bit of a hack - we're relying on the implementation detail that zeroes
     * is a valid start state for the hash. */
    MVMUniHashTable *hash_array = MVM_calloc(MVM_NUM_PROPERTY_CODES, sizeof(MVMUniHashTable));
    MVMuint32 index = 0;
    for ( ; index < num_unicode_property_value_keypairs; index++) {
        MVMint32 property_code = unicode_property_value_keypairs[index].value >> 24;
        MVM_uni_hash_insert(tc, &hash_array[property_code],
                            unicode_property_value_keypairs[index].name,
                            unicode_property_value_keypairs[index].value & 0xFFFFFF);
    }
    for (index = 0; index < MVM_NUM_PROPERTY_CODES; index++) {
        if (MVM_uni_hash_is_empty(tc, &hash_array[index])) {
            MVMUnicodeNamedValue yes[8] = { {"T",1}, {"Y",1},
                {"Yes",1}, {"yes",1}, {"True",1}, {"true",1}, {"t",1}, {"y",1} };
            MVMUnicodeNamedValue no [8] = { {"F",0}, {"N",0},
                {"No",0}, {"no",0}, {"False",0}, {"false",0}, {"f",0}, {"n",0} };
            MVMuint8 i;
            for (i = 0; i < 8; i++) {
                MVM_uni_hash_insert(tc, &hash_array[index], yes[i].name, yes[i].value);
            }
            for (i = 0; i < 8; i++) {
                MVM_uni_hash_insert(tc, &hash_array[index], no[i].name, no[i].value);
            }
        }
    }
    tc->instance->unicode_property_values_hashes = hash_array;
}
static MVMint32 unicode_cname_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, const char *cname, MVMuint64 cname_length) {
    char *out_str = NULL;
                                   /* number + dash + property_value + NULL */
    MVMuint64 out_str_length = length_of_num(property_code) + 1 + cname_length + 1;
    if (1024 < out_str_length)
        MVM_exception_throw_adhoc(tc, "Property value or name queried (%"PRIu64") is larger than allowed (1024).", out_str_length);

    out_str = alloca(sizeof(char) * out_str_length);
    snprintf(out_str, out_str_length, "%"PRIi64"-%s", property_code, cname);

    struct MVMUniHashEntry *result = MVM_uni_hash_fetch(tc,
                                                        &tc->instance->unicode_property_values_hashes[property_code],
                                                        out_str);
    return result ? result->value : 0;
}
MVMint64 MVM_unicode_name_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, MVMString *name) {
    if (property_code <= 0 || MVM_NUM_PROPERTY_CODES <= property_code)
        return 0;
    else {
        MVMuint64 cname_length;
        char *cname = MVM_string_ascii_encode(tc, name, &cname_length, 0);
        MVMint32 code = unicode_cname_to_property_value_code(tc, property_code, cname, cname_length);
        MVM_free(cname);
        return code;
    }
}
MVMint32 MVM_unicode_cname_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, const char *cname, size_t cname_length) {
    if (property_code <= 0 || MVM_NUM_PROPERTY_CODES <= property_code)
        return 0;
    else
        return unicode_cname_to_property_value_code(tc, property_code, cname, cname_length);
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

/* Looks up a codepoint sequence or codepoint by name (case insensitive).
 First tries to look it up by codepoint with MVM_unicode_lookup_by_name and if
 not found as a named codepoint, lazily constructs a hash of the codepoint
 sequences and looks up the sequence name */
MVMString * MVM_unicode_string_from_name(MVMThreadContext *tc, MVMString *name) {
    MVMString * name_uc = MVM_string_uc(tc, name);

    MVMGrapheme32 result_graph = MVM_unicode_lookup_by_name(tc, name_uc);
    /* If it's just a codepoint, return that */
    if (0 <= result_graph) {
        return MVM_string_chr(tc, result_graph);
    }
    /* Otherwise look up the sequence */
    else {
        const MVMint32 *uni_seq = NULL;
        char *cname = MVM_string_utf8_encode_C_string(tc, name_uc);
        if (MVM_uni_hash_is_empty(tc, &tc->instance->property_codes_by_seq_names)) {
            generate_property_codes_by_seq_names(tc);
        }
        struct MVMUniHashEntry *result = MVM_uni_hash_fetch(tc,
                                                            &tc->instance->property_codes_by_seq_names,
                                                            cname);
        MVM_free(cname);
        /* If we can't find a result return an empty string */
        if (!result)
            return tc->instance->str_consts.empty;

        uni_seq = uni_seq_enum[result->value];
        /* The first element is the number of codepoints in the sequence */
        return MVM_unicode_codepoints_c_array_to_nfg_string(tc, (MVMCodepoint *) uni_seq + 1, uni_seq[0]);
    }

}
