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
