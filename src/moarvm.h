/* Pull in the APR. */
#define APR_DECLARE_STATIC 1
#include <apr_general.h>

/* Configuration. */
#include "gen/config.h"

/* Headers for APIs for various other data structures and APIs. */
#include "core/threadcontext.h"
#include "core/instance.h"
#include "core/exceptions.h"
#include "gc/allocation.h"
#include "gc/nursery.h"
#include "strings/string.h"

/* Top level VM API functions. */
MVMInstance * MVM_vm_create_instance(void);
void MVM_vm_destroy_instance(MVMInstance *instance);
