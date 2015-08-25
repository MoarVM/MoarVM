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
 *
 * load emits a load of value V to register N and transfers the state of
 * N to ALLOCATED * INVALIDATE spills all ALLOCATED registers (but does
 * not touch USED registers) */

#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64
static MVMint8 free_gpr[] = {
    X64_FREE_GPR(MVM_JIT_REGNAME)
};
static MVMint8 free_num[] = {
    X64_SSE(MVM_JIT_REGNAME)
};
#else
static MVMint8 free_gpr[] = { -1 };
static MVMint8 free_num[] = { -1 };
#endif

/* Register lock bitmap macros */
#define REGISTER_LOCKED(a, n) ((a)->reg_lock &  (1 << (n)))
#define LOCK_REGISTER(a, n)   ((a)->reg_lock |= (1 << (n)))
#define UNLOCK_REGISTER(a,n)  ((a)->reg_lock ^= (1 << (n)))


/* NB only implement GPR register allocation now */
void MVM_jit_register_allocator_init(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                     MVMJitRegisterAllocator *alc) {
    /* Live ranges */
    MVM_DYNAR_INIT(alc->active, sizeof(free_gpr));
    /* Initialize free register stacks */
    memcpy(alc->free_reg, free_gpr, sizeof(free_gpr));
    memset(alc->reg_use, 0, sizeof(alc->reg_use));
    alc->reg_top   = 0;
    alc->spill_top = 0;
    alc->reg_lock  = 0;
    compiler->allocator = alc;
}

void MVM_jit_register_allocator_deinit(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                       MVMJitRegisterAllocator *alc) {
    MVM_free(alc->active);
    compiler->allocator = NULL;
}

#define NYI(x) MVM_oops(tc, #x " NYI");



MVMint8 MVM_jit_register_alloc(MVMThreadContext *tc, MVMJitCompiler *cl, MVMint32 reg_cls) {
    MVMJitRegisterAllocator *alc = cl->allocator;
    MVMint8 reg;
    if (reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    } else {
        if (alc->reg_top == sizeof(alc->free_reg)) {
            /* Out of registers, spill something */
            NYI(spill);
        }
        return alc->free_reg[alc->reg_top++];
    }
}


/* Freeing a register makes it available again */
void MVM_jit_register_free(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    if (reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    } else {
        if (alc->reg_top == 0) {
            MVM_oops(tc, "Trying to free too many registers");
        }
        /* Add to register stack */
        alc->free_reg[--alc->reg_top] = reg_num;
    }
}

/* Use marks a register currently in use */
void MVM_jit_register_use(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    if (reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    } else {
        LOCK_REGISTER(compiler->allocator, reg_num);
    }

}

void MVM_jit_register_release(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    if (reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    } else {
        UNLOCK_REGISTER(compiler->allocator, reg_num);
    }
}


void spill_value(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprValue *value) {
    NYI(spill_value);
}

void MVM_jit_register_spill(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 spill_location, MVMint32 reg_cls, MVMint8 reg_num, MVMint32 size) {
    MVMint32 i;
    /* When spilling a register, all values currently assigned to that register should be marked spilled */
    MVM_jit_emit_spill(tc, compiler, spill_location, reg_cls, reg_num, size);
    for (i = 0; i < compiler->allocator->active_num; i++) {
        MVMJitExprValue *value = compiler->allocator->active[i];
        if (value->state == MVM_JIT_VALUE_ALLOCATED &&
            value->u.reg.cls == reg_cls && value->u.reg.num == reg_num) {
            value->spill_location = spill_location;
            value->state = MVM_JIT_VALUE_SPILLED;
            compiler->allocator->reg_use[reg_num]--;
        }
    }
    if (compiler->allocator->reg_use[reg_num] != 0) {
        MVM_oops(tc, "After spill no users of the registers should remain");
    }
}

void MVM_jit_register_load(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 spill_location,
                           MVMint32 reg_cls, MVMint8 reg_num, MVMint32 size) {
    MVMint32 i;
    MVM_jit_emit_load(tc, compiler, spill_location, reg_cls, reg_num, size);
    /* All active values assigned to that spill location are marked allocated */
    for (i = 0; i < compiler->allocator->active_num; i++) {
        MVMJitExprValue *value = compiler->allocator->active[i];
        if (value->spill_location == spill_location) {
            value->u.reg.cls = reg_cls;
            value->u.reg.num = reg_num;
            value->state     = MVM_JIT_VALUE_ALLOCATED;
            compiler->allocator->reg_use[reg_num]++;
        }
    }
}


/* Assign a register to a value. Useful primitive for 'virtual copy' */
void MVM_jit_register_assign(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprValue *value, MVMint32 reg_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = cl->allocator;
    value->u.reg.num = reg_num;
    value->u.reg.cls = reg_cls;
    MVM_DYNAR_PUSH(alc->active, value);
    alc->reg_use[reg_num]++;
}

/* Expiring a value marks it dead and possibly releases its register */
void MVM_jit_register_expire(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprValue *value) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    MVMint8 reg_num = value->u.reg.num;
    MVMint32 i;
    if (value->u.reg.cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    }
    /* Remove value from active */
    for (i = 0; i < alc->active_num; i++) {
        if (alc->active[i] == value) {
            /* splice it out */
            alc->active_num--;
            alc->active[i] = alc->active[alc->active_num];
        }
    }
    /* Mark value as dead */
    value->state = MVM_JIT_VALUE_DEAD;
    /* Decrease register number count and free if possible */
    alc->reg_use[reg_num]--;
    if (alc->reg_use[reg_num] == 0) {
        MVM_jit_register_free(tc, compiler, value->u.reg.cls, reg_num);
    }
}


/* Spill values prior to emitting a call */
void MVM_jit_spill_before_call(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprTree *tree, MVMint32 node) {
    MVMJitRegisterAllocator *alc = cl->allocator;
    MVMint32 order_nr = tree->info[node].value.order_nr;
    MVMint32 i;
    for (i = 0; i < alc->active_num; i++) {
        MVMJitExprValue *v = alc->active[i];
        /* currently allocate nodes that are live after the call need to be spilled */
        if (v->last_use > order_nr && v->state == MVM_JIT_VALUE_ALLOCATED) {
            spill_value(tc, cl, v);
        }
    }
}


/* Expire values that are no longer useful */
void MVM_jit_expire_values(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 order_nr) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    MVMint32 i = 0;
    while (i < alc->active_num) {
        MVMJitExprValue *value = alc->active[i];
        if (value->last_use <= order_nr) {
            MVM_jit_register_expire(tc, compiler, value);
        } else {
            i++;
        }
    }
}

