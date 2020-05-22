#include "moar.h"

/* Allocates a dispatcher table. */
MVMDispRegistryTable * allocate_table(MVMThreadContext *tc, MVMuint32 num_entries) {
    MVMDispRegistryTable *table = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
            sizeof(MVMDispRegistryTable));
    table->num_dispatchers = 0;
    table->alloc_dispatchers = num_entries;
    table->dispatchers = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
            table->alloc_dispatchers * sizeof(MVMDispDefinition *));
    return table;
}

/* Hashes and adds an entry to the registry. */
static void add_to_table(MVMThreadContext *tc, MVMDispRegistryTable *table,
        MVMDispDefinition *def) {
    size_t slot = (size_t)(MVM_string_hash_code(tc, def->id) % table->alloc_dispatchers);
    while (table->dispatchers[slot] != NULL)
        slot = (slot + 1) % table->alloc_dispatchers;
    table->dispatchers[slot] = def;
    table->num_dispatchers++;
}

/* We keep the registry at maximum 75% load to avoid collisions. */
static void grow_registry_if_needed(MVMThreadContext *tc) {
    MVMDispRegistry *reg = &(tc->instance->disp_registry);
    MVMDispRegistryTable *current_table = reg->table;
    if ((double)current_table->num_dispatchers / (double)current_table->alloc_dispatchers >= 0.75) {
        /* Copy entries to new table. */
        MVMDispRegistryTable *new_table = allocate_table(tc, current_table->alloc_dispatchers * 2);
        MVMuint32 i;
        for (i = 0; i < current_table->alloc_dispatchers; i++)
            if (current_table->dispatchers[i])
                add_to_table(tc, new_table, current_table->dispatchers[i]);

        /* Install the new table. */
        reg->table = new_table;

        /* Free the previous table at the next safepoint. */
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                current_table->alloc_dispatchers * sizeof(MVMDispDefinition *),
                current_table->dispatchers);
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa,
                sizeof(MVMDispRegistryTable), current_table);
    }
}

/* The core part of registering a new dispatcher. Assumes we are either in
 * the setup phase *or* we hold the mutex for registering dispatchers if it's
 * a user-defined one. Also that the REPR of the dispatch (and, if non-null,
 * resume) dispatcher is MVMCFunction or MVMCode. */
static void register_internal(MVMThreadContext *tc, MVMString *id, MVMObject *dispatch,
        MVMObject *resume) {
    MVMDispRegistry *reg = &(tc->instance->disp_registry);

    /* Allocate and populate the dispatch definition. */
    MVMDispDefinition *def = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(MVMDispDefinition));
    def->id = id;
    def->dispatch = dispatch;
    def->resume = resume != NULL && IS_CONCRETE(resume) ? resume : NULL;

    /* Insert into the registry. */
    grow_registry_if_needed(tc);
    add_to_table(tc, reg->table, def);
}

/* Registers a boot dispatcher (that is, one provided by the VM). */
static void register_boot_dispatcher(MVMThreadContext *tc, const char *id, MVMObject *dispatch) {
    MVMString *id_str = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, id);
    register_internal(tc, id_str, dispatch, NULL);
}

/* Initialize the dispatcher registry and add all of the boot dispatchers. */
void MVM_disp_registry_init(MVMThreadContext *tc) {
    MVMDispRegistry *reg = &(tc->instance->disp_registry);

    /* Set up dispatchers table with hopefully enough slots we don't tend to
     * need to expand it. */
    reg->table = allocate_table(tc, 32);

    /* Set up mutex. */
    int init_stat;
    if ((init_stat = uv_mutex_init(&(reg->mutex_update))) < 0) {
        fprintf(stderr, "MoarVM: Initialization of dispatch registry mutex failed\n    %s\n",
            uv_strerror(init_stat));
        exit(1);
    }

    /* Add each of the boot dispatchers. */
    MVM_gc_allocate_gen2_default_set(tc);
    register_boot_dispatcher(tc, "boot-constant", MVM_disp_boot_constant_dispatch(tc));
    register_boot_dispatcher(tc, "boot-value", MVM_disp_boot_value_dispatch(tc));
    register_boot_dispatcher(tc, "boot-code-constant", MVM_disp_boot_code_constant_dispatch(tc));
    register_boot_dispatcher(tc, "boot-syscall", MVM_disp_boot_syscall_dispatch(tc));
    MVM_gc_allocate_gen2_default_clear(tc);
}

/* Register a new dispatcher. */
void MVM_disp_registry_register(MVMThreadContext *tc, MVMString *id, MVMObject *dispatch,
        MVMObject *resume) {
    MVMDispRegistry *reg = &(tc->instance->disp_registry);
    if (REPR(dispatch)->ID != MVM_REPR_ID_MVMCode || !IS_CONCRETE(dispatch))
        MVM_exception_throw_adhoc(tc, "dispatch callback be an instance with repr MVMCode");
    if (resume && (REPR(resume)->ID != MVM_REPR_ID_MVMCode || !IS_CONCRETE(resume)))
        MVM_exception_throw_adhoc(tc, "resume callback be an instance with repr MVMCode");
    uv_mutex_lock(&(reg->mutex_update));
    register_internal(tc, id, dispatch, resume);
    uv_mutex_unlock(&(reg->mutex_update));
}

/* Find a dispatcher. Throws if there isn't one. */
MVMDispDefinition * MVM_disp_registry_find(MVMThreadContext *tc, MVMString *id) {
    MVMDispRegistry *reg = &(tc->instance->disp_registry);
    MVMDispRegistryTable *table = reg->table;
    size_t start_slot = (size_t)(MVM_string_hash_code(tc, id) % table->alloc_dispatchers);
    size_t cur_slot = start_slot;
    while (1) {
        MVMDispDefinition *disp = table->dispatchers[cur_slot];
        if (disp && MVM_string_equal(tc, disp->id, id))
            return disp;
        cur_slot = (cur_slot + 1) % table->alloc_dispatchers;
        if (cur_slot == start_slot) // Wrapped all the way around.
            break;
    }
    {
        char *c_id = MVM_string_utf8_encode_C_string(tc, id);
        char *waste[] = { c_id, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
                "No dispatcher registered with ID '%s'", c_id);
    }
}

/* Mark the dispatch registry. */
void MVM_disp_registry_mark(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMDispRegistry *reg = &(tc->instance->disp_registry);
    MVMDispRegistryTable *table = reg->table;
    size_t i;
    for (i = 0; i < table->alloc_dispatchers; i++) {
        MVMDispDefinition *disp = table->dispatchers[i];
        if (disp) {
            MVM_gc_worklist_add(tc, worklist, &(disp->id));
            MVM_gc_worklist_add(tc, worklist, &(disp->dispatch));
            MVM_gc_worklist_add(tc, worklist, &(disp->resume));
        }
    }
}

/* Tear down the dispatcher registry, freeing all memory associated with it. */
void MVM_disp_registry_destroy(MVMThreadContext *tc) {
    MVMDispRegistry *reg = &(tc->instance->disp_registry);
    MVMDispRegistryTable *table = reg->table;
    MVMuint32 i;
    for (i = 0; i < table->alloc_dispatchers; i++)
        if (table->dispatchers[i])
            MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMDispDefinition),
                    table->dispatchers[i]);
    MVM_fixed_size_free(tc, tc->instance->fsa,
            table->alloc_dispatchers * sizeof(MVMDispDefinition *),
            table->dispatchers);
    MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMDispRegistryTable), table);
    uv_mutex_destroy(&reg->mutex_update);
}
