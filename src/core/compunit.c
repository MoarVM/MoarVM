#include "moarvm.h"
#include "platform/mmap.h"

#ifdef _WIN32
#include <fcntl.h>
#define O_RDONLY _O_RDONLY
#endif

/* Creates a compilation unit from a byte array. */
MVMCompUnit * MVM_cu_from_bytes(MVMThreadContext *tc, MVMuint8 *bytes, MVMuint32 size) {
    /* Create compilation unit data structure. */
    MVMCompUnit *cu = (MVMCompUnit *)MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTCompUnit);
    cu->body.data_start = bytes;
    cu->body.data_size  = size;

    /* Process the input. */
    MVMROOT(tc, cu, {
        MVM_bytecode_unpack(tc, cu);
    });

    /* Resolve HLL config. */
    cu->body.hll_config = MVM_hll_get_config_for(tc, cu->body.hll_name);

    return cu;
}

/* Loads a compilation unit from a bytecode file, mapping it into memory. */
MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, const char *filename) {
    MVMCompUnit *cu          = NULL;
    void        *block       = NULL;
    uv_file      fd;
    MVMuint64    size;
    uv_fs_t req;

    /* Ensure the file exists, and get its size. */
    if (uv_fs_stat(tc->loop, &req, filename, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "While looking for '%s': %s", filename, uv_strerror(req.result));
    }

    size = req.statbuf.st_size;

    /* Map the bytecode file into memory. */
    if ((fd = uv_fs_open(tc->loop, &req, filename, O_RDONLY, 0, NULL)) < 0) {
        MVM_exception_throw_adhoc(tc, "While trying to open '%s': %s", filename, uv_strerror(req.result));
    }

    /* leaks the mapping file handle on win32 */
    if ((block = MVM_platform_map_file(fd, NULL, (size_t)size, 0)) == NULL) {
        /* FIXME: check errno or GetLastError() */
        MVM_exception_throw_adhoc(tc, "Could not map file '%s' into memory: %s", filename, "FIXME");
    }

    if (uv_fs_close(tc->loop, &req, fd, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to close filehandle: %s", uv_strerror(req.result));
    }

    /* Turn it into a compilation unit. */
    return MVM_cu_from_bytes(tc, (MVMuint8 *)block, (MVMuint32)size);
}
