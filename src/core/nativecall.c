#include "moar.h"

void MVM_nativecall_build(MVMThreadContext *tc, MVMObject *site, MVMString *lib,
        MVMString *sym, MVMString *conv, MVMObject *arg_spec, MVMObject *ret_spec) {
    MVM_exception_throw_adhoc(tc, "nativecallbuild NYI");
}

MVMObject * MVM_nativecall_invoke(MVMThreadContext *tc, MVMObject *ret_type,
        MVMObject *site, MVMObject *args) {
    MVM_exception_throw_adhoc(tc, "nativecallinvoke NYI");
}

void MVM_nativecall_refresh(MVMThreadContext *tc, MVMObject *cthingy) {
    MVM_exception_throw_adhoc(tc, "nativecallrefresh NYI");
}
