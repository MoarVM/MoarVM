void MVM_spesh_debug_printf(MVMThreadContext *tc, const char *format, ...);
void MVM_spesh_debug_flush(MVMThreadContext *tc);

MVM_STATIC_INLINE MVMint32 MVM_spesh_debug_enabled(MVMThreadContext *tc) {
    return tc->instance->spesh_log_fh != NULL &&
        (tc->instance->spesh_limit == 0 ||
         tc->instance->spesh_produced == tc->instance->spesh_limit);
}
