#include "moarvm.h"
#include "platform/time.h"

/* MSVC compilers know about environ,
 * see http://msdn.microsoft.com/en-us//library/vstudio/stxk41x1.aspx */
#ifndef _WIN32
#  ifdef __APPLE_CC__
#    include <crt_externs.h>
#    define environ (*_NSGetEnviron())
#  else
extern char **environ;
#  endif
#endif

#define POOL(tc) (*(tc->interp_cu))->body.pool

#ifdef _WIN32
static wchar_t * ANSIToUnicode(MVMuint16 acp, const char *str)
{
     const int          len = MultiByteToWideChar(acp, 0, str, -1, NULL, 0);
     wchar_t * const result = (wchar_t *)malloc(len * sizeof(wchar_t));

     MultiByteToWideChar(acp, 0, str, -1, (LPWSTR)result, len);

     return result;
}

static char * UnicodeToUTF8(const wchar_t *str)
{
     const int       len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
     char * const result = (char *)malloc(len * sizeof(char));

     WideCharToMultiByte(CP_UTF8, 0, str, -1, result, len, NULL, NULL);

     return result;
}

static char* ANSIToUTF8(MVMuint16 acp, const char* str)
{
    wchar_t * const wstr = ANSIToUnicode(acp, str);
    char  * const result = UnicodeToUTF8(wstr);

    free(wstr);
    return result;
}

#endif

MVMObject * MVM_proc_getenvhash(MVMThreadContext *tc) {
    static MVMObject *env_hash;

    if (!env_hash) {
#ifdef _WIN32
        MVMuint16     acp = GetACP(); /* We should get ACP at runtime. */
#endif
        MVMuint32     pos = 0;
        MVMString *needle = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, "=", 1, MVM_encoding_type_ascii);
        char      *env;

        MVM_gc_root_temp_push(tc, (MVMCollectable **)&needle);

        env_hash = MVM_repr_alloc_init(tc, tc->instance->boot_types->BOOTHash);
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&env_hash);

        while ((env = environ[pos++]) != NULL) {
#ifndef _WIN32
            MVMString    *str = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, env, strlen(env), MVM_encoding_type_utf8);
#else
            char * const _env = ANSIToUTF8(acp, env);
            MVMString    *str = MVM_decode_C_buffer_to_string(tc, tc->instance->VMString, _env, strlen(_env), MVM_encoding_type_utf8);

#endif

            MVMuint32 index = MVM_string_index(tc, str, needle, 0);

            MVMString *key, *val;

#ifdef _WIN32
            free(_env);
#endif
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
    return 42;  /* chosen by fair dice roll
                 * yes, I've got one with that many sides
                 */
}

/* extremely naively generates a number between 0 and 1 */
MVMnum64 MVM_proc_rand_n(MVMThreadContext *tc) {
    return 0.42; /* see above */
}

/* gets the system time since the epoch truncated to integral seconds */
MVMint64 MVM_proc_time_i(MVMThreadContext *tc) {
    return MVM_platform_now() / 1000000000;
}

/* gets the system time since the epoch as floating point seconds */
MVMnum64 MVM_proc_time_n(MVMThreadContext *tc) {
    return (MVMnum64)MVM_platform_now() / 1000000000.0;
}

MVMObject * MVM_proc_clargs(MVMThreadContext *tc) {
    MVMInstance *instance = tc->instance;
    if (!instance->clargs) {
        MVMObject *clargs = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
        MVMROOT(tc, clargs, {
            MVMint64 count;

            MVMString *prog_string = MVM_string_utf8_decode(tc,
                tc->instance->VMString,
                instance->prog_name, strlen(instance->prog_name));
            MVM_repr_push_o(tc, clargs, MVM_repr_box_str(tc,
                tc->instance->boot_types->BOOTStr, prog_string));

            for (count = 0; count < instance->num_clargs; count++) {
                char *raw = instance->raw_clargs[count];
                MVMString *string = MVM_string_utf8_decode(tc,
                    tc->instance->VMString,
                    instance->raw_clargs[count], strlen(instance->raw_clargs[count]));
                MVM_repr_push_o(tc, clargs, MVM_repr_box_str(tc,
                    tc->instance->boot_types->BOOTStr, string));
            }
        });

        instance->clargs = clargs;

        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&instance->clargs);
    }
    return instance->clargs;
}
