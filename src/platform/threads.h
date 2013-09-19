#if defined _WIN32
#define MVM_platform_thread_yield SwitchToThread
#elif defined __APPLE__
#include <sched.h>
#define MVM_platform_thread_yield sched_yield
#else
#define MVM_platform_thread_yield pthread_yield
#endif

#if defined _WIN32
#define MVM_platform_thread_exit(status) ExitThread(0)
#endif