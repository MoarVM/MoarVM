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

/* Loads a compilation unit from a bytecode file handle, mapping it into memory. */
MVMCompUnit * MVM_cu_map_from_file_handle(MVMThreadContext *tc, uv_file fd, MVMuint64 pos) {
    MVMCompUnit *cu          = NULL;
    void        *block       = NULL;
    void        *handle      = NULL;
    MVMuint64    size;
    uv_fs_t req;

    /* Ensure the file exists, and get its size. */
    if (uv_fs_fstat(tc->loop, &req, fd, NULL) < 0) {
        MVM_exception_throw_adhoc(tc, "Trying to stat: %s", uv_strerror(req.result));
    }

    size = req.statbuf.st_size;

    if ((block = MVM_platform_map_file(fd, &handle, (size_t)size, 0)) == NULL) {
        /* FIXME: check errno or GetLastError() */
        MVM_exception_throw_adhoc(tc, "Could not map file into memory: %s", "FIXME");
    }

    block = ((char*)block) + pos;

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

    uv_mutex_lock(cu->body.inline_tweak_mutex);

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

    uv_mutex_unlock(cu->body.inline_tweak_mutex);

    return idx;
}

/* Adds an extra string, needed due to an inlining, and returns its index. */
MVMuint32 MVM_cu_string_add(MVMThreadContext *tc, MVMCompUnit *cu, MVMString *str) {
    MVMuint32 found = 0;
    MVMuint32 idx;

    uv_mutex_lock(cu->body.inline_tweak_mutex);

    /* See if we already know this string; only consider those added already by
     * inline, since we don't intern and don't want this to be costly to hunt. */
    for (idx = cu->body.orig_strings; idx < cu->body.num_strings; idx++)
        if (MVM_cu_string(tc, cu, idx) == str) {
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

    uv_mutex_unlock(cu->body.inline_tweak_mutex);

    return idx;
}

/* Used when we try to read a string from the string heap, but it's not there.
 * Decodes it "on-demand" and stores it in the string heap. */
static MVMuint32 read_uint32(MVMuint8 *src) {
#ifdef MVM_BIGENDIAN
    MVMuint32 value;
    size_t i;
    MVMuint8 *destbytes = (MVMuint8 *)&value;
    for (i = 0; i < 4; i++)
         destbytes[4 - i - 1] = src[i];
    return value;
#else
    return *((MVMuint32 *)src);
#endif
}
static void compute_fast_table_upto(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint32 end_bin) {
    MVMuint32  cur_bin = cu->body.string_heap_fast_table_top;
    MVMuint8  *cur_pos = cu->body.string_heap_start + cu->body.string_heap_fast_table[cur_bin];
    MVMuint8  *limit   = cu->body.string_heap_read_limit;
    while (cur_bin < end_bin) {
        MVMuint32 i;
        for (i = 0; i < MVM_STRING_FAST_TABLE_SPAN; i++) {
            if (cur_pos + 4 < limit) {
                MVMuint32 bytes = read_uint32(cur_pos) >> 1;
                cur_pos += 4 + bytes + (bytes & 3 ? 4 - (bytes & 3) : 0);
            }
            else {
                MVM_exception_throw_adhoc(tc,
                    "Attempt to read past end of string heap when locating string");
            }
        }
        cur_bin++;
        cu->body.string_heap_fast_table[cur_bin] = (MVMuint32)
            (cur_pos - cu->body.string_heap_start);
    }
    MVM_barrier();
    cu->body.string_heap_fast_table_top = end_bin;
}
MVMString * MVM_cu_obtain_string(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint32 idx) {
    MVMuint32  cur_idx;
    MVMuint8  *cur_pos;
    MVMuint8  *limit = cu->body.string_heap_read_limit;

    /* Make sure we've enough entries in the fast table to jump close to where
     * the string will be. */
    MVMuint32 fast_bin = idx / MVM_STRING_FAST_TABLE_SPAN;
    if (fast_bin > cu->body.string_heap_fast_table_top)
        compute_fast_table_upto(tc, cu, fast_bin);

    /* Scan from that position to find the string we need. */
    cur_idx = fast_bin * MVM_STRING_FAST_TABLE_SPAN;
    cur_pos = cu->body.string_heap_start + cu->body.string_heap_fast_table[fast_bin];
    while (cur_idx != idx) {
        if (cur_pos + 4 < limit) {
            MVMuint32 bytes = read_uint32(cur_pos) >> 1;
            cur_pos += 4 + bytes + (bytes & 3 ? 4 - (bytes & 3) : 0);
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Attempt to read past end of string heap when locating string");
        }
        cur_idx++;
    }

    /* Read the string. */
    if (cur_pos + 4 < limit) {
        MVMuint32 ss = read_uint32(cur_pos);
        MVMuint32 bytes = ss >> 1;
        MVMuint32 decode_utf8 = ss & 1;
        cur_pos += 4;
        if (cur_pos + bytes < limit) {
            MVMString *s;
            MVM_gc_allocate_gen2_default_set(tc);
            s = decode_utf8
                ? MVM_string_utf8_decode(tc, tc->instance->VMString, (char *)cur_pos, bytes)
                : MVM_string_latin1_decode(tc, tc->instance->VMString, (char *)cur_pos, bytes);
            MVM_ASSIGN_REF(tc, &(cu->common.header), cu->body.strings[idx], s);
            MVM_gc_allocate_gen2_default_clear(tc);
            return s;
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Attempt to read past end of string heap when reading string");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Attempt to read past end of string heap when reading string length");
    }
}
