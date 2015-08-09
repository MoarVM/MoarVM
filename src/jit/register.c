#include "moar.h"
#include "internal.h"
/* Online register allocater requires the following operations:
 * alloc/free
 * take/release
 * spill/load
 * subset/release
 * invalidate.

 * We also require a 'default implementation' of register
 * allocation for default tiles.
 *
 * Registers can be USED, ALLOCATED or FREE.
 * When a register is USED, it cannot be allocated.
 * When a register is ALLOCATED, its value must be spilled before it can be reallocated
 * When a register is FREE, it can be allocated directly.
 *
 * release transfers the register state from USED to ALLOCATED
 * free transfers the register state from USED or ALLOCATED to FREE
 * spill transfers a register state from ALLOCATED to FREE,
 * emits a value spill, and stores the value spill location
 * alloc tries to take a FREE register if any; if none it spills
 * an ALLOCATED register, and transfers the state to USED
 * take tries to take register N. If N is USED, this is an error.
 * If N is ALLOCATED, it is spilt. It transfers the state of N to USED
 * load emits a load of value V to register N and transfers the state of N to ALLOCATED
 * INVALIDATE spills all ALLOCATED registers (but does not touch USED registers)
 */




MVMint8 MVM_jit_register_alloc(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls) {


}

void MVM_jit_register_free(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls,
                           MVMint8 reg_num) {

}
