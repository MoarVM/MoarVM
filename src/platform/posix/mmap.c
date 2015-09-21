#include <stddef.h>
#include <sys/mman.h>
#include "moar.h"
#include "platform/mmap.h"
#include <errno.h>

/* MAP_ANONYMOUS is Linux, MAP_ANON is BSD */
#ifndef MVM_MAP_ANON

#if defined(MAP_ANONYMOUS)
#define MVM_MAP_ANON MAP_ANONYMOUS

#elif defined(MAP_ANON)
#define MVM_MAP_ANON MAP_ANON

#else
#error "Anonymous mmap() not supported. You need to define MVM_MAP_ANON manually if it is."
#endif
#endif

static int page_mode_to_prot_mode(int page_mode) {
    switch (page_mode) {
    case MVM_PAGE_READ:
        return PROT_READ;
    case MVM_PAGE_WRITE:
        return PROT_WRITE;
    case (MVM_PAGE_READ|MVM_PAGE_WRITE):
        return PROT_READ|PROT_WRITE;
    case MVM_PAGE_EXEC:
        return PROT_EXEC;
    case (MVM_PAGE_READ|MVM_PAGE_EXEC):
        return PROT_READ|PROT_EXEC;
    case (MVM_PAGE_WRITE|MVM_PAGE_EXEC):
        return PROT_WRITE|PROT_EXEC;
    case (MVM_PAGE_READ|MVM_PAGE_WRITE|MVM_PAGE_EXEC):
        return PROT_READ|PROT_WRITE|PROT_EXEC;
    default:
        return PROT_NONE;
    }
}

void *MVM_platform_alloc_pages(size_t size, int page_mode)
{
    int prot_mode = page_mode_to_prot_mode(page_mode);
    void *block = mmap(NULL, size, prot_mode, MVM_MAP_ANON | MAP_PRIVATE, -1, 0);

    if (block == MAP_FAILED)
        MVM_panic(1, "MVM_platform_alloc_pages failed: %d", errno);

    return block;
}

int MVM_platform_set_page_mode(void * block, size_t size, int page_mode) {
    int prot_mode = page_mode_to_prot_mode(page_mode);
    return mprotect(block, size, prot_mode) == 0;
}

int MVM_platform_free_pages(void *block, size_t size)
{
    return munmap(block, size) == 0;
}

void *MVM_platform_map_file(int fd, void **handle, size_t size, int writable)
{
    void *block = mmap(NULL, size,
        writable ? PROT_READ | PROT_WRITE : PROT_READ,
        writable ? MAP_SHARED : MAP_PRIVATE, fd, 0);

    (void)handle;
    return block != MAP_FAILED ? block : NULL;
}

int MVM_platform_unmap_file(void *block, void *handle, size_t size)
{
    (void)handle;
    return munmap(block, size) == 0;
}
