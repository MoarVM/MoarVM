#include "moarvm.h"

/* This file implements the various steps involved in getting 6model
 * bootstrapped from the ground up - that is, getting to having a
 * KnowHOW meta-object type so that userland can start building up
 * more interesting meta-objects. Mostly it just has to make objects
 * with some "holes", and later go back and fill them out. This is
 * due to the circular nature of things.
 */

/* Creates a stub BOOTStr. Note we didn't initialize the
 * representation yet, so have to do this somewhat pokily. */
static void create_stub_BOOTStr(MVMThreadContext *tc) {
    /* Need to create the REPR function table "in advance"; the
     * MVMString REPR specially knows not to duplicately create
     * this. */
    MVMREPROps *repr = MVMString_initialize(tc);
    
    /* Now we can create a type object; note we have no HOW yet,
     * though. */
    MVMSTable *st  = MVM_gc_allocate_stable(tc, repr, NULL);
    
    /* We can now go for the type object. */
    tc->instance->boot_types->BOOTStr = MVM_gc_allocate_type_object(tc, st);
    
    /* Set the WHAT in the STable we just made to point to the type
     * object (this is completely normal). */
    st->WHAT = tc->instance->boot_types->BOOTStr;
}
 
/* Drives the overall bootstrap process. */
void MVM_6model_bootstrap(MVMThreadContext *tc) {
    /* First, we have to get the BOOTStr type to exist; this has to
     * come even before REPR registry setup because it relies on
     * being able to create strings. */
    create_stub_BOOTStr(tc);
    
    /* Now we've enough to actually create the REPR registry. */
    MVM_repr_initialize_registry(tc);
    
    /* XXX Much more to come... */
}
