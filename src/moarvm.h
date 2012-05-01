/* Pull in the APR. */
#define APR_DECLARE_STATIC 1
#include <apr_general.h>
#include <apr_hash.h>

/* Configuration. */
#include "gen/config.h"

/* Headers for APIs for various other data structures and APIs. */
#include "6model/6model.h"
#include "core/threadcontext.h"
#include "core/instance.h"
#include "core/exceptions.h"
#include "6model/reprs.h"
#include "6model/bootstrap.h"
#include "gc/allocation.h"
#include "gc/nursery.h"
#include "gc/wb.h"
#include "strings/ascii.h"

/* Top level VM API functions. */
MVMInstance * MVM_vm_create_instance(void);
void MVM_vm_destroy_instance(MVMInstance *instance);
