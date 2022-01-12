/* Configuration. */
#include "gen/config.h"

#if MVM_HAS_PTHREAD_SETNAME_NP
/* pthread_setname_np only exists if we set _GNU_SOURCE extremely early.
 * We will need to be vgilant to not accidentally use gnu extensions in
 * other places without checking properly. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Standard integer types. */
#include <platform/inttypes.h>

/* platform-specific setjmp override */
#include <platform/setjmp.h>

#ifdef MVM_USE_MIMALLOC
/* mimalloc needs to come early so other libs use it */
#include <mimalloc.h>

#define MVM_strdup mi_strdup
#define MVM_strndup mi_strndup
#else
#define MVM_strdup strdup
#define MVM_strndup strndup
#endif

/* libuv
 * must precede atomic_ops.h so we get the ordering of Winapi headers right
 */
#include <uv.h>

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
#else
/* libatomic_ops */
#define AO_REQUIRE_CAS
#include <atomic_ops.h>
#define AO_READ(v) (v)
#define AO_CAST(v) (AO_t)(v)
#endif

/* libffi or dynload/dyncall/dyncallback */
#ifdef HAVE_LIBFFI
#include <ffi.h>
#else
#include <dynload.h>
#include <dyncall.h>
#include <dyncall_callback.h>
#endif

/* needed in threadcontext.h */
#include <platform/threads.h>

/* forward declarations */
#include "types.h"

/* Sized types. */
typedef int8_t   MVMint8;
typedef uint8_t  MVMuint8;
typedef int16_t  MVMint16;
typedef uint16_t MVMuint16;
typedef int32_t  MVMint32;
typedef uint32_t MVMuint32;
typedef int64_t  MVMint64;
typedef uint64_t MVMuint64;
typedef float    MVMnum32;
typedef double   MVMnum64;

/* Alignment. */
#if HAVE_ALIGNOF
/* A GCC extension. */
#define ALIGNOF(t) __alignof__(t)
#elif defined _MSC_VER
/* MSVC extension. */
#define ALIGNOF(t) __alignof(t)
#else
/* Alignment by measuring structure padding. */
#define ALIGNOF(t) ((char *)(&((struct { char c; t _h; } *)0)->_h) - (char *)0)
#endif

#define MVM_ASSERT_ALIGNED(var, align) assert(!((MVMuint64)(var) % (MVMuint64)(align)))

#define MVM_ALIGN_SECTION_MASK ((MVMuint32)ALIGNOF(MVMint64) - 1)
#define MVM_ALIGN_SECTION(offset) (((offset) + (MVM_ALIGN_SECTION_MASK)) & ~(MVM_ALIGN_SECTION_MASK))

#if defined MVM_BUILD_SHARED
#  define MVM_PUBLIC  MVM_DLL_EXPORT
#  define MVM_PRIVATE MVM_DLL_LOCAL
#elif defined MVM_SHARED
#  define MVM_PUBLIC  MVM_DLL_IMPORT
#  define MVM_PRIVATE MVM_DLL_LOCAL
#else
#  define MVM_PUBLIC
#  define MVM_PRIVATE
#endif

#if MVM_PTR_SIZE < 8
#  define MVM_USE_OVERFLOW_SERIALIZATION_INDEX
#endif

#if defined _MSC_VER
#  define MVM_USED_BY_JIT __pragma(optimize( "g", off ))
#else
#  define MVM_USED_BY_JIT
#endif

/* Returns non-zero for success. Use for both AO_t numbers and pointers. */
#ifdef MVM_USE_C11_ATOMICS
MVM_STATIC_INLINE int
MVM_trycas_AO(volatile AO_t *addr, uintptr_t old, const uintptr_t new) {
    return atomic_compare_exchange_strong(addr, &old, new);
}
#define MVM_trycas(addr, old, new) MVM_trycas_AO((volatile AO_t *)(addr), AO_CAST(old), AO_CAST(new))
#else
#define MVM_trycas(addr, old, new) AO_compare_and_swap_full((volatile AO_t *)(addr), (AO_t)(old), (AO_t)(new))
#endif

/* Hashes */
#define HASH_DEBUG_ITER 0
#define MVM_HASH_RANDOMIZE 1
#define MVM_HASH_MAX_PROBE_DISTANCE 255
#define MVM_HASH_INITIAL_BITS_IN_METADATA 5

typedef MVMuint32 MVMHashNumItems;
typedef MVMuint64 MVMHashv;

MVM_PUBLIC MVMint32 MVM_jit_support(void);

