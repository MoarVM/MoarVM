#ifdef MVM_USE_C11_ATOMICS

#include <stdatomic.h>
typedef atomic_uintptr_t AO_t;

/* clang and gcc disagree on rvalue semantics of atomic types
 * clang refuses to implicitly assign the value of an atomic variable to the
 * regular non-atomic type. Hence we need the following for clang.
 * Whereas gcc permits reading them as normal variables.
 *
 * We can also use `atomic_load_explicit` on gcc, which keeps our code simpler.
 * Curiously the affect is different on different architectures. (This might be
 * a gcc bug, or just ambiguity in the C standard).
 * Using `atomic_load_explicit` instead of a simple read has these changes:
 *
 * * sparc64 removes `membar` instructions
 * * arm64 changes from LFAR to LDR (LDAR has load-acquire semantics)
 *
 * but x86_64 and ppc64 are unchanged.
 *
 * suggesting that the former platforms are treating the implicit read as
 * `memory_order_seq_cst` but the latter as `memory_order_relaxed`
 *
 * The latter is what we want, and is cheaper, so be explicit.
 */
#define AO_READ(v) atomic_load_explicit(&(v), memory_order_relaxed)

/* clang also refuses to cast as (AO_t)(v), but doing this works for gcc and
 * clang (and hopefully other compilers, when we get there) */
#define AO_CAST(v) (uintptr_t)(v)

#define MVM_incr(addr) atomic_fetch_add((volatile AO_t *)(addr), 1)
#define MVM_decr(addr) atomic_fetch_sub((volatile AO_t *)(addr), 1)
#define MVM_add(addr, add) atomic_fetch_add((volatile AO_t *)(addr), (add))

/* Returns non-zero for success. Use for both AO_t numbers and pointers. */
MVM_STATIC_INLINE int
MVM_trycas_AO(volatile AO_t *addr, uintptr_t old, const uintptr_t new) {
    return atomic_compare_exchange_strong(addr, &old, new);
}
#define MVM_trycas(addr, old, new) MVM_trycas_AO((volatile AO_t *)(addr), AO_CAST(old), AO_CAST(new))


/* Returns the old value dereferenced at addr.
 * Strictly, as libatomic_ops documents it:
 *      Atomically compare *addr to old_val, and replace *addr by new_val
 *      if the first comparison succeeds; returns the original value of *addr;
 *       cannot fail spuriously.
 */
MVM_STATIC_INLINE uintptr_t
MVM_cas(volatile AO_t *addr, uintptr_t old, const uintptr_t new) {
    /* If *addr == old then { does exchange, returns true }
     * else { writes old value to &old, returns false }
     * Hence if exchange happens, we return the old value because C11 doesn't
     * overwrite &old. If exchange doesn't happen, C11 does overwrite. */
    atomic_compare_exchange_strong(addr, &old, new);
    return old;
}

/* Returns the old pointer value dereferenced at addr. Provided for a tiny bit of type safety. */
#define MVM_casptr(addr, old, new) ((void *)MVM_cas((AO_t *)(addr), (uintptr_t)(old), (uintptr_t)(new)))

/* Full memory barrier. */
#define MVM_barrier() atomic_thread_fence(memory_order_seq_cst)

/* Need to use these to assign to or read from any memory locations on
 * which the other atomic operation macros are used... */
#define MVM_store(addr, new) atomic_store((volatile AO_t *)(addr), AO_CAST(new))
#define MVM_load(addr) atomic_load((volatile AO_t *)(addr))

#else

/* libatomic_ops */

#define AO_REQUIRE_CAS
#include <atomic_ops.h>
#define AO_READ(v) (v)
#define AO_CAST(v) (AO_t)(v)

/* Seems that both 32 and 64 bit sparc need this crutch */
#if defined(__s390__) || defined(__sparc__)
AO_t AO_fetch_compare_and_swap_emulation(volatile AO_t *addr, AO_t old_val, AO_t new_val);
# define AO_fetch_compare_and_swap_full(addr, old, newval) \
    AO_fetch_compare_and_swap_emulation(addr, old, newval)
#endif

/* Returns original. Use only on AO_t-sized values (including pointers). */
#define MVM_incr(addr) AO_fetch_and_add1_full((volatile AO_t *)(addr))
#define MVM_decr(addr) AO_fetch_and_sub1_full((volatile AO_t *)(addr))
#define MVM_add(addr, add) AO_fetch_and_add_full((volatile AO_t *)(addr), (AO_t)(add))

/* Returns non-zero for success. Use for both AO_t numbers and pointers. */
#define MVM_trycas(addr, old, new) AO_compare_and_swap_full((volatile AO_t *)(addr), (AO_t)(old), (AO_t)(new))

/* Returns the old value dereferenced at addr. */
#define MVM_cas(addr, old, new) AO_fetch_compare_and_swap_full((addr), (old), (new))

/* Returns the old pointer value dereferenced at addr. Provided for a tiny bit of type safety. */
#define MVM_casptr(addr, old, new) ((void *)MVM_cas((AO_t *)(addr), (AO_t)(old), (AO_t)(new)))

/* Full memory barrier. */
#define MVM_barrier() AO_nop_full()

/* Need to use these to assign to or read from any memory locations on
 * which the other atomic operation macros are used... */
#define MVM_store(addr, new) AO_store_full((volatile AO_t *)(addr), (AO_t)(new))
#define MVM_load(addr) AO_load_full((volatile AO_t *)(addr))

#endif
