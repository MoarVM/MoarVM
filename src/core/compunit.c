#include "moarvm.h"

/* Loads a compilation unit from a bytecode file, mapping it into
 * memory. */
MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, char *filename) {
    MVMCompUnit *cu          = NULL;
    apr_pool_t  *pool        = NULL;
    apr_file_t  *file_handle = NULL;
    apr_mmap_t  *mmap_handle = NULL;
    apr_finfo_t  stat_info;

    /* Ensure the file exists, and get its size. */
    if (apr_pool_create(&pool, NULL) != APR_SUCCESS) {
        MVM_panic("Could not allocate APR memory pool");
    }
    if (apr_stat(&stat_info, filename, APR_FINFO_SIZE, pool) != APR_SUCCESS) {
        apr_pool_destroy(pool);
        MVM_exception_throw_adhoc(tc, "File does not exist");
    }

    /* Map the bytecdoe file into memory. */
    if (apr_file_open(&file_handle, filename, APR_READ | APR_BINARY, APR_OS_DEFAULT, pool) != APR_SUCCESS) {
        apr_pool_destroy(pool);
        MVM_exception_throw_adhoc(tc, "Could not open file");
    } 	
    if (apr_mmap_create(&mmap_handle, file_handle, 0, stat_info.size, APR_MMAP_READ, pool) != APR_SUCCESS) {
        apr_pool_destroy(pool);
        MVM_exception_throw_adhoc(tc, "Could not map file into memory");
    }

    /* Create compilation unit data structure. */
    cu = malloc(sizeof(MVMCompUnit));
    memset(cu, 0, sizeof(MVMCompUnit));
    cu->pool       = pool;
    cu->data_start = (char *)mmap_handle->mm;
    cu->data_size  = mmap_handle->size;
    
    /* Process the input. */
    /* XXX */
    
    return cu;
}
