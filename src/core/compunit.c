#include "moar.h"
#include "platform/mmap.h"

#ifdef _WIN32
#include <fcntl.h>
#define O_RDONLY _O_RDONLY
#endif

/* Creates a compilation unit from a byte array. */
MVMCompUnit * MVM_cu_from_bytes(MVMThreadContext *tc, MVMuint8 *bytes, MVMuint32 size) {
    /* Create compilation unit data structure. Allocate it in gen2 always, so
     * it will never move (the JIT relies on this). */
    MVMCompUnit *cu;
    MVM_gc_allocate_gen2_default_set(tc);
    cu = (MVMCompUnit *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTCompUnit);
    cu->body.data_start = bytes;
    cu->body.data_size  = size;
    MVM_gc_allocate_gen2_default_clear(tc);

    /* Process the input. */
    MVMROOT(tc, cu, {
        MVM_bytecode_unpack(tc, cu);
    });

    /* Resolve HLL config. It may contain nursery pointers, so fire write
     * barrier on it. */
    cu->body.hll_config = MVM_hll_get_config_for(tc, cu->body.hll_name);
    MVM_gc_write_barrier_hit(tc, (MVMCollectable *)cu);

    return cu;
}

/* Loads a compilation unit from a bytecode file, mapping it into memory. */
MVMCompUnit * MVM_cu_map_from_file(MVMThreadContext *tc, const char *filename) {
    MVMCompUnit *cu          = NULL;
    void        *block       = NULL;
    void        *handle      = NULL;
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

    if ((block = MVM_platform_map_file(fd, &handle, (size_t)size, 0)) == NULL) {
        /* FIXME: check errno or GetLastError() */
        MVM_exception_throw_adhoc(tc, "Could not map file '%s' into memory: %s", filename, "FIXME");
    }

    if (uv_fs_close(tc->loop, &req, fd, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to close filehandle: %s", uv_strerror(req.result));
    }

    /* Turn it into a compilation unit. */
    cu = MVM_cu_from_bytes(tc, (MVMuint8 *)block, (MVMuint32)size);
    cu->body.handle = handle;
    cu->body.deallocate = MVM_DEALLOCATE_UNMAP;
    return cu;
}

/* Adds an extra callsite, needed due to an inlining, and returns its index. */
MVMuint16 MVM_cu_callsite_add(MVMThreadContext *tc, MVMCompUnit *cu, MVMCallsite *cs) {
    MVMuint16 found = 0;
    MVMuint16 idx;

    MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)cu->body.update_mutex);

    /* See if we already know this callsite. */
    for (idx = 0; idx < cu->body.num_callsites; idx++)
        if (cu->body.callsites[idx] == cs) {
            found = 1;
            break;
        }
    if (!found) {
        /* Not known; let's add it. */
        idx = cu->body.num_callsites;
        cu->body.callsites = MVM_realloc(cu->body.callsites,
            (idx + 1) * sizeof(MVMCallsite *));
        cu->body.callsites[idx] = MVM_callsite_copy(tc, cs);
        cu->body.num_callsites++;
    }

    MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)cu->body.update_mutex);

    return idx;
}

/* Adds an extra string, needed due to an inlining, and returns its index. */
MVMuint32 MVM_cu_string_add(MVMThreadContext *tc, MVMCompUnit *cu, MVMString *str) {
    MVMuint32 found = 0;
    MVMuint32 idx;

    MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)cu->body.update_mutex);

    /* See if we already know this string; only consider those added already by
     * inline, since we don't intern and don't want this to be costly to hunt. */
    for (idx = cu->body.orig_strings; idx < cu->body.num_strings; idx++)
        if (cu->body.strings[idx] == str) {
            found = 1;
            break;
        }
    if (!found) {
        /* Not known; let's add it. */
        idx = cu->body.num_strings;
        cu->body.strings = MVM_realloc(cu->body.strings,
            (idx + 1) * sizeof(MVMString *));
        cu->body.strings[idx] = str;
        cu->body.num_strings++;
    }

    MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)cu->body.update_mutex);

    return idx;
}
