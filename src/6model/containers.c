#include "moarvm.h"

/* ***************************************************************************
 * CodePair container configuration: container with FETCH/STORE code refs
 * ***************************************************************************/

typedef struct {
    PMC *fetch_code;
    PMC *store_code;
} CodePairContData;