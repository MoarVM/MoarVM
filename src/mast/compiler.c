#ifdef PARROT_OPS_BUILD
#define PARROT_IN_EXTENSION
#include "parrot/parrot.h"
#include "parrot/extend.h"
#include "sixmodelobject.h"
#include "nodes_parrot.h"
#else
#include "moarvm.h"
#endif

char * MVM_mast_compile(MASTNode *node, MASTNodeTypes *types) {
    return NULL;
}
