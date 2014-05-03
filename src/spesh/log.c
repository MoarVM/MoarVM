#include "moar.h"

/* In order to collect more information to use during specialization, we
 * make a pass through the code inserting logging instructions after a
 * range of insturctions that obtain data we can't reason about easily
 * statically. After a number of logging runs, the collected data is
 * used as an additional "fact" source while specializing. */

void MVM_spesh_log_add_logging(MVMThreadContext *tc, MVMSpeshGraph *g) {
}
