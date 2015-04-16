#include "moar.h"

/* Takes one or more code points. If only one code point is passed, it is
 * returned as the grapheme. Otherwise, resolves it to a synthetic - either an
 * already existing one if we saw it before, or a new one if not.  Assumes
 * that the code points are already in NFC, and as such canonical ordering has
 * been applied. */
MVMGrapheme32 MVM_nfg_codes_to_grapheme(MVMThreadContext *tc, MVMCodepoint *codes, MVMint32 num_codes) {
    if (num_codes == 1) {
        return codes[0];
    }
    else {
        /* TODO: implement synthetic computation. For now always return the
        * same one. */
        return -1;
    }
}
