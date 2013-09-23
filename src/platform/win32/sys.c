#include "moarvm.h"
#include "bithacks.h"
#include "platform/sys.h"

#include <windows.h>

MVMuint32 MVM_platform_cpu_count(void) {
    DWORD_PTR proc_mask, sys_mask;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask))
        return 0;

    return MVM_bithacks_count_bits(proc_mask);
}
