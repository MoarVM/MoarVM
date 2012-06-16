#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool

static void verify_dirhandle_type(MVMThreadContext *tc, MVMObject *oshandle, MVMOSHandle **handle, const char *msg) {

    /* work on only MVMOSHandle of type MVM_OSHANDLE_DIR */
    if (REPR(oshandle)->ID != MVM_REPR_ID_MVMOSHandle) {
        MVM_exception_throw_adhoc(tc, "%s requires an object with REPR MVMOSHandle");
    }
    *handle = (MVMOSHandle *)oshandle;
    if ((*handle)->body.handle_type != MVM_OSHANDLE_DIR) {
        MVM_exception_throw_adhoc(tc, "%s requires an MVMOSHandle of type dir handle");
    }
}

/* create a directory recursively */
void MVM_dir_mkdir(MVMThreadContext *tc, MVMString *f) {
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    const char *a;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to mkdir: ");
    }
    
    a = (const char *) MVM_string_utf8_encode_C_string(tc, f);
    
    if ((rv = apr_dir_make_recursive(a, APR_FPROT_OS_DEFAULT, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to mkdir: ");
    }
    apr_pool_destroy(tmp_pool);
}
