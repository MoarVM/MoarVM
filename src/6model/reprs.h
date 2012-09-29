/* Pull in all of the core REPRs. */
#include "6model/reprs/MVMString.h"
#include "6model/reprs/MVMArray.h"
#include "6model/reprs/MVMHash.h"
#include "6model/reprs/MVMCFunction.h"
#include "6model/reprs/KnowHOWREPR.h"
#include "6model/reprs/P6opaque.h"
#include "6model/reprs/MVMCode.h"
#include "6model/reprs/MVMOSHandle.h"
#include "6model/reprs/P6int.h"
#include "6model/reprs/P6num.h"
#include "6model/reprs/Uninstantiable.h"
#include "6model/reprs/HashAttrStore.h"
#include "6model/reprs/KnowHOWAttributeREPR.h"
#include "6model/reprs/P6str.h"
#include "6model/reprs/MVMThread.h"

/* REPR related functions. */
void MVM_repr_initialize_registry(MVMThreadContext *tc);
MVMuint32 MVM_repr_name_to_id(MVMThreadContext *tc, MVMString *name);
MVMREPROps * MVM_repr_get_by_id(MVMThreadContext *tc, MVMuint32 id);
MVMREPROps * MVM_repr_get_by_name(MVMThreadContext *tc, MVMString *name);

/* Core representation IDs (determined by the order we add them
 * to the registery in reprs.c). */
#define MVM_REPR_ID_MVMString               0
#define MVM_REPR_ID_MVMArray                1
#define MVM_REPR_ID_MVMHash                 2
#define MVM_REPR_ID_MVMCFunction            3
#define MVM_REPR_ID_KnowHOWREPR             4
#define MVM_REPR_ID_P6opaque                5
#define MVM_REPR_ID_MVMCode                 6
#define MVM_REPR_ID_MVMOSHandle             7
#define MVM_REPR_ID_P6int                   8
#define MVM_REPR_ID_P6num                   9
#define MVM_REPR_ID_Uninstantiable          10
#define MVM_REPR_ID_HashAttrStore           11
#define MVM_REPR_ID_KnowHOWAttributeREPR    12
#define MVM_REPR_ID_P6str                   13
#define MVM_REPR_ID_MVMThread               14
