#include <windows.h>
#include <io.h>
#include "platform/mmap.h"

static int page_mode_to_prot_mode(int page_mode) {
    switch (page_mode) {
    case MVM_PAGE_READ:
	return PAGE_READONLY;
    case MVM_PAGE_WRITE:
    case (MVM_PAGE_WRITE | MVM_PAGE_READ):
	return PAGE_READWRITE;
    case MVM_PAGE_EXEC:
	return PAGE_EXECUTE;
    case (MVM_PAGE_READ|MVM_PAGE_EXEC):
	return PAGE_EXECUTE_READ;
    case (MVM_PAGE_WRITE|MVM_PAGE_EXEC):
    case (MVM_PAGE_READ|MVM_PAGE_WRITE|MVM_PAGE_EXEC):
	return PAGE_EXECUTE_READWRITE;
    }
    /* I pity the fools that enter an invalid mode */
    return PAGE_NOACCESS;
}

void *MVM_platform_alloc_pages(size_t size, int page_mode)
{
    int prot_mode = page_mode_to_prot_mode(page_mode);
    void * allocd = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, prot_mode);
    if (!allocd)
        MVM_panic(1, "MVM_platform_alloc_pages failed: %d", GetLastError());
    return allocd;
}

int MVM_platform_set_page_mode(void * pages, size_t size, int page_mode) {
    int prot_mode = page_mode_to_prot_mode(page_mode);
    DWORD oldMode;
    return VirtualProtect(pages, size, prot_mode, &oldMode);
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
