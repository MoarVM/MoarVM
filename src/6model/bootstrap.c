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

/* Creates a stub BOOTArray (missing a meta-object). */
static void create_stub_BOOTArray(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMArray);
    tc->instance->boot_types->BOOTArray = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTHash (missing a meta-object). */
static void create_stub_BOOTHash(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMHash);
    tc->instance->boot_types->BOOTHash = repr->type_object_for(tc, NULL);
}

/* Creates a stub BOOTCCode (missing a meta-object). */
static void create_stub_BOOTCCode(MVMThreadContext *tc) {
    MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_MVMCFunction);
    tc->instance->boot_types->BOOTCCode = repr->type_object_for(tc, NULL);
}

/* Bootstraps the KnowHOW type. */
static void bootstrap_KnowHOW(MVMThreadContext *tc) {
    MVMObject *BOOTStr   = tc->instance->boot_types->BOOTStr;
    MVMObject *BOOTArray = tc->instance->boot_types->BOOTArray;
    MVMObject *BOOTHash  = tc->instance->boot_types->BOOTHash;
    
    /* Create our KnowHOW type object. Note we don't have a HOW just yet, so
     * pass in NULL. */
    MVMREPROps *REPR    = MVM_repr_get_by_id(tc, MVM_REPR_ID_KnowHOWREPR);
    MVMObject  *knowhow = REPR->type_object_for(tc, NULL);

    /* We create a KnowHOW instance that can describe itself. This means
     * (once we tie the knot) that .HOW.HOW.HOW.HOW etc will always return
     * that, which closes the model up. */
    MVMKnowHOWREPR *knowhow_how = (MVMKnowHOWREPR *)REPR->allocate(tc, NULL);
    
    /* Create an STable for the knowhow_how. */
    MVMSTable *st = MVM_gc_allocate_stable(tc, REPR, (MVMObject *)knowhow_how);
    st->WHAT = (MVMObject *)knowhow;
    knowhow_how->common.st = st;
    
    /* Add various methods to the KnowHOW's HOW. */
    knowhow_how->body.methods    = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
    knowhow_how->body.attributes = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    /* XXX TODO: add the methods */
    
    /* Set name KnowHOW for the KnowHOW's HOW. */
    knowhow_how->body.name = MVM_string_ascii_decode_nt(tc, BOOTStr, "KnowHOW");

    /* Set this built up HOW as the KnowHOW's HOW. */
    STABLE(knowhow)->HOW = (MVMObject *)knowhow_how;
    
    /* Give it an authoritative method cache; this in turn will make the
     * method dispatch bottom out. */
    STABLE(knowhow)->method_cache = knowhow_how->body.methods;
    STABLE(knowhow)->mode_flags   = MVM_METHOD_CACHE_AUTHORITATIVE;
    
    /* Associate the created objects with the intial core serialization
     * context. */
    /* XXX TODO */

    /* Stash the created KnowHOW. */
    tc->instance->KnowHOW = (MVMObject *)knowhow;
}
 
/* Drives the overall bootstrap process. */
void MVM_6model_bootstrap(MVMThreadContext *tc) {
    /* First, we have to get the BOOTStr type to exist; this has to
     * come even before REPR registry setup because it relies on
     * being able to create strings. */
    create_stub_BOOTStr(tc);
    
    /* Now we've enough to actually create the REPR registry. */
    MVM_repr_initialize_registry(tc);
    
    /* Create stub BOOTArray, BOOTHash and BOOTCCode types. */
    create_stub_BOOTArray(tc);
    create_stub_BOOTHash(tc);
    create_stub_BOOTCCode(tc);
    
    /* Bootstrap the KnowHOW type, giving it a meta-object. */
    bootstrap_KnowHOW(tc);
    
    /* XXX Give BOOTStr, BOOTArray, BOOTHash and BOOTCode meta-objects... */
}
