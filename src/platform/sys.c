#include "moar.h"
#include "platform/sys.h"

MVMuint32 MVM_platform_cpu_count(void) {
    int count;
    uv_cpu_info_t *info;
    int e;

    e = uv_cpu_info(&info, &count);
    if (e == 0) uv_free_cpu_info(info, count);

    return count;
}

MVMObject * MVM_platform_uname(MVMThreadContext *tc) {
    int error;
    uv_utsname_t uname;
    MVMObject *result;

    if ((error = uv_os_uname(&uname)) != 0)
        MVM_exception_throw_adhoc(tc, "Unable to uname: %s", uv_strerror(error));

    MVMROOT(tc, result, {
        MVMString *sysname = MVM_string_utf8_decode(tc, tc->instance->VMString, uname.sysname, strlen((char *)uname.sysname));
        MVMString *release = MVM_string_utf8_decode(tc, tc->instance->VMString, uname.release, strlen((char *)uname.release));
        MVMString *version = MVM_string_utf8_decode(tc, tc->instance->VMString, uname.version, strlen((char *)uname.version));
        MVMString *machine = MVM_string_utf8_decode(tc, tc->instance->VMString, uname.machine, strlen((char *)uname.machine));
        result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStrArray);
        MVM_repr_bind_pos_s(tc, result, 0, sysname);
        MVM_repr_bind_pos_s(tc, result, 1, release);
        MVM_repr_bind_pos_s(tc, result, 2, version);
        MVM_repr_bind_pos_s(tc, result, 3, machine);
    });

    return result;
}
