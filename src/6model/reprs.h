/* Pull in all of the core REPRs. */
#include "6model/reprs/MVMString.h"
#include "6model/reprs/MVMArray.h"
#include "6model/reprs/MVMHash.h"

/* REPR related functions. */
void MVM_repr_initialize_registry(MVMThreadContext *tc);
MVMuint32 MVM_repr_name_to_id(MVMThreadContext *tc, MVMString *name);
MVMREPROps * MVM_repr_get_by_id(MVMThreadContext *tc, MVMuint32 id);
MVMREPROps * MVM_repr_get_by_name(MVMThreadContext *tc, MVMString *name);
