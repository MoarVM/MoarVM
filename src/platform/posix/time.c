#include "moarvm.h"
#include "platform/time.h"

#include <time.h>

MVMint64 MVM_platform_now(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		return 0;

	return (MVMint64)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
