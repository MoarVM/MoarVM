#if _MSC_VER < 1800
#include <msinttypes/inttypes.h>
/* Print size_t values. */
#define MVM_PRSz "Iu"
#else
#include <inttypes.h>
#define MVM_PRSz "zu" /* C99 */
#endif
