#include <windows.h>
#include <io.h>
#include "platform/mmap.h"

void *MVM_platform_alloc_pages(size_t size, int executable)
{
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
        executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
}

int MVM_platform_free_pages(void *pages, size_t size)
{
    (void)size;
    return VirtualFree(pages, 0, MEM_RELEASE);
}

void *MVM_platform_map_file(int fd, void **handle, size_t size, int writable)
{
    HANDLE fh, mapping;
    LARGE_INTEGER li;
    void *block;

    fh = (HANDLE)_get_osfhandle(fd);
    if (fh == INVALID_HANDLE_VALUE)
        return NULL;

    li.QuadPart = size;
    mapping = CreateFileMapping(fh, NULL,
        writable ? PAGE_READWRITE : PAGE_READONLY,
        li.HighPart, li.LowPart, NULL);

    if(mapping == NULL)
        return NULL;

    block = MapViewOfFile(mapping,
        writable ? FILE_MAP_READ | FILE_MAP_WRITE : FILE_MAP_READ,
        0, 0, size);

    if (block == NULL)
    {
        CloseHandle(mapping);
        return NULL;
    }

    if (handle)
        *handle = mapping;

    return block;
}

int MVM_platform_unmap_file(void *block, void *handle, size_t size)
{
    BOOL unmapped = UnmapViewOfFile(block);
    BOOL closed = !handle || CloseHandle(handle);
    (void)size;
    return unmapped && closed;
}
