#if defined _WIN32
#define MVM_platform_thread_yield SwitchToThread
#elif defined MVM_HAS_PTHREAD_YIELD
#include <pthread.h>
#define MVM_platform_thread_yield pthread_yield
#else
#include <sched.h>
#define MVM_platform_thread_yield sched_yield
#endif

#if defined _WIN32
#define MVM_platform_thread_exit(status) ExitThread(0)
#define MVM_platform_thread_id() (MVMint64)GetCurrentThreadId()
#else
#define MVM_platform_thread_exit(status) pthread_exit(status)
#define MVM_platform_thread_id() (MVMint64)uv_thread_self()
#endif
