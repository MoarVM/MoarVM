#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool

void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest) {
    apr_status_t rv;
    const char *a, *b;
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, src);
    b = (const char *) MVM_string_utf8_encode_C_string(tc, dest);
    
    if ((rv = apr_file_copy(a, b, APR_FILE_SOURCE_PERMS, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to copy '%s' to '%s': ", a, b);
    }
}

/* read all of a file into a string */
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename) {
    MVMString *result;
    apr_status_t rv;
    apr_file_t *fp;
    apr_finfo_t finfo;
    apr_mmap_t *mmap;
    char *fname = MVM_string_utf8_encode_C_string(tc, filename);
    apr_pool_t *mp = POOL(tc);
    
    /* TODO detect encoding (ucs4, latin1, utf8 (including ascii/ansi), utf16).
     * Currently assume utf8. */
    
    if ((rv = apr_file_open(&fp, fname, APR_READ, APR_OS_DEFAULT, mp)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to open file '%s': ", fname);
    }
    if ((rv = apr_file_info_get(&finfo, APR_FINFO_SIZE, fp)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to get info about file '%s': ", fname);
    }
    if ((rv = apr_mmap_create(&mmap, fp, 0, finfo.size, APR_MMAP_READ, mp)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Slurp failed to mmap file '%s': ", fname);
    }
    
    /* no longer need the filehandle */
    apr_file_close(fp);
    
    /* convert the mmap to a MVMString */
    result = MVM_string_utf8_decode(tc, (MVMObject *)filename, mmap->mm, finfo.size);
    
    /* delete the mmap */
    apr_mmap_delete(mmap);
    
    return result;
}
