/* Configuration. */
#include "gen/config.h"

#if MVM_HAS_PTHREAD_SETNAME_NP
/* pthread_setname_np only exists if we set _GNU_SOURCE extremely early.
 * We will need to be vgilant to not accidentally use gnu extensions in
 * other places without checking properly. */
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>
#include <stddef.h>

/* Standard integer types. */
#include <platform/inttypes.h>

/* platform-specific setjmp override */
#include <platform/setjmp.h>

/* libuv
 * must precede atomic_ops.h so we get the ordering of Winapi headers right
 */
#include <uv.h>

/* libatomic_ops */
#define AO_REQUIRE_CAS
#include <atomic_ops.h>

/* libffi or dynload/dyncall/dyncallback */
#ifdef HAVE_LIBFFI
#include <ffi.h>
#else
#include <dynload.h>
#include <dyncall.h>
#include <dyncall_callback.h>
#endif

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

/* stuff for uthash */
#define uthash_fatal(msg) MVM_exception_throw_adhoc(tc, "internal hash error: " msg)

#include "strings/uthash_types.h"

MVM_PUBLIC MVMint32 MVM_jit_support(void);

/* Headers for various other data structures and APIs. */
#include "6model/6model.h"
#include "gc/collect.h"
#include "gc/debug.h"
#include "gc/wb.h"
#include "core/vector.h"
#include "core/threadcontext.h"
#include "core/instance.h"
#include "strings/uthash.h"
#include "core/interp.h"
#include "core/callsite.h"
#include "core/args.h"
#include "core/exceptions.h"
#include "core/alloc.h"
#include "core/frame.h"
#include "core/callstack.h"
#include "core/validation.h"
#include "core/bytecode.h"
#include "core/bytecodedump.h"
#include "core/ops.h"
#include "core/threads.h"
#include "core/hll.h"
#include "core/loadbytecode.h"
#include "core/bitmap.h"
#include "math/num.h"
#include "core/coerce.h"
#include "core/ext.h"
#ifdef HAVE_LIBFFI
#include "core/nativecall_libffi.h"
#else
#include "core/nativecall_dyncall.h"
#endif
#include "core/nativecall.h"
#include "core/dll.h"
#include "core/continuation.h"
#include "debug/debugserver.h"
#include "6model/reprs.h"
#include "6model/reprconv.h"
#include "6model/bootstrap.h"
#include "6model/containers.h"
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
#include "spesh/pea.h"
#include "spesh/graph.h"
#include "spesh/codegen.h"
#include "spesh/candidate.h"
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
#include "spesh/plugin.h"
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
#include "strings/unicode_gen.h"
#include "strings/unicode.h"
#include "strings/latin1.h"
#include "strings/windows1252.h"
#include "strings/shiftjis.h"
#include "strings/unicode_ops.h"
#include "strings/gb2312.h"
#include "strings/gb18030.h"
#include "io/io.h"
#include "io/eventloop.h"
#include "io/syncfile.h"
#include "io/syncsocket.h"
#include "io/fileops.h"
#include "io/dirops.h"
#include "io/procops.h"
#include "io/timers.h"
#include "io/filewatchers.h"
#include "io/signals.h"
#include "io/asyncsocket.h"
#include "io/asyncsocketudp.h"
#include "math/bigintops.h"
#include "core/intcache.h"
#include "core/fixedsizealloc.h"
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

#if defined(__s390__)
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
