/* Boolification. */
MVMint64 MVM_coerce_istrue_s(MVMThreadContext *tc, struct _MVMString *str);
MVMint64 MVM_coerce_istrue(MVMThreadContext *tc, MVMObject *obj);

/* Stringification. */
struct _MVMString * MVM_coerce_i_s(MVMThreadContext *tc, MVMint64 i);
struct _MVMString * MVM_coerce_n_s(MVMThreadContext *tc, MVMnum64 n);
struct _MVMString * MVM_coerce_o_s(MVMThreadContext *tc, MVMObject *obj);
struct _MVMString * MVM_coerce_smart_stringify(MVMThreadContext *tc, MVMObject *obj);

/* Numification. */
MVMint64 MVM_coerce_s_i(MVMThreadContext *tc, struct _MVMString *s);
MVMnum64 MVM_coerce_s_n(MVMThreadContext *tc, struct _MVMString *s);
