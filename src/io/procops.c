#include "moarvm.h"

#define POOL(tc) (*(tc->interp_cu))->pool

MVMString * MVM_proc_getenv(MVMThreadContext *tc, MVMString *var) {
    MVMString *result;
    apr_status_t rv;
    char *varstring = MVM_string_utf8_encode_C_string(tc, var);
    char *value;
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(varstring);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get env variable: ");
    }
    
    if ((rv = apr_env_get(&value, (const char *)varstring, tmp_pool)) != APR_SUCCESS && rv != 2) {
        free(varstring);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get env variable: ");
    }
    
    /* TODO find out the define for the magic value 2 (env var not found) */
    result = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, rv == 2 ? "" : value, rv == 2 ? 0 : strlen(value));
    
    free(varstring);
    apr_pool_destroy(tmp_pool);
    
    return result;
}

void MVM_proc_setenv(MVMThreadContext *tc, MVMString *var, MVMString *value) {
    apr_status_t rv;
    char *varstring = MVM_string_utf8_encode_C_string(tc, var);
    char *valuestring = MVM_string_utf8_encode_C_string(tc, value);
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to set env variable: ");
    }
    
    if ((rv = apr_env_set((const char *)varstring, (const char *)valuestring, tmp_pool)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to set env variable: ");
    }
    
    apr_pool_destroy(tmp_pool);
}

void MVM_proc_delenv(MVMThreadContext *tc, MVMString *var) {
    apr_status_t rv;
    char *varstring = MVM_string_utf8_encode_C_string(tc, var);
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(varstring);
        MVM_exception_throw_apr_error(tc, rv, "Failed to delete env variable: ");
    }
    
    if ((rv = apr_env_delete((const char *)varstring, tmp_pool)) != APR_SUCCESS) {
        free(varstring);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get delete variable: ");
    }
    
    apr_pool_destroy(tmp_pool);
    free(varstring);
}
