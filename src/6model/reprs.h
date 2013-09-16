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
#include "6model/reprs/MVMIter.h"
#include "6model/reprs/MVMContext.h"
#include "6model/reprs/SCRef.h"
#include "6model/reprs/Lexotic.h"
#include "6model/reprs/MVMCallCapture.h"
#include "6model/reprs/P6bigint.h"
#include "6model/reprs/NFA.h"
#include "6model/reprs/MVMException.h"
#include "6model/reprs/MVMStaticFrame.h"
#include "6model/reprs/MVMCompUnit.h"

/* REPR related functions. */
void MVM_repr_initialize_registry(MVMThreadContext *tc);
MVMuint32 MVM_repr_name_to_id(MVMThreadContext *tc, MVMString *name);
const MVMREPROps * MVM_repr_get_by_id(MVMThreadContext *tc, MVMuint32 id);
const MVMREPROps * MVM_repr_get_by_name(MVMThreadContext *tc, MVMString *name);

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
#define MVM_REPR_ID_MVMIter                 15
#define MVM_REPR_ID_MVMContext              16
#define MVM_REPR_ID_SCRef                   17
#define MVM_REPR_ID_Lexotic                 18
#define MVM_REPR_ID_MVMCallCapture          19
#define MVM_REPR_ID_P6bigint                20
#define MVM_REPR_ID_NFA                     21
#define MVM_REPR_ID_MVMException            22
#define MVM_REPR_ID_MVMStaticFrame          23
#define MVM_REPR_ID_MVMCompUnit             24

/* FIXME: defined in core/instance.h */
#if 0
#define MVM_REPR_CORE_COUNT                 25
#endif

/* Default attribute functions for a REPR that lacks them. */
extern const MVMREPROps_Attribute MVM_REPR_DEFAULT_ATTR_FUNCS;

/* Default boxing functions for a REPR that lacks them. */
extern const MVMREPROps_Boxing MVM_REPR_DEFAULT_BOX_FUNCS;

/* Default positional functions for a REPR that lacks them. */
extern const MVMREPROps_Positional MVM_REPR_DEFAULT_POS_FUNCS;

/* Default associative functions for a REPR that lacks them. */
extern const MVMREPROps_Associative MVM_REPR_DEFAULT_ASS_FUNCS;

/* Default elems function for a REPR that lacks it. */
MVMuint64 MVM_REPR_DEFAULT_ELEMS(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data);
