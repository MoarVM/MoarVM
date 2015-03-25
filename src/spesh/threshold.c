#include "moar.h"

/* Choose the threshold for a given static frame before we start applying
 * specialization to it. */
MVMuint32 MVM_spesh_threshold(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMuint32 bs = sf->body.bytecode_size;
    if (bs <= 256)
        return 150;
    else if (bs <= 512)
        return 200;
    else if (bs <= 2048)
        return 250;
    else
        return 300;
}
