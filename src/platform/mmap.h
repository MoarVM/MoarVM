void *MVM_platform_alloc_pages(size_t size, int executable);
int MVM_platform_free_pages(void *block, size_t size);
void *MVM_platform_map_file(int fd, void **handle, size_t size, int writable);
int MVM_platform_unmap_file(void *block, void *handle, size_t size);
