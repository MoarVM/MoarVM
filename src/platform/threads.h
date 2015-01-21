#if defined _WIN32
#define MVM_platform_thread_yield SwitchToThread
#elif defined(__APPLE__) || defined(__sun) || defined(__NetBSD__) || defined(_POSIX_PRIORITY_SCHEDULING)
#include <sched.h>
#define MVM_platform_thread_yield sched_yield
#else
#define MVM_platform_thread_yield pthread_yield
#endif

#if defined _WIN32
#define MVM_platform_thread_exit(status) ExitThread(0)
#else
#define MVM_platform_thread_exit(status) pthread_exit(status)
#endif
