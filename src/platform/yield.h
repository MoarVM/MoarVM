#if defined _WIN32
#define MVM_platform_yield SwitchToThread
#elif defined __APPLE__
#include <sched.h>
#define MVM_platform_yield sched_yield
#else
#define MVM_platform_yield pthread_yield
#endif
