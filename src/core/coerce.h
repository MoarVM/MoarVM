/* Boolification. */
MVMint64 MVM_coerce_istrue_s(MVMThreadContext *tc, MVMString *str);

/* Stringification. */
MVMString * MVM_coerce_i_s(MVMThreadContext *tc, MVMint64 i);
MVMString * MVM_coerce_u_s(MVMThreadContext *tc, MVMuint64 i);
MVMString * MVM_coerce_n_s(MVMThreadContext *tc, MVMnum64 n);

/* Numification. */
MVMint64 MVM_coerce_s_i(MVMThreadContext *tc, MVMString *s);
MVMuint64 MVM_coerce_s_u(MVMThreadContext *tc, MVMString *s);
MVMint64 MVM_coerce_simple_intify(MVMThreadContext *tc, MVMObject *obj);
MVMObject* MVM_radix(MVMThreadContext *tc, MVMint64 radix, MVMString *str, MVMint64 offset, MVMint64 flag);

/* Size of the int to string coercion cache (we cache 0 ..^ this). */
#define MVM_INT_TO_STR_CACHE_SIZE 64
