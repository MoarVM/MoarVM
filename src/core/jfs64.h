/* This is the "A small noncryptographic PRNG" by Bob Jenkins, later given the name JFS64.
   http://burtleburtle.net/bob/rand/smallprng.html
   "I wrote this PRNG. I place it in the public domain."
   It's small, and good enough:
   https://www.pcg-random.org/posts/bob-jenkins-small-prng-passes-practrand.html
*/
/* Adapted from https://github.com/nwc10/perl5/commit/16ff20f28225d92d24c7b0d7a7196c035640b27f */

/* Find best way to ROTL64 */
/* Copied from https://github.com/Perl/perl5/blob/d6b487cec2690eccc59f18bd6c3803ea06b3c9d6/hv_macro.h */
#if defined(_MSC_VER)
  #include <stdlib.h>  /* Microsoft put _rotl declaration in here */
  #define ROTL64(x,r)  _rotl64(x,r)
#else
  /* gcc recognises this code and generates a rotate instruction for CPUs with one */
  #define ROTL64(x,r)  ( ( (MVMuint64)(x) << (r) ) | ( (MVMuint64)(x) >> ( 64 - (r) ) ) )
#endif

MVM_STATIC_INLINE MVMuint64 jfs64_generate_uint64(MVMuint64 *rand_state) {
    MVMuint64 e = rand_state[0] - ROTL64(rand_state[1], 7);
    rand_state[0] = rand_state[1] ^ ROTL64(rand_state[2], 13);
    rand_state[1] = rand_state[2] + ROTL64(rand_state[3], 37);
    rand_state[2] = rand_state[3] + e;
    rand_state[3] = e + rand_state[0];
    return rand_state[3];
}

MVM_STATIC_INLINE void jfs64_init(MVMuint64 *rand_state, MVMuint64 seed) {
    rand_state[0] = 0xf1ea5eed;
    rand_state[1] = rand_state[2] = rand_state[3] = seed;
    for (int i = 0; i < 20; ++i) {
        (void)jfs64_generate_uint64(rand_state);
    }
}
