#include "moarvm.h"
#include "platform/time.h"

#include <windows.h>

/* see http://support.microsoft.com/kb/167296 */
#define OFFSET 116444736000000000

MVMint64 MVM_platform_now(void)
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return (((MVMint64)ft.dwHighDateTime << 32 | ft.dwLowDateTime) - OFFSET) * 100;
}
