#include "moar.h"

/* Initializes a new thread context. Note that this doesn't set up a
 * thread itself, it just creates the data structure that exists in
 * MoarVM per thread. */
MVMThreadContext * MVM_tc_create(MVMInstance *instance) {
    MVMThreadContext *tc = calloc(1, sizeof(MVMThreadContext));

    /* Associate with VM instance. */
    tc->instance = instance;

    /* Set up GC nursery. */
    tc->nursery_fromspace   = calloc(1, MVM_NURSERY_SIZE);
    tc->nursery_tospace     = calloc(1, MVM_NURSERY_SIZE);
    tc->nursery_alloc       = tc->nursery_tospace;
    tc->nursery_alloc_limit = (char *)tc->nursery_alloc + MVM_NURSERY_SIZE;

    /* Set up temporary root handling. */
    tc->num_temproots   = 0;
    tc->alloc_temproots = 16;
    tc->temproots       = malloc(sizeof(MVMCollectable **) * tc->alloc_temproots);

    /* Set up intergenerational root handling. */
    tc->num_gen2roots   = 0;
    tc->alloc_gen2roots = 64;
    tc->gen2roots       = malloc(sizeof(MVMCollectable *) * tc->alloc_gen2roots);

    /* Set up the second generation allocator. */
    tc->gen2 = MVM_gc_gen2_create(instance);

    /* Set up table of per-static-frame chains. */
    /* XXX For non-first threads, make them start with the size of the
       main thread's table. or, look into lazily initializing this. */
    tc->frame_pool_table_size = MVMInitialFramePoolTableSize;
    tc->frame_pool_table = calloc(MVMInitialFramePoolTableSize, sizeof(MVMFrame *));

    tc->loop = instance->default_loop ? uv_loop_new() : uv_default_loop();

    /* Create a CallCapture for usecapture instructions in this thread (needs
     * special handling in initial thread as this runs before bootstrap). */
    if (instance->CallCapture)
        tc->cur_usecapture = MVM_repr_alloc_init(tc, instance->CallCapture);

    /* Initialize random number generator state. */
    MVM_proc_seed(tc, (MVM_platform_now() / 10000) * MVM_proc_getpid(tc));

#if MVM_HLL_PROFILE_CALLS
#define PROFILE_INITIAL_SIZE (1 << 29)
    tc->profile_data_size = PROFILE_INITIAL_SIZE;
    tc->profile_data = malloc(sizeof(MVMProfileRecord) * PROFILE_INITIAL_SIZE);
    tc->profile_index = 0;
#endif

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

    /* Free the nursery. */
    free(tc->nursery_fromspace);
    free(tc->nursery_tospace);

    /* Destroy the second generation allocator. */
    MVM_gc_gen2_destroy(tc->instance, tc->gen2);

    /* Free the thread-specific storage */
    MVM_frame_free_frame_pool(tc);
    MVM_checked_free_null(tc->gc_work);
    MVM_checked_free_null(tc->temproots);
    MVM_checked_free_null(tc->gen2roots);

    /* Destroy the libuv event loop */
    uv_loop_delete(tc->loop);

    /* Free the thread context itself. */
    memset(tc, 0, sizeof(MVMThreadContext));
    free(tc);
}
