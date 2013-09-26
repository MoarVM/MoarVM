#include "moarvm.h"
#include "platform/time.h"
#include "tinymt64.h"

#include <math.h>

/* concatenating with "" ensures that only literal strings are accepted as argument. */
#define STR_WITH_LEN(str)  ("" str ""), (sizeof(str) - 1)

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
    MVMInstance * const instance = tc->instance;
    MVMObject          *env_hash = instance->env_hash;
    if (!env_hash) {
#ifdef _WIN32
        const MVMuint16 acp = GetACP(); /* We should get ACP at runtime. */
#endif
        MVMuint32     pos = 0;
        MVMString *needle = MVM_string_ascii_decode(tc, instance->VMString, STR_WITH_LEN("="));
        char      *env;

        MVM_gc_root_temp_push(tc, (MVMCollectable **)&needle);

        env_hash = MVM_repr_alloc_init(tc,  MVM_hll_current(tc)->slurpy_hash_type);
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&env_hash);

        while ((env = environ[pos++]) != NULL) {
#ifndef _WIN32
            MVMString    *str = MVM_string_utf8_decode(tc, instance->VMString, env, strlen(env));
#else
            char * const _env = ANSIToUTF8(acp, env);
            MVMString    *str = MVM_string_utf8_decode(tc, instance->VMString, _env, strlen(_env));

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

        instance->env_hash = env_hash;
    }
    return env_hash;
}

MVMint64 MVM_proc_spawn(MVMThreadContext *tc, MVMString *cmd, MVMString *cwd, MVMObject *env) {
    MVMint64 result;
    uv_process_t process;
    uv_process_options_t process_options;
    char   *args[4];
    int i;

    char   * const     cmdin = MVM_string_utf8_encode_C_string(tc, cmd);
    char   * const      _cwd = MVM_string_utf8_encode_C_string(tc, cwd);
    const MVMuint64     size = MVM_repr_elems(tc, env);
    char              **_env = malloc((size + 1) * sizeof(char *));
    MVMIter    * const  iter = (MVMIter *)MVM_iter(tc, env);
    MVMString  * const equal = MVM_string_ascii_decode(tc, tc->instance->VMString, STR_WITH_LEN("="));

#ifdef _WIN32
    const MVMuint16      acp = GetACP(); /* We should get ACP at runtime. */
    char    * const     _cmd = ANSIToUTF8(acp, getenv("ComSpec"));

    args[0] = _cmd;
    args[1] = "/c";
#else
    args[0]   = "/bin/sh";
    args[1]   = "-c";
#endif

    args[2]   = cmdin;
    args[3]   = NULL;

    MVMROOT(tc, iter, {
        MVMString *env_str;
        i = 0;
        while(MVM_iter_istrue(tc, iter)) {
            MVM_repr_shift_o(tc, (MVMObject *)iter);
            env_str = MVM_string_concatenate(tc, MVM_iterkey_s(tc, iter), equal);
            env_str = MVM_string_concatenate(tc, env_str, (MVMString *)MVM_iterval(tc, iter));
            _env[i++] = MVM_string_utf8_encode_C_string(tc, env_str);
        }
        _env[size] = NULL;
    });

    process_options.args  = args;
    process_options.cwd   = _cwd;
    process_options.flags = UV_PROCESS_DETACHED | UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS | UV_PROCESS_WINDOWS_HIDE;
    process_options.env   = _env;
    result = uv_spawn(tc->loop, &process, &process_options);

#ifdef _WIN32
    free(_cmd);
#endif

    free(cmdin);
    free(_cwd);

    i = 0;
    while(_env[i])
        free(_env[i++]);

    free(_env);
    return result;
}

/* generates a random int64 */
MVMint64 MVM_proc_rand_i(MVMThreadContext *tc) {
    MVMuint64 result = tinymt64_generate_uint64(tc->rand_state);
    return *(MVMint64 *)&result;
}

/* generates a number between 0 and 1 */
MVMnum64 MVM_proc_rand_n(MVMThreadContext *tc) {
    return tinymt64_generate_double(tc->rand_state);
}

/* seed random number generator */
void MVM_proc_seed(MVMThreadContext *tc, MVMint64 seed) {
    tinymt64_init(tc->rand_state, (MVMuint64)seed);
}

/* gets the system time since the epoch truncated to integral seconds */
MVMint64 MVM_proc_time_i(MVMThreadContext *tc) {
    return (MVMint64)(MVM_platform_now() / 1000000000);
}

/* gets the system time since the epoch as floating point seconds */
MVMnum64 MVM_proc_time_n(MVMThreadContext *tc) {
    return (MVMnum64)MVM_platform_now() / 1000000000.0;
}

MVMObject * MVM_proc_clargs(MVMThreadContext *tc) {
    MVMInstance * const instance = tc->instance;
    MVMObject            *clargs = instance->clargs;
    if (!clargs) {
        clargs = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_array_type);
        MVMROOT(tc, clargs, {
            MVMint64 count;

            MVMString *prog_string = MVM_string_utf8_decode(tc,
                instance->VMString,
                instance->prog_name, strlen(instance->prog_name));
            MVM_repr_push_o(tc, clargs, MVM_repr_box_str(tc,
                instance->boot_types.BOOTStr, prog_string));

            for (count = 0; count < instance->num_clargs; count++) {
                char *raw = instance->raw_clargs[count];
                MVMString *string = MVM_string_utf8_decode(tc,
                    instance->VMString,
                    instance->raw_clargs[count], strlen(instance->raw_clargs[count]));
                MVM_repr_push_o(tc, clargs, MVM_repr_box_str(tc,
                    instance->boot_types.BOOTStr, string));
            }
        });

        instance->clargs = clargs;
    }
    return clargs;
}
