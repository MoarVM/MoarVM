/* different places in which we can store stuff */
typedef enum {
    MVM_JIT_STORAGE_GPR,  /* general purpose register */
    MVM_JIT_STORAGE_FPR,  /* floating point register */
    MVM_JIT_STORAGE_LOCAL,
    MVM_JIT_STORAGE_STACK,
} MVMJitStorageClass;

/* a reference to a place something is stored */
typedef struct {
    MVMJitStorageClass _cls;
    MVMint32           _pos;
} MVMJitStorageRef; /* I'll never run out of names for a CONS */


/* REGISTER REQUIREMENT SPECIFICATION
 *
 * The goal is to represent required registers for a tile. I've assumed that 6
 * bits are sufficient to specify a register number (64 possibilities; for
 * x86-64 we'd need 16 for GPR, 16 for the FPR, so that'd be 32, and I'm not
 * 100% sure that using register numbers that way is right for the FPR/GPR
 * distinction.
 *
 * We need to encode two facts:
 * - this value needs to be in a register
 * - and that register should be $such
 *
 * So we use the following layout:
 *
 * +---------------+--------------------------+-------------------------------+
 * | Used? (1 bit) | Has Requirement? (1 bit) | Required register nr (6 bits) |
 * +---------------+--------------------------+-------------------------------+
 *
 * Which is then repeated four times over an unsgined 32 bit integer. The first
 * value always specifies the output register, so that we can determine if a
 * tile has any register output simply by (spec & 1).
 */

#define MVM_JIT_REG_MK2(arch,name) arch ## _ ## name
#define MVM_JIT_REG_MK1(arch,name) MVM_JIT_REG_MK2(arch,name)
#define MVM_JIT_REG(name) MVM_JIT_REG_MK1(MVM_JIT_ARCH,name)


#define MVM_JIT_REGISTER_NONE 0
#define MVM_JIT_REGISTER_ANY  1
#define MVM_JIT_REGISTER_REQUIRE(name) (3 | ((MVM_JIT_REG(name)) << 2))
#define MVM_JIT_REGISTER_ENCODE(spec,n) ((spec) << (8*(n)))

#define MVM_JIT_REGISTER_FETCH(spec,n) (((spec) >> (8*(n)))&0xff)
#define MVM_JIT_REGISTER_IS_USED(desc) ((desc) & 1)
#define MVM_JIT_REGISTER_HAS_REQUIREMENT(desc) (((desc) & 2) >> 1)
#define MVM_JIT_REGISTER_REQUIREMENT(desc) (((desc) & 0xfc) >> 2)



void MVM_jit_linear_scan_allocate(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTileList *list);
void MVM_jit_arch_storage_for_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                      MVMJitExprTree *tree, MVMint32 arglist_node,
                                      MVMJitStorageRef *storage);
