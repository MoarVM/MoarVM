#define MVM_PAGE_READ    1
#define MVM_PAGE_WRITE   2
#define MVM_PAGE_EXEC    4

void *MVM_platform_alloc_pages(size_t size, int mode);
int MVM_platform_set_page_mode(void * block, size_t size, int mode);
int MVM_platform_free_pages(void *block, size_t size);
void *MVM_platform_map_file(int fd, void **handle, size_t size, int writable);
int MVM_platform_unmap_file(void *block, void *handle, size_t size);