/* Headers for various other data structures and APIs. */
#include "6model/6model.h"
#include "gc/collect.h"
#include "gc/debug.h"
#include "gc/wb.h"
#include "core/vector.h"
#include "core/exceptions.h"
#include "core/str_hash_table.h"
#include "core/fixkey_hash_table.h"
#include "core/index_hash_table.h"
#include "core/ptr_hash_table.h"
#include "core/uni_hash_table.h"
#include "core/threadcontext.h"
#include "disp/registry.h"
#include "disp/boot.h"
#include "disp/inline_cache.h"
#include "core/instance.h"
#include "core/interp.h"
#include "core/callsite.h"
#include "core/alloc.h"
#include "core/args.h"
#include "disp/program.h"
#include "disp/syscall.h"
#include "disp/resume.h"
#include "core/frame.h"
#include "core/callstack.h"
#include "core/validation.h"
#include "core/ops.h"
#include "core/ext.h"
#include "core/bytecode.h"
#include "core/bytecodedump.h"
#include "core/threads.h"
#include "core/hll.h"
#include "core/loadbytecode.h"
#include "core/bitmap.h"
#include "math/num.h"
#include "core/coerce.h"
#ifdef HAVE_LIBFFI
#include "core/nativecall_libffi.h"
#else
#include "core/nativecall_dyncall.h"
#endif
#include "core/nativecall.h"
#include "core/dll.h"
#include "core/continuation.h"
#include "debug/debugserver.h"
#include "spesh/pea.h"
#include "6model/reprs.h"
#include "6model/reprconv.h"
#include "6model/bootstrap.h"
#include "6model/sc.h"
#include "6model/serialization.h"
#include "6model/parametric.h"
#include "core/compunit.h"
#include "gc/gen2.h"
#include "gc/allocation.h"
#include "gc/worklist.h"
#include "gc/orchestrate.h"
#include "gc/roots.h"
#include "gc/objectid.h"
#include "gc/finalize.h"
#include "core/regionalloc.h"
#include "spesh/dump.h"
#include "spesh/debug.h"
#include "spesh/disp.h"
#include "spesh/graph.h"
#include "spesh/codegen.h"
#include "spesh/manipulate.h"
#include "spesh/args.h"
#include "spesh/usages.h"
#include "spesh/facts.h"
#include "spesh/optimize.h"
#include "spesh/dead_bb_elimination.h"
#include "spesh/dead_ins_elimination.h"
#include "spesh/deopt.h"
#include "spesh/log.h"
#include "spesh/threshold.h"
#include "spesh/inline.h"
#include "spesh/osr.h"
#include "spesh/iterator.h"
#include "spesh/lookup.h"
#include "spesh/worker.h"
#include "spesh/stats.h"
#include "spesh/plan.h"
#include "spesh/arg_guard.h"
#include "spesh/frame_walker.h"
#include "strings/nfg.h"
#include "strings/normalize.h"
#include "strings/decode_stream.h"
#include "strings/ascii.h"
#include "strings/parse_num.h"
#include "strings/utf8.h"
#include "strings/utf8_c8.h"
#include "strings/utf16.h"
#include "strings/iter.h"
#include "strings/ops.h"
#include "io/procops.h"
#include "core/str_hash_table_funcs.h"
#include "core/fixkey_hash_table_funcs.h"
#include "core/index_hash_table_funcs.h"
#include "core/ptr_hash_table_funcs.h"
#include "core/uni_hash_table_funcs.h"
#include "6model/containers.h"
#include "strings/unicode_gen.h"
#include "strings/unicode.h"
#include "strings/latin1.h"
#include "strings/windows1252.h"
#include "strings/shiftjis.h"
#include "strings/unicode_ops.h"
#include "strings/gb2312.h"
#include "strings/gb18030.h"
#include "strings/siphash/csiphash.h"
#include "io/io.h"
#include "io/eventloop.h"
#include "io/syncfile.h"
#include "io/syncsocket.h"
#include "io/fileops.h"
#include "io/dirops.h"
#include "io/timers.h"
#include "io/filewatchers.h"
#include "io/signals.h"
#include "io/asyncsocket.h"
#include "io/asyncsocketudp.h"
#include "math/bigintops.h"
#include "core/intcache.h"
#include "jit/graph.h"
#include "jit/label.h"
#include "jit/expr.h"
#include "jit/register.h"
#include "jit/tile.h"
#include "jit/compile.h"
#include "jit/dump.h"
#include "jit/interface.h"
#include "profiler/instrument.h"
#include "profiler/log.h"
#include "profiler/profile.h"
#include "profiler/heapsnapshot.h"
#include "profiler/telemeh.h"
#include "profiler/configuration.h"
#include "instrument/crossthreadwrite.h"
#include "instrument/line_coverage.h"

MVMObject *MVM_backend_config(MVMThreadContext *tc);

/* Top level VM API functions. */
MVM_PUBLIC MVMInstance * MVM_vm_create_instance(void);
MVM_PUBLIC void MVM_vm_run_file(MVMInstance *instance, const char *filename);
MVM_PUBLIC void MVM_vm_run_bytecode(MVMInstance *instance, MVMuint8 *bytes, MVMuint32 size);
MVM_PUBLIC void MVM_vm_dump_file(MVMInstance *instance, const char *filename);
MVM_PUBLIC MVM_NO_RETURN void MVM_vm_exit(MVMInstance *instance) MVM_NO_RETURN_ATTRIBUTE;
MVM_PUBLIC void MVM_vm_destroy_instance(MVMInstance *instance);
MVM_PUBLIC void MVM_vm_set_clargs(MVMInstance *instance, int argc, char **argv);
MVM_PUBLIC void MVM_vm_set_exec_name(MVMInstance *instance, const char *exec_name);
MVM_PUBLIC void MVM_vm_set_prog_name(MVMInstance *instance, const char *prog_name);
MVM_PUBLIC void MVM_vm_set_lib_path(MVMInstance *instance, int count, const char **lib_path);

MVM_PUBLIC void MVM_vm_event_subscription_configure(MVMThreadContext *tc, MVMObject *queue, MVMObject *config);

/* Returns absolute executable path. */
MVM_PUBLIC int MVM_exepath(char* buffer, size_t* size);

#ifdef _WIN32
/* Reopens STDIN, STDOUT, STDERR to the 'NUL' device. */
MVM_PUBLIC int MVM_set_std_handles_to_nul(void);
#endif

#ifdef MVM_USE_C11_ATOMICS

#define MVM_incr(addr) atomic_fetch_add((volatile AO_t *)(addr), 1)
#define MVM_decr(addr) atomic_fetch_sub((volatile AO_t *)(addr), 1)
#define MVM_add(addr, add) atomic_fetch_add((volatile AO_t *)(addr), (add))

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
