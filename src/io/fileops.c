#include "moarvm.h"

MVMString * MVM_slurp_filename(MVMThreadContext *tc, MVMString *filename) {
    MVMString *result;
    apr_status_t rv;
    apr_file_t *fp;
    apr_finfo_t finfo;
    apr_mmap_t *mmap;
    char *fname = MVM_string_utf8_encode_C_string(tc, filename);
    apr_pool_t *mp = (*(tc->interp_cu))->pool;
    
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
    
    apr_file_close(fp);
    
    result = MVM_string_utf8_decode(tc, (MVMObject *)filename, mmap->mm, finfo.size);
    
    return result;
}