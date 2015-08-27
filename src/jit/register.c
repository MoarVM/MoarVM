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

#define NUM_GPR sizeof(free_gpr)
#define NEXT_REG(x) (((x)+1)%NUM_GPR)


/* Register lock bitmap macros */
#define REGISTER_IS_LOCKED(a, n) ((a)->reg_lock &  (1 << (n)))
#define LOCK_REGISTER(a, n)   ((a)->reg_lock |= (1 << (n)))
#define UNLOCK_REGISTER(a,n)  ((a)->reg_lock ^= (1 << (n)))


/* NB only implement GPR register allocation now */
void MVM_jit_register_allocator_init(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                     MVMJitRegisterAllocator *alc) {
    /* Store live ranges */
    MVM_DYNAR_INIT(alc->active, NUM_GPR);
    /* Initialize free register stacks */
    memcpy(alc->free_reg, free_gpr, NUM_GPR);
    memset(alc->reg_use, 0, sizeof(alc->reg_use));

    alc->reg_give  = 0;
    alc->reg_take  = 0;

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
    MVMint8 reg_num;
    if (reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    } else {
        if (NEXT_REG(alc->reg_take) == alc->reg_give) {
            /* Out of registers, spill something */
            NYI(spill_something);
        }
        /* Use a circular handout scheme for the 'fair use' of registers */
        reg_num       = alc->free_reg[alc->reg_take];
        alc->reg_take = NEXT_REG(alc->reg_take);
    }
    return reg_num;
}


/* Freeing a register makes it available again */
void MVM_jit_register_free(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    if (reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    } else {
        if (alc->reg_give == alc->reg_take) {
            MVM_oops(tc, "Trying to free too many registers");
        }
        alc->free_reg[alc->reg_give] = reg_num;
        alc->reg_give                = NEXT_REG(alc->reg_give);
    }
}

void MVM_jit_register_take(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    MVMint32 i;
    if (reg_cls == MVM_JIT_REGCLS_NUM)
        NYI(numeric_regs);
    if (REGISTER_IS_LOCKED(compiler->allocator, reg_num))
        MVM_oops(tc, "Trying to take a locked register");
    /* Spill register, if it is in use */
    if (alc->reg_use[reg_num] > 0) {
        MVM_jit_register_spill(tc, compiler, reg_cls, reg_num);
    }
    /* Take register directly from allocator */
    for (i = alc->reg_take; NEXT_REG(i) != alc->reg_give; i = NEXT_REG(i)) {
        if (alc->free_reg[i] == reg_num) {
            /* swap with next take register, which is overwritten anyway */
            alc->free_reg[i] = alc->free_reg[alc->reg_take];
            alc->reg_take    = NEXT_REG(alc->reg_take);
            return;
        }
    }
    MVM_oops(tc, "Could not take register even after spilling");
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

#define VALUE_IS_ASSIGNED(v, rc, rn) ((v)->state == MVM_JIT_VALUE_ALLOCATED && (v)->u.reg.cls == (rc) && (v)->u.reg.num == (rn))

void MVM_jit_register_spill(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    MVMint32 i, spill_location = 0;
    MVMint8 *spill_bmp = MVM_calloc(alc->spill_top, sizeof(MVMint8));
    for (i = 0; i < alc->active_num; i++) {
        MVMJitExprValue *value = alc->active[i];
        if (VALUE_IS_ASSIGNED(value, reg_cls, reg_num) && value->spill_location > 0) {
            /*
            if (spill_location != 0 && value->spill_location != spill_location)
                MVM_oops(tc, "Inconsistent spill location!");
            */
            spill_location = value->spill_location;
            break;
        } else if (value->spill_location > 0) {
            spill_bmp[value->spill_location] = 1;
        }
    }
    if (spill_location == 0) {
        /* This value was not yet spilled, find a location */
        for (i = 0; i < alc->spill_top; i++) {
            if (spill_bmp[i] == 0) {
                spill_location = i;
                break;
            }
        }
        if (spill_location == 0) {
            /* Bitmap was full */
            spill_location = alc->spill_top++;
        }
        MVM_jit_emit_spill(tc, compiler, spill_location, reg_cls, reg_num, MVM_JIT_REG_SZ);
    } /* if it was spilled before, it's immutable now */

    /* Mark nodes as spilled on the location */
    for (i = 0; i < alc->active_num; i++) {
        MVMJitExprValue *value = compiler->allocator->active[i];
        if (VALUE_IS_ASSIGNED(value, reg_cls, reg_num)) {
            value->spill_location = spill_location;
            value->state = MVM_JIT_VALUE_SPILLED;
            alc->reg_use[reg_num]--;
        }
    }

    /* register ought to be free now! */
    if (alc->reg_use[reg_num] != 0) {
        MVM_oops(tc, "After spill no users of the registers should remain");
    }
    /* make it available */
    MVM_jit_register_free(tc, compiler, reg_cls, reg_num);
}





void MVM_jit_register_load(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 spill_location,
                           MVMint32 reg_cls, MVMint8 reg_num, MVMint32 size) {
    MVMint32 i;
    MVM_jit_emit_load(tc, compiler, spill_location, reg_cls, reg_num, size);
    /* All active values assigned to that spill location are marked allocated */
    for (i = 0; i < compiler->allocator->active_num; i++) {
        MVMJitExprValue *value = compiler->allocator->active[i];
        if (value->spill_location == spill_location) {
            value->u.reg.cls    = reg_cls;
            value->u.reg.num    = reg_num;
            value->state        = MVM_JIT_VALUE_ALLOCATED;
            value->last_created = compiler->order_nr;
            compiler->allocator->reg_use[reg_num]++;
        }
    }
}


/* Assign a register to a value */
void MVM_jit_register_assign(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprValue *value, MVMint32 reg_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = cl->allocator;
    value->state        = MVM_JIT_VALUE_ALLOCATED;
    value->u.reg.num    = reg_num;
    value->u.reg.cls    = reg_cls;
    value->last_created = cl->order_nr;

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
void MVM_jit_spill_before_call(MVMThreadContext *tc, MVMJitCompiler *cl) {
    MVMJitRegisterAllocator *alc = cl->allocator;
    MVMint32 order_nr = cl->order_nr;
    MVMint32 i;
    for (i = 0; i < alc->active_num; i++) {
        MVMJitExprValue *v = alc->active[i];
        if (v->last_use > order_nr && v->state == MVM_JIT_VALUE_ALLOCATED) {
            MVM_jit_register_spill(tc, cl, v->u.reg.cls, v->u.reg.num);
        }
    }
}


/* Expire values that are no longer useful */
void MVM_jit_expire_values(MVMThreadContext *tc, MVMJitCompiler *compiler) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    MVMint32 order_nr = compiler->order_nr;
    MVMint32 i = 0;
    while (i < alc->active_num) {
        MVMJitExprValue *value = alc->active[i];
        if (value->last_use <= order_nr &&
            /* can't expire values held in locked registers */
            !(value->state == MVM_JIT_VALUE_ALLOCATED && REGISTER_IS_LOCKED(alc, value->u.reg.num))) {
            MVM_jit_register_expire(tc, compiler, value);
        } else {
            i++;
        }
    }
}

