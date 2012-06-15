#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool

/* cache the oshandle repr */
static MVMREPROps *oshandle_repr;
static MVMREPROps * get_oshandle_repr(MVMThreadContext *tc) {
    if (!oshandle_repr)
        oshandle_repr = MVM_repr_get_by_name(tc, MVM_string_ascii_decode_nt(tc,
                tc->instance->boot_types->BOOTStr, "MVMOSHandle"));
    return oshandle_repr;
}

char * MVM_file_get_full_path(MVMThreadContext *tc, apr_pool_t *tmp_pool, char *path) {
    apr_status_t rv;
    char *rootpath, *cwd;
    
    /* determine whether the given path is absolute */
    rv = apr_filepath_root((const char **)&rootpath, (const char **)&path, 0, tmp_pool);
    
    if (rv != APR_SUCCESS) {
        /* path is relative so needs cwd prepended */
        rv = apr_filepath_get(&cwd, 0, tmp_pool);
        return (char *)apr_pstrcat(tmp_pool, cwd, "/", path, NULL);
    }
    /* the path is already absolute */
    return (char *)apr_pstrcat(tmp_pool, path, NULL);
}

/* copy a file from one to another. */
void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    apr_status_t rv;
    char *a, *b, *afull, *bfull;
    MVMuint32 len;
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to copy file: ");
    }
    
    afull = MVM_file_get_full_path(tc, tmp_pool, a = MVM_string_utf8_encode_C_string(tc, src));
    bfull = MVM_file_get_full_path(tc, tmp_pool, b = MVM_string_utf8_encode_C_string(tc, dest));
    free(a); free(b);
    
    if ((rv = apr_file_copy((const char *)afull, (const char *)bfull,
            0, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to copy file: ");
    }
    apr_pool_destroy(tmp_pool);
}

void MVM_file_delete(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    const char *a;
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to delete file: ");
    }
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, f);
    
    /* 720002 means file wasn't there on windows, 2 on linux...  */
    /* TODO find defines for these and make it os-specific */
    if ((rv = apr_file_remove(a, tmp_pool)) != APR_SUCCESS && rv != 720002 && rv != 2) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to delete file: ");
    }
    apr_pool_destroy(tmp_pool);
}

MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    char *a;
    apr_finfo_t  stat_info;
    apr_pool_t *tmp_pool;
    MVMint64 result;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to exists file: ");
    }
    
    a = MVM_string_utf8_encode_C_string(tc, f);
    
    result = ((rv = apr_stat(&stat_info, (const char *)a, APR_FINFO_SIZE, tmp_pool)) == APR_SUCCESS)
        ? 1 : 0;
    free(a);
    apr_pool_destroy(tmp_pool);
    return result;
}

/* read all of a file into a string */
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename) {
    MVMString *result;
    apr_status_t rv;
    apr_file_t *fp;
    apr_finfo_t finfo;
    apr_mmap_t *mmap;
    char *fname = MVM_string_utf8_encode_C_string(tc, filename);
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to create pool: ");
    }
    
    /* TODO detect encoding (ucs4, latin1, utf8 (including ascii/ansi), utf16).
     * Currently assume utf8. */
    
    if ((rv = apr_file_open(&fp, fname, APR_READ, APR_OS_DEFAULT, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to open file: ");
    }
    if ((rv = apr_file_info_get(&finfo, APR_FINFO_SIZE, fp)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to get info about file: ");
    }
    if ((rv = apr_mmap_create(&mmap, fp, 0, finfo.size, APR_MMAP_READ, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to mmap file: ");
    }
    
    /* no longer need the filehandle */
    apr_file_close(fp);
    
    /* convert the mmap to a MVMString */
    result = MVM_string_utf8_decode(tc, (MVMObject *)filename, mmap->mm, finfo.size);
    
    /* delete the mmap */
    apr_mmap_delete(mmap);
    apr_pool_destroy(tmp_pool);
    
    return result;
}

/* writes a string to a filehandle.  XXX writes only utf8 for now. */
void MVM_file_write_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMString *str, MVMint64 start, MVMint64 length) {
    apr_status_t rv;
    MVMuint8 *output;
    MVMuint64 output_size;
    apr_size_t bytes_written;
    MVMOSHandle *handle;
    
    /* work on only MVMOSHandle of type MVM_OSHANDLE_FILE */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "write to filehandle requires an object with REPR MVMOSHandle");
    }
    handle = (MVMOSHandle *)oshandle;
    if (handle->body.handle_type != MVM_OSHANDLE_FILE) {
        MVM_exception_throw_adhoc(tc, "write to filehandle requires an MVMOSHandle of type file handle");
    }
    
    if (length < 0)
        length = str->body.graphs - start;
    else if (start + length > str->body.graphs)
        MVM_exception_throw_adhoc(tc, "write to filehandle start + length past end of string");
    
    output = MVM_string_utf8_encode_substr(tc, str, &output_size, start, length);
    bytes_written = (apr_size_t) output_size;
    if ((rv = apr_file_write(handle->body.file_handle, (const void *)output, &bytes_written)) != APR_SUCCESS) {
        free(output);
        MVM_exception_throw_apr_error(tc, rv, "Failed to write bytes to filehandle: tried to write %u bytes, wrote %u bytes: ", output_size, bytes_written);
    }
    free(output);
}

/* return an OSHandle representing stdout */
MVMObject * MVM_file_get_stdout(MVMThreadContext *tc) {
    MVMOSHandle *result = (MVMOSHandle *)get_oshandle_repr(tc)->allocate(tc,
        MVM_gc_allocate_stable(tc, get_oshandle_repr(tc), NULL));
    apr_file_t  *handle;
    apr_status_t rv;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&result->body.mem_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "get_stdout failed to create pool: ");
    }
    
    apr_file_open_stdout(&handle, result->body.mem_pool);
    result->body.file_handle = handle;
    result->body.handle_type = MVM_OSHANDLE_FILE;
    
    return (MVMObject *)result;
}