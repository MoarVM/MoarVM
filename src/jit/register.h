/* different places in which we can store stuff */
typedef enum {
    MVM_JIT_STORAGE_NONE,
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


#define MVM_JIT_REG_MK2(arch,name) arch ## _ ## name
#define MVM_JIT_REG_MK1(arch,name) MVM_JIT_REG_MK2(arch,name)
#define MVM_JIT_REG(name) MVM_JIT_REG_MK1(MVM_JIT_ARCH,name)

#define MVM_JIT_REGISTER_REQUIRE(name) (0x80 | MVM_JIT_REG(name))
#define MVM_JIT_REGISTER_IS_USED(spec) (spec != MVM_JIT_STORAGE_NONE)
#define MVM_JIT_REGISTER_HAS_REQUIREMENT(spec) ((spec & 0x80) != 0)
#define MVM_JIT_REGISTER_REQUIREMENT(spec) (spec & 0x7f)



void MVM_jit_linear_scan_allocate(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTileList *list);
void MVM_jit_arch_storage_for_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                      MVMJitExprTree *tree, MVMint32 arglist_node,
                                      MVMJitStorageRef *storage);
