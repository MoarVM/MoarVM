#include "moarvm.h"
#include "platform/sys.h"
#include "platform/bithacks.h"

#include <windows.h>

MVMuint32 MVM_platform_cpu_count(void) {
    DWORD_PTR proc_mask, sys_mask;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask))
        return 0;

    return count_bits(proc_mask);
}
