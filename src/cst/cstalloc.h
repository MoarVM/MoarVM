#ifndef _MVM_CSTALLOC_H_GUARD
#define _MVM_CSTALLOC_H_GUARD

#include <sys/types.h>

void* cstalloc(size_t);
void  cstfree(void*);

#endif
