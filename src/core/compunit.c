#include "moarvm.h"

/* Loads a compilation unit from a bytecode file, mapping it into
 * memory. */
MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, char *filename) {
    MVMCompUnit *cu          = NULL;
    apr_pool_t  *pool        = NULL;
    apr_file_t  *file_handle = NULL;
    apr_mmap_t  *mmap_handle = NULL;
    apr_finfo_t  stat_info;
    int          apr_return_status;

    /* Ensure the file exists, and get its size. */
    if ((apr_return_status = apr_pool_create(&pool, NULL)) != APR_SUCCESS) {
        MVM_panic(MVM_exitcode_compunit, "Could not allocate APR memory pool: errorcode %d", apr_return_status);
    }
    if ((apr_return_status = apr_stat(&stat_info, filename, APR_FINFO_SIZE, pool)) != APR_SUCCESS) {
        apr_pool_destroy(pool);
        MVM_exception_throw_apr_error(tc, apr_return_status, "While looking for '%s': ", filename);
    }

    /* Map the bytecdoe file into memory. */
    if ((apr_return_status = apr_file_open(&file_handle, filename,
            APR_READ | APR_BINARY, APR_OS_DEFAULT, pool)) != APR_SUCCESS) {
        apr_pool_destroy(pool);
        MVM_exception_throw_apr_error(tc, apr_return_status, "While trying to open '%s': ", filename);
    }
    if ((apr_return_status = apr_mmap_create(&mmap_handle, file_handle, 0,
            stat_info.size, APR_MMAP_READ, pool)) != APR_SUCCESS) {
        apr_pool_destroy(pool);
        MVM_exception_throw_apr_error(tc, apr_return_status, "Could not map file into memory '%s': ", filename);
    }

    /* close the filehandle. */
    apr_file_close(file_handle);

    /* Create compilation unit data structure. */
    cu = malloc(sizeof(MVMCompUnit));
    memset(cu, 0, sizeof(MVMCompUnit));
    cu->pool       = pool;
    cu->data_start = (MVMuint8 *)mmap_handle->mm;
    cu->data_size  = (MVMuint32)mmap_handle->size;

    /* Process the input. */
    MVM_bytecode_unpack(tc, cu);

    /* Resolve HLL config. */
    cu->hll_config = MVM_hll_get_config_for(tc, cu->hll_name);

    /* Add the compilation unit to the head of the unit linked lists. */
    do {
        cu->next_compunit = tc->instance->head_compunit;
    } while (!MVM_cas(&tc->instance->head_compunit, cu->next_compunit, cu));

    return cu;
}
