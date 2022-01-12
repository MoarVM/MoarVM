#ifdef MVM_VALGRIND_SUPPORT
#  include <valgrind/memcheck.h>

#define VALGRIND_CREATE_MEMPOOL(pool, rzB, is_zeroed) do { } while (0)
#define VALGRIND_CREATE_MEMPOOL_EXT(pool, rzB, is_zeroed, flags) do { } while (0)
#define VALGRIND_DESTROY_MEMPOOL(pool)  do { } while (0)
#define VALGRIND_MEMPOOL_ALLOC(pool, addr, size) do { } while (0)
#define VALGRIND_MEMPOOL_FREE(pool, addr) do { } while (0)
#define VALGRIND_MOVE_MEMPOOL(poolA, poolB) do { } while (0)
#define VALGRIND_MAKE_MEM_DEFINED(addr, size) do { } while (0)
#endif
