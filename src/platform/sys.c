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

void MVM_platform_uname(MVMThreadContext *tc, MVMObject *result) {
    uv_utsname_t uname;
    int r = uv_os_uname(&uname);
    if (r != 0)
        MVM_exception_throw_adhoc(tc, "Unable to uname: %s", uv_strerror(r));
    if (REPR(result)->ID != MVM_REPR_ID_VMArray || !IS_CONCRETE(result) ||
            ((MVMArrayREPRData *)STABLE(result)->REPR_data)->slot_type != MVM_ARRAY_STR) {
        MVM_exception_throw_adhoc(tc, "uname needs a concrete string array.");
    }

    MVMROOT(tc, result, {
        MVM_repr_bind_pos_s(tc, result, 0, MVM_string_utf8_decode(tc, tc->instance->VMString, uname.sysname, strlen((char *)uname.sysname)));
        MVM_repr_bind_pos_s(tc, result, 1, MVM_string_utf8_decode(tc, tc->instance->VMString, uname.release, strlen((char *)uname.release)));
        MVM_repr_bind_pos_s(tc, result, 2, MVM_string_utf8_decode(tc, tc->instance->VMString, uname.version, strlen((char *)uname.version)));
        MVM_repr_bind_pos_s(tc, result, 3, MVM_string_utf8_decode(tc, tc->instance->VMString, uname.machine, strlen((char *)uname.machine)));
    });
}
