/* Pull in the APR. */
#define APR_DECLARE_STATIC 1
#include "apr.h"

/* Configuration. */
#include "gen/config.h"

/* Headers for APIs for various other data structures and APIs. */
#include "core/threadcontext.h"
#include "core/instance.h"

/* Top level VM API functions. */
void MVM_vm_create_instance(void);
