/* The configuration for a particular High Level Language. Of note, this
 * indicates the types to use in various situations. */
typedef struct _MVMHLLConfig {
    /* The types the language wishes to get things boxed as. */
    MVMObject *int_box_type;
    MVMObject *num_box_type;
    MVMObject *str_box_type;

    /* The type to use for slurpy arrays. */
    MVMObject *slurpy_array_type;

    /* The type to use for slurpy hashes. */
    MVMObject *slurpy_hash_type;

    /* The type to use for array iteration (should have VMIter REPR). */
    MVMObject *array_iterator_type;

    /* The type to use for hash iteration (should have VMIter REPR). */
    MVMObject *hash_iterator_type;

    /* HLL name. */
    struct _MVMString *name;

    /* Inline handle to the hash in which this is stored. */
    UT_hash_handle hash_handle;
} MVMHLLConfig;

MVMHLLConfig *MVM_hll_get_config_for(MVMThreadContext *tc, struct _MVMString *name);
MVMObject *MVM_hll_set_config(MVMThreadContext *tc, struct _MVMString *name, MVMObject *config_hash);
MVMHLLConfig *MVM_hll_current(MVMThreadContext *tc);
