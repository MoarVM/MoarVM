void MVM_gc_enter_from_allocator(MVMThreadContext *tc);
void MVM_gc_enter_from_interrupt(MVMThreadContext *tc);
MVM_PUBLIC void MVM_gc_mark_thread_blocked(MVMThreadContext *tc);
MVM_PUBLIC void MVM_gc_mark_thread_unblocked(MVMThreadContext *tc);
MVM_PUBLIC MVMint32 MVM_gc_is_thread_blocked(MVMThreadContext *tc);
void MVM_gc_global_destruction(MVMThreadContext *tc);

struct MVMWorkThread {
    MVMThreadContext *tc;
    void             *limit;
};

typedef enum {
    MVM_GC_DEBUG_ORCHESTRATE = 1,
    MVM_GC_DEBUG_COLLECT = 2,
/*    MVM_GC_DEBUG_ = 4,
    MVM_GC_DEBUG_ = 8,
    MVM_GC_DEBUG_ = 16,
    MVM_GC_DEBUG_ = 32,
    MVM_GC_DEBUG_ = 64,
    MVM_GC_DEBUG_ = 128,
    MVM_GC_DEBUG_ = 256,
    MVM_GC_DEBUG_ = 512,
    MVM_GC_DEBUG_ = 1024,
    MVM_GC_DEBUG_ = 2048,
    MVM_GC_DEBUG_ = 4096,
    MVM_GC_DEBUG_ = 8192,
    MVM_GC_DEBUG_ = 16384,
    MVM_GC_DEBUG_ = 32768,
    MVM_GC_DEBUG_ = 65536,
    MVM_GC_DEBUG_ = 131072,
    MVM_GC_DEBUG_ = 262144,
    MVM_GC_DEBUG_ = 524288,
    MVM_GC_DEBUG_ = 1048576,
    MVM_GC_DEBUG_ = 2097152,
    MVM_GC_DEBUG_ = 4194304,
    MVM_GC_DEBUG_ = 8388608,
    MVM_GC_DEBUG_ = 16777216,
    MVM_GC_DEBUG_ = 33554432,
    MVM_GC_DEBUG_ = 67108864,
    MVM_GC_DEBUG_ = 134217728*/
} MVMGCDebugLogFlags;

/* OR together the flags you want to require, or redefine
 * MVM_GC_DEBUG_ENABLED(flags) if you want something more
 * complicated. */
#define MVM_GC_DEBUG_LOG_FLAGS \
    0

#define MVM_GC_DEBUG_ENABLED(flags) \
    ((MVM_GC_DEBUG_LOG_FLAGS) & (flags))

#ifdef _MSC_VER
# define GCDEBUG_LOG(tc, flags, msg, ...) \
    if (MVM_GC_DEBUG_ENABLED(flags)) \
        printf((msg), (tc)->thread_id, \
            (int)MVM_load(&(tc)->instance->gc_seq_number), __VA_ARGS__)
#else
# define GCDEBUG_LOG(tc, flags, msg, ...) \
    if (MVM_GC_DEBUG_ENABLED(flags)) \
        printf((msg), (tc)->thread_id, \
            (int)MVM_load(&(tc)->instance->gc_seq_number) , ##__VA_ARGS__)
#endif
