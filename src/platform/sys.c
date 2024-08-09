#include "moar.h"
#include "platform/sys.h"

MVMint64 MVM_platform_cpu_count(void) {
    int            count;
    uv_cpu_info_t *info;
    int            e;

    e = uv_cpu_info(&info, &count);
    if (e == 0) uv_free_cpu_info(info, count);

    return count;
}

MVMint64 MVM_platform_free_memory(void) {
    return uv_get_free_memory();
}

MVMint64 MVM_platform_total_memory(void) {
    return uv_get_total_memory();
}

MVMObject * MVM_platform_uname(MVMThreadContext *tc) {
    int           error;
    uv_utsname_t  uname;
    MVMObject    *result = NULL;

    if ((error = uv_os_uname(&uname)) != 0)
        MVM_exception_throw_adhoc(tc, "Unable to uname: %s", uv_strerror(error));

    MVMROOT(tc, result) {
        result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStrArray);

        MVM_repr_push_s(
            tc,
            result,
            MVM_string_utf8_decode(tc, tc->instance->VMString, uname.sysname, strlen((char *)uname.sysname))
        );

        MVM_repr_push_s(
            tc,
            result,
            MVM_string_utf8_decode(tc, tc->instance->VMString, uname.release, strlen((char *)uname.release))
        );

        MVM_repr_push_s(
            tc,
            result,
            MVM_string_utf8_decode(tc, tc->instance->VMString, uname.version, strlen((char *)uname.version))
        );

        MVM_repr_push_s(
            tc,
            result,
            MVM_string_utf8_decode(tc, tc->instance->VMString, uname.machine, strlen((char *)uname.machine))
        );
    }

    return result;
}
