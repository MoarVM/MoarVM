/* various coercions and conversions */

MVMint64 MVM_coerce_istrue_s(MVMThreadContext *tc, struct _MVMString *str);
MVMint64 MVM_coerce_istrue(MVMThreadContext *tc, MVMObject *obj);

struct _MVMString * MVM_coerce_o_s(MVMThreadContext *tc, MVMObject *obj);

struct _MVMString * MVM_coerce_smart_stringify(MVMThreadContext *tc, MVMObject *obj);
