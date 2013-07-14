void MVM_bigint_abs(MVMObject *b, MVMObject *a);
void MVM_bigint_neg(MVMObject *b, MVMObject *a);
void MVM_bigint_not(MVMObject *b, MVMObject *a);
/* unused */
/* void MVM_bigint_sqrt(MVMObject *b, MVMObject *a); */

void MVM_bigint_add(MVMObject *c, MVMObject *a, MVMObject *b);
void MVM_bigint_sub(MVMObject *c, MVMObject *a, MVMObject *b);
void MVM_bigint_mul(MVMObject *c, MVMObject *a, MVMObject *b);
void MVM_bigint_div(MVMObject *c, MVMObject *a, MVMObject *b);
void MVM_bigint_mod(MVMObject *c, MVMObject *a, MVMObject *b);
void MVM_bigint_pow(MVMObject *c, MVMObject *a, MVMObject *b);
void MVM_bigint_gcd(MVMObject *c, MVMObject *a, MVMObject *b);
void MVM_bigint_lcm(MVMObject *c, MVMObject *a, MVMObject *b);

void MVM_bigint_or(MVMObject *c, MVMObject *a, MVMObject *b);
void MVM_bigint_xor(MVMObject *c, MVMObject *a, MVMObject *b);
void MVM_bigint_and(MVMObject *c, MVMObject *a, MVMObject *b);

void MVM_bigint_shl(MVMObject *c, MVMObject *a, MVMint64 b);
void MVM_bigint_shr(MVMObject *c, MVMObject *a, MVMint64 b);

void MVM_bigint_expmod(MVMObject *d, MVMObject *a, MVMObject *b, MVMObject *c);

MVMint64 MVM_bigint_cmp(MVMObject *a, MVMObject *b);

void MVM_bigint_from_str(MVMObject *a, MVMuint8 *buf);
MVMString * MVM_bigint_to_str(MVMThreadContext *tc, MVMObject *a);
