#include "moar.h"
#include "platform/time.h"

#include <windows.h>

/* see http://support.microsoft.com/kb/167296 */
#define OFFSET 116444736000000000

#define E6 1000000

MVMuint64 MVM_platform_now(void)
{
    union { FILETIME ft; MVMuint64 u; } now;
    GetSystemTimeAsFileTime(&now.ft);
    return (now.u - OFFSET) * 100;
}

void MVM_platform_sleep(MVMnum64 second)
{

    DWORD millis = (DWORD)(second * 1000);

    Sleep(millis);
}

void MVM_platform_nanosleep(MVMuint64 nanos)
{
    MVMuint64 now;
    DWORD millis;
    const MVMuint64 end = MVM_platform_now() + nanos;

    millis = (DWORD)((nanos + E6 - 1) / E6);

    while(1) {
        Sleep(millis);
        now = MVM_platform_now();

        if (now >= end)
            break;

        millis = (DWORD)((end - now) / E6);
    }
}
