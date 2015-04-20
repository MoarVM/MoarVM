#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>
#include <stddef.h>

/* Configuration. */
#include "gen/config.h"

/* Standard integer types. */
#include <platform/inttypes.h>

/* stuff for uthash */
#define uthash_fatal(msg) MVM_exception_throw_adhoc(tc, "internal hash error: " msg)

#include <uthash.h>

/* libuv
 * must precede atomic_ops.h so we get the ordering of Winapi headers right
 */
#include <uv.h>

/* libatomic_ops */
#define AO_REQUIRE_CAS
#include <atomic_ops.h>

/* dynload/dyncall/dyncallback */
#include <dynload.h>
#include <dyncall.h>
#include <dyncall_callback.h>

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

MVM_PUBLIC const MVMint32 MVM_jit_support(void);

/* Headers for various other data structures and APIs. */
#include "6model/6model.h"
#include "gc/wb.h"
#include "core/threadcontext.h"
#include "core/instance.h"
#include "core/interp.h"
#include "core/callsite.h"
#include "core/args.h"
#include "core/exceptions.h"
#include "core/alloc.h"
#include "core/frame.h"
#include "core/validation.h"
#include "core/compunit.h"
#include "core/bytecode.h"
#include "core/bytecodedump.h"
#include "core/ops.h"
#include "core/threads.h"
#include "core/hll.h"
#include "core/loadbytecode.h"
#include "math/num.h"
#include "core/coerce.h"
#include "core/dll.h"
#include "core/ext.h"
#include "core/nativecall.h"
#include "core/continuation.h"
#include "6model/reprs.h"
#include "6model/reprconv.h"
#include "6model/bootstrap.h"
#include "6model/containers.h"
#include "6model/sc.h"
#include "6model/serialization.h"
#include "6model/parametric.h"
#include "gc/allocation.h"
#include "gc/worklist.h"
#include "gc/collect.h"
#include "gc/orchestrate.h"
#include "gc/gen2.h"
#include "gc/roots.h"
#include "gc/objectid.h"
#include "gc/finalize.h"
#include "spesh/dump.h"
#include "spesh/graph.h"
#include "spesh/codegen.h"
#include "spesh/candidate.h"
#include "spesh/manipulate.h"
#include "spesh/args.h"
#include "spesh/facts.h"
#include "spesh/optimize.h"
#include "spesh/deopt.h"
#include "spesh/log.h"
#include "spesh/threshold.h"
#include "spesh/inline.h"
#include "spesh/osr.h"
#include "strings/normalize.h"
#include "strings/decode_stream.h"
#include "strings/ascii.h"
#include "strings/utf8.h"
#include "strings/utf16.h"
#include "strings/nfg.h"
#include "strings/iter.h"
#include "strings/ops.h"
#include "strings/unicode_gen.h"
#include "strings/unicode.h"
#include "strings/latin1.h"
#include "strings/windows1252.h"
#include "io/io.h"
#include "io/eventloop.h"
#include "io/syncfile.h"
#include "io/syncpipe.h"
#include "io/syncstream.h"
#include "io/syncsocket.h"
#include "io/fileops.h"
#include "io/dirops.h"
#include "io/procops.h"
#include "io/timers.h"
#include "io/filewatchers.h"
#include "io/signals.h"
#include "io/asyncsocket.h"
#include "math/bigintops.h"
#include "mast/driver.h"
#include "core/intcache.h"
#include "core/fixedsizealloc.h"
#include "jit/graph.h"
#include "jit/compile.h"
#include "jit/log.h"
#include "profiler/instrument.h"
#include "profiler/log.h"
#include "profiler/profile.h"

MVMObject *MVM_backend_config(MVMThreadContext *tc);

/* Top level VM API functions. */
MVM_PUBLIC MVMInstance * MVM_vm_create_instance(void);
MVM_PUBLIC void MVM_vm_run_file(MVMInstance *instance, const char *filename);
MVM_PUBLIC void MVM_vm_dump_file(MVMInstance *instance, const char *filename);
MVM_PUBLIC MVM_NO_RETURN void MVM_vm_exit(MVMInstance *instance) MVM_NO_RETURN_GCC;
MVM_PUBLIC void MVM_vm_destroy_instance(MVMInstance *instance);
MVM_PUBLIC void MVM_vm_set_clargs(MVMInstance *instance, int argc, char **argv);
MVM_PUBLIC void MVM_vm_set_exec_name(MVMInstance *instance, const char *exec_name);
MVM_PUBLIC void MVM_vm_set_prog_name(MVMInstance *instance, const char *prog_name);
MVM_PUBLIC void MVM_vm_set_lib_path(MVMInstance *instance, int count, const char **lib_path);

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

/* Convenience shortcut for use in gc_free routines. */
#define MVM_checked_free_null(addr) do { \
    if ((addr)) { \
        MVM_free((void *)(addr)); \
        (addr) = NULL; \
    } \
} while (0)

/* Need to use these to assign to or read from any memory locations on
 * which the other atomic operation macros are used... */
#define MVM_store(addr, new) AO_store_full((volatile AO_t *)(addr), (AO_t)(new))
#define MVM_load(addr) AO_load_full((volatile AO_t *)(addr))
