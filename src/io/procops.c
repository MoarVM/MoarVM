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
