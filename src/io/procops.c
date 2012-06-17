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
        free(varstring);
        free(valuestring);
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to set env variable: ");
    }
    
    free(varstring);
    free(valuestring);
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
        apr_pool_destroy(tmp_pool);
        free(varstring);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get delete variable: ");
    }
    
    apr_pool_destroy(tmp_pool);
    free(varstring);
}

MVMint64 MVM_proc_nametogid(MVMThreadContext *tc, MVMString *name) {
    apr_status_t rv;
    apr_gid_t groupid;
    char *namestring = MVM_string_utf8_encode_C_string(tc, name);
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(namestring);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get gid from group name: ");
    }
    
    if ((rv = apr_gid_get(&groupid, (const char *)namestring, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        free(namestring);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get gid from group name: ");
    }
    
    apr_pool_destroy(tmp_pool);
    free(namestring);
    
    return (MVMint64)groupid;
}

MVMString * MVM_proc_gidtoname(MVMThreadContext *tc, MVMint64 groupid) {
    MVMString *result;
    apr_status_t rv;
    char *namestring;
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to get group name from gid: ");
    }
    
    if ((rv = apr_gid_name_get(&namestring, (apr_gid_t)groupid, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get group name from gid: ");
    }
    
    result = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, namestring, strlen(namestring));
    
    apr_pool_destroy(tmp_pool);
    
    return result;
}

MVMint64 MVM_proc_nametouid(MVMThreadContext *tc, MVMString *name) {
    apr_status_t rv;
    apr_uid_t userid;
    apr_gid_t groupid;
    char *namestring = MVM_string_utf8_encode_C_string(tc, name);
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        free(namestring);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get uid from user name: ");
    }
    
    if ((rv = apr_uid_get(&userid, &groupid, (const char *)namestring, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        free(namestring);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get uid from user name: ");
    }
    
    apr_pool_destroy(tmp_pool);
    free(namestring);
    
    return (MVMint64)userid;
}

MVMString * MVM_proc_uidtoname(MVMThreadContext *tc, MVMint64 userid) {
    MVMString *result;
    apr_status_t rv;
    char *namestring;
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to get user name from uid: ");
    }
    
    if ((rv = apr_uid_name_get(&namestring, (apr_uid_t)userid, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get user name from uid: ");
    }
    
    result = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, namestring, strlen(namestring));
    
    apr_pool_destroy(tmp_pool);
    
    return result;
}

MVMString * MVM_proc_getusername(MVMThreadContext *tc) {
    return MVM_proc_uidtoname(tc, MVM_proc_getuid(tc));
}

MVMint64 MVM_proc_getuid(MVMThreadContext *tc) {
    apr_status_t rv;
    apr_uid_t userid;
    apr_gid_t groupid;
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to get current uid: ");
    }
    
    if ((rv = apr_uid_current(&userid, &groupid, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get current uid: ");
    }
    
    apr_pool_destroy(tmp_pool);
    return (MVMint64)userid;
}

MVMint64 MVM_proc_getgid(MVMThreadContext *tc) {
    apr_status_t rv;
    apr_uid_t userid;
    apr_gid_t groupid;
    apr_pool_t *tmp_pool;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to get current gid: ");
    }
    
    if ((rv = apr_uid_current(&userid, &groupid, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get current gid: ");
    }
    
    apr_pool_destroy(tmp_pool);
    return (MVMint64)groupid;
}

MVMString * MVM_proc_gethomedir(MVMThreadContext *tc) {
    apr_uid_t userid = (apr_uid_t)MVM_proc_getuid(tc);
    MVMString *result;
    apr_status_t rv;
    char *namestring;
    apr_pool_t *tmp_pool;
    char *dirname;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to get user name from uid: ");
    }
    
    if ((rv = apr_uid_name_get(&namestring, (apr_uid_t)userid, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get user name from uid: ");
    }
    
    if ((rv = apr_uid_homepath_get(&dirname, namestring, tmp_pool)) != APR_SUCCESS) {
        apr_pool_destroy(tmp_pool);
        MVM_exception_throw_apr_error(tc, rv, "Failed to get homedir: ");
    }
    
    result = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, dirname, strlen(dirname));
    
    apr_pool_destroy(tmp_pool);
    
    return result;
}

MVMString * MVM_proc_getencoding(MVMThreadContext *tc) {
    MVMString *result;
    apr_status_t rv;
    apr_pool_t *tmp_pool;
    char *encoding;
    
    /* need a temporary pool */
    if ((rv = apr_pool_create(&tmp_pool, POOL(tc))) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to get encoding: ");
    }
    
    encoding = (char *)apr_os_locale_encoding(tmp_pool);
    
    result = MVM_string_utf8_decode(tc, tc->instance->boot_types->BOOTStr, encoding, strlen(encoding));
    
    apr_pool_destroy(tmp_pool);
    
    return result;
}
