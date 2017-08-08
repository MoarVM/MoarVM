#include "moar.h"
#include "platform/time.h"

/* Initializes a new thread context. Note that this doesn't set up a
 * thread itself, it just creates the data structure that exists in
 * MoarVM per thread. */
MVMThreadContext * MVM_tc_create(MVMThreadContext *parent, MVMInstance *instance) {
    MVMThreadContext *tc = MVM_calloc(1, sizeof(MVMThreadContext));

    /* Associate with VM instance. */
    tc->instance = instance;

    /* Use default loop for main thread; create a new one for others. */
    if (instance->main_thread) {
        int r;

        tc->loop = MVM_calloc(1, sizeof(uv_loop_t));
        r = uv_loop_init(tc->loop);
        if (r < 0) {
            MVM_free(tc->loop);
            MVM_free(tc);
            MVM_exception_throw_adhoc(parent, "Could not create a new Thread: %s", uv_strerror(r));
        }
    } else {
        tc->loop = uv_default_loop();
    }

    /* Set up GC nursery. We only allocate tospace initially, and allocate
     * fromspace the first time this thread GCs, provided it ever does. */
    tc->nursery_tospace     = MVM_calloc(1, MVM_NURSERY_SIZE);
    tc->nursery_alloc       = tc->nursery_tospace;
    tc->nursery_alloc_limit = (char *)tc->nursery_alloc + MVM_NURSERY_SIZE;

    /* Set up temporary root handling. */
    tc->num_temproots   = 0;
    tc->alloc_temproots = MVM_TEMP_ROOT_BASE_ALLOC;
    tc->temproots       = MVM_malloc(sizeof(MVMCollectable **) * tc->alloc_temproots);

    /* Set up intergenerational root handling. */
    tc->num_gen2roots   = 0;
    tc->alloc_gen2roots = 64;
    tc->gen2roots       = MVM_malloc(sizeof(MVMCollectable *) * tc->alloc_gen2roots);

    /* Set up the second generation allocator. */
    tc->gen2 = MVM_gc_gen2_create(instance);

    /* The fixed size allocator also keeps pre-thread state. */
    MVM_fixed_size_create_thread(tc);

    /* Allocate an initial call stack region for the thread. */
    MVM_callstack_region_init(tc);

    /* Initialize random number generator state. */
    MVM_proc_seed(tc, (MVM_platform_now() / 10000) * MVM_proc_getpid(tc));

    /* Initialize frame sequence numbers */
    tc->next_frame_nr = 0;
    tc->current_frame_nr = 0;

    /* Initialize last_payload, so we can be sure it's never NULL and don't
     * need to check. */
    tc->last_payload = instance->VMNull;

    return tc;
}

/* Destroys a given thread context. This will also free the nursery.
 * This means that it must no longer be in use, at all; this can be
 * ensured by a GC run at thread exit that forces evacuation of all
 * objects from this nursery to the second generation. Only after
 * that is true should this be called. */
void MVM_tc_destroy(MVMThreadContext *tc) {
    /* We run once again (non-blocking) to eventually close filehandles. */
    uv_run(tc->loop, UV_RUN_NOWAIT);

    /* Free specialization state. */
    MVM_spesh_sim_stack_destroy(tc, tc->spesh_sim_stack);

    /* Free the nursery and finalization queue. */
    MVM_free(tc->nursery_fromspace);
    MVM_free(tc->nursery_tospace);
    MVM_free(tc->finalizing);

    /* Destroy the second generation allocator. */
    MVM_gc_gen2_destroy(tc->instance, tc->gen2);

    /* Destory the per-thread fixed size allocator state. */
    MVM_fixed_size_destroy_thread(tc);

    /* Destroy all callstack regions. */
    MVM_callstack_region_destroy_all(tc);

    /* Free the thread-specific storage */
    MVM_free(tc->gc_work);
    MVM_free(tc->temproots);
    MVM_free(tc->gen2roots);
    MVM_free(tc->finalize);

    /* Free any memory allocated for NFAs and multi-dim indices. */
    MVM_free(tc->nfa_done);
    MVM_free(tc->nfa_curst);
    MVM_free(tc->nfa_nextst);
    MVM_free(tc->nfa_fates);
    MVM_free(tc->nfa_longlit);
    MVM_free(tc->multi_dim_indices);

    /* Destroy the libuv event loop */
    uv_loop_delete(tc->loop);

    /* Free the thread context itself. */
    memset(tc, 0, sizeof(MVMThreadContext));
    MVM_free(tc);
}

/* Setting and clearing mutex to release on exception throw. */
void MVM_tc_set_ex_release_mutex(MVMThreadContext *tc, uv_mutex_t *mutex) {
    if (tc->ex_release_mutex)
        MVM_exception_throw_adhoc(tc, "Internal error: multiple ex_release_mutex");
    tc->ex_release_mutex = mutex;
}
void MVM_tc_release_ex_release_mutex(MVMThreadContext *tc) {
    if (tc->ex_release_mutex)
        uv_mutex_unlock(tc->ex_release_mutex);
    tc->ex_release_mutex = NULL;
}
void MVM_tc_clear_ex_release_mutex(MVMThreadContext *tc) {
    tc->ex_release_mutex = NULL;
}
