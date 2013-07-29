#include "moarvm.h"

/* MSVC compilers know about environ,
 * see http://msdn.microsoft.com/en-us//library/vstudio/stxk41x1.aspx */
#ifndef WIN32
#  ifdef __APPLE_CC__
#    include <crt_externs.h>
#    define environ (*_NSGetEnviron())
#  else
extern char **environ;
#  endif
#endif

#define POOL(tc) (*(tc->interp_cu))->pool

MVMObject * MVM_proc_getenvhash(MVMThreadContext *tc) {
    static MVMObject *env_hash;

    if (!env_hash) {
        MVMuint32     pos = 0;
        MVMString *needle = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, "=", 1, MVM_encoding_type_ascii);
        char      *env;

        MVM_gc_root_temp_push(tc, (MVMCollectable **)&needle);

        env_hash = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTHash);
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&env_hash);

        while ((env = environ[pos++]) != NULL) {
#ifndef WIN32
            MVMString *str  = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, env, strlen(env), MVM_encoding_type_utf8);
#else
            /* Can't use MVM_encoding_type_utf8 if it's in GBK encoding environment, otherwise it will exit directly. */
            MVMString *str  = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, env, strlen(env), MVM_encoding_type_latin1);
#endif
            MVMuint32 index = MVM_string_index(tc, str, needle, 0);

            MVMString *key, *val;

            MVM_gc_root_temp_push(tc, (MVMCollectable **)&str);

            key  = MVM_string_substring(tc, str, 0, index);
            MVM_gc_root_temp_push(tc, (MVMCollectable **)&key);

            val  = MVM_string_substring(tc, str, index + 1, -1);
            MVM_repr_bind_key_boxed(tc, env_hash, key, (MVMObject *)val);

            MVM_gc_root_temp_pop_n(tc, 2);
        }

        MVM_gc_root_temp_pop_n(tc, 2);
    }
    return env_hash;
}



/* gets environment variable value */
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
    result = MVM_string_utf8_decode(tc, tc->instance->VMString, rv == 2 ? "" : value, rv == 2 ? 0 : strlen(value));

    free(varstring);
    apr_pool_destroy(tmp_pool);

    return result;
}

/* set environment variable */
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

/* delete environment variable */
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

/* translates groupname to groupid */
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

/* translates groupid to groupname */
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

    result = MVM_string_utf8_decode(tc, tc->instance->VMString, namestring, strlen(namestring));

    apr_pool_destroy(tmp_pool);

    return result;
}

/* translates username to userid */
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

/* translates a userid to username */
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

    result = MVM_string_utf8_decode(tc, tc->instance->VMString, namestring, strlen(namestring));

    apr_pool_destroy(tmp_pool);

    return result;
}

/* gets the username of the calling process */
MVMString * MVM_proc_getusername(MVMThreadContext *tc) {
    return MVM_proc_uidtoname(tc, MVM_proc_getuid(tc));
}

/* gets the uid of the calling process */
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

/* gets the gid of the calling process */
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

/* gets the homedir of the current user. Probably should take username as an argument */
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

    result = MVM_string_utf8_decode(tc, tc->instance->VMString, dirname, strlen(dirname));

    apr_pool_destroy(tmp_pool);

    return result;
}

/* gets the current encoding of the system */
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

    result = MVM_string_utf8_decode(tc, tc->instance->VMString, encoding, strlen(encoding));

    apr_pool_destroy(tmp_pool);

    return result;
}

/* generates a random MVMint64, supposedly. */
/* XXX the internet says this may block... */
MVMint64 MVM_proc_rand_i(MVMThreadContext *tc) {
    MVMint64 result;
    apr_generate_random_bytes((unsigned char *)&result, sizeof(MVMint64));
    return result;
}

/* extremely naively generates a number between 0 and 1 */
MVMnum64 MVM_proc_rand_n(MVMThreadContext *tc) {
    MVMuint64 first, second;
    apr_generate_random_bytes((unsigned char *)&first, sizeof(MVMuint64));
    do {
        apr_generate_random_bytes((unsigned char *)&second, sizeof(MVMuint64));
    /* prevent division by zero in the 2**-128 chance both are 0 */
    } while (first == second);
    return first < second ? (MVMnum64)first / second : (MVMnum64)second / first;
}

/* gets the system time since the epoch in microseconds.
 * APR says the unix version returns GMT. */
MVMint64 MVM_proc_time_i(MVMThreadContext *tc) {
    return (MVMint64)apr_time_now();
}

/* gets the system time since the epoch in seconds.
 * APR says the unix version returns GMT. */
MVMnum64 MVM_proc_time_n(MVMThreadContext *tc) {
    return (MVMnum64)apr_time_now() / 1000000.0;
}

MVMObject * MVM_proc_clargs(MVMThreadContext *tc) {
    MVMInstance *instance = tc->instance;
    if (!instance->clargs) {
        MVMObject *clargs = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTStrArray);
        MVMROOT(tc, clargs, {
            MVMint64 count;
            for (count = 0; count < instance->num_clargs; count++) {
                char *raw = instance->raw_clargs[count];
                MVMString *string = MVM_string_utf8_decode(tc,
                    tc->instance->VMString,
                    instance->raw_clargs[count], strlen(instance->raw_clargs[count]));
                MVM_repr_push_s(tc, clargs, string);
            }
        });

        instance->clargs = clargs;

        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&instance->clargs);
    }
    return instance->clargs;
}
