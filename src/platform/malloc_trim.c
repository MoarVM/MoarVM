#ifdef __linux__
#include <malloc.h>
int MVM_malloc_trim(void) {
    /* 128*1024 is the glibc default. */
    return malloc_trim(128*1024);
}
#else
int MVM_malloc_trim(void) {
    return 1;
}
#endif
