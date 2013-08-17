#include <sys/mman.h>
#include "platform/mmap.h"

void *MVM_platform_alloc_pages(size_t size, int executable)
{
    void *block = mmap(NULL, size,
        executable ? PROT_READ | PROT_WRITE | PROT_EXEC : PROT_READ | PROT_WRITE,
        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    return block != MAP_FAILED ? block : NULL;
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
