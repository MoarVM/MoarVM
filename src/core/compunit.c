#include "moarvm.h"
#include "platform/mmap.h"

/* Loads a compilation unit from a bytecode file, mapping it into
 * memory. */
MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, char *filename) {
    MVMCompUnit *cu          = NULL;
    void        *block       = NULL;
    apr_pool_t  *pool        = NULL;
    uv_file      fd;
    MVMuint64    size;
    int          apr_return_status;
    uv_fs_t req;


    /* Create compunit's APR pool */
    if ((apr_return_status = apr_pool_create(&pool, NULL)) != APR_SUCCESS) {
        MVM_panic(MVM_exitcode_compunit, "Could not allocate APR memory pool: errorcode %d", apr_return_status);
    }

    /* Ensure the file exists, and get its size. */
    if ((uv_fs_stat(tc->loop, &req, filename, NULL)) < 0) {
        apr_pool_destroy(pool);
        MVM_exception_throw_adhoc(tc, "While looking for '%s': %s", filename, uv_strerror(req.result));
    }

    size = req.statbuf.st_size;

    /* Map the bytecode file into memory. */
    if ((fd = uv_fs_open(tc->loop, &req, filename, O_RDONLY, 0, NULL)) < 0) {
        apr_pool_destroy(pool);
        MVM_exception_throw_adhoc(tc, "While trying to open '%s': %s", filename, uv_strerror(req.result));
    }

    /* leaks the mapping file handle on win32 */
    if ((block = MVM_platform_map_file(fd, NULL, (size_t)size, 0)) == NULL) {
        apr_pool_destroy(pool);
        /* FIXME: check errno or GetLastError() */
        MVM_exception_throw_adhoc(tc, "Could not map file '%s' into memory: %s", filename, "FIXME");
    }

    if (uv_fs_close(tc->loop, &req, fd, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to close filehandle: %s", uv_strerror(req.result));
    }

    /* Create compilation unit data structure. */
    cu = (MVMCompUnit *)MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTCompUnit);
    cu->body.pool       = pool;
    cu->body.data_start = (MVMuint8 *)block;
    cu->body.data_size  = (MVMuint32)size;

    /* Process the input. */
    MVMROOT(tc, cu, {
        MVM_bytecode_unpack(tc, cu);
    });

    /* Resolve HLL config. */
    cu->body.hll_config = MVM_hll_get_config_for(tc, cu->body.hll_name);

    return cu;
}
