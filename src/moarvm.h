#include <stdlib.h>

/* Pull in the APR. */
#define APR_DECLARE_STATIC 1
#include <apr_general.h>
#include <apr_atomic.h>
#include <apr_file_info.h>
#include <apr_file_io.h>
#include <apr_mmap.h>
#include <apr_network_io.h>
#include <apr_strings.h>
#include <apr_portable.h>
#include <apr_env.h>
#include <apr_getopt.h>

/* libatomic_ops */
#include <atomic_ops.h>

/* Configuration. */
#include "gen/config.h"

/* needs to be after config.h */
#include <uthash.h>

/* forward declarations */
#include "types.h"

#ifdef MVM_SHARED
#  if INCLUDED_FROM_FILE_THAT_GOES_INTO_THE_DLL
#    define MVM_PUBLIC MVM_DLL_EXPORT
#  else
#    define MVM_PUBLIC MVM_DLL_IMPORT
#  endif
#  define MVM_PRIVATE MVM_DLL_LOCAL
#else
#  define MVM_PUBLIC
#  define MVM_PRIVATE
#endif

/* Headers for APIs for various other data structures and APIs. */
#include "6model/6model.h"
#include "core/threadcontext.h"
#include "core/instance.h"
#include "core/interp.h"
#include "core/args.h"
#include "core/exceptions.h"
#include "core/frame.h"
#include "core/validation.h"
#include "core/compunit.h"
#include "core/bytecode.h"
#include "core/bytecodedump.h"
#include "core/ops.h"
#include "core/threads.h"
#include "core/hll.h"
#include "core/loadbytecode.h"
#include "core/coerce.h"
#include "6model/reprs.h"
#include "6model/reprconv.h"
#include "6model/bootstrap.h"
#include "6model/containers.h"
#include "6model/sc.h"
#include "6model/serialization.h"
#include "gc/allocation.h"
#include "gc/worklist.h"
#include "gc/collect.h"
#include "gc/orchestrate.h"
#include "gc/gen2.h"
#include "gc/roots.h"
#include "gc/wb.h"
#include "strings/ascii.h"
#include "strings/utf8.h"
#include "strings/ops.h"
#include "strings/unicode_gen.h"
#include "strings/unicode.h"
#include "strings/latin1.h"
#include "io/fileops.h"
#include "io/socketops.h"
#include "io/dirops.h"
#include "io/procops.h"
#include "math/bigintops.h"

/* Top level VM API functions. */
MVMInstance * MVM_vm_create_instance(void);
void MVM_vm_run_file(MVMInstance *instance, const char *filename);
void MVM_vm_dump_file(MVMInstance *instance, const char *filename);
void MVM_vm_destroy_instance(MVMInstance *instance);

/* Returns original. Use only on AO_t-sized values (including pointers). */
#define MVM_atomic_incr(addr) AO_fetch_and_add1_full((volatile AO_t *)(addr))
#define MVM_atomic_decr(addr) AO_fetch_and_sub1_full((volatile AO_t *)(addr))
#define MVM_atomic_add(addr, add) AO_fetch_and_add_full((volatile AO_t *)(addr), (AO_t)(add))

/* Returns non-zero for success. Use for both AO_t numbers and pointers. */
#define MVM_trycas(addr, old, new) AO_compare_and_swap_full((volatile AO_t *)(addr), (AO_t)(old), (AO_t)(new))

/* Full memory barrier. */
#define MVM_barrier() AO_nop_full()

/* Convenience shortcut for use in gc_free routines. */
#define MVM_checked_free_null(addr) do { \
    if (addr) { \
        free(addr); \
        addr = NULL; \
    } \
} while (0)