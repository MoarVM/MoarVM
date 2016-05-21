#include "moar.h"
#include "internal.h"

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
#define LOCK_REGISTER(a, n)   ((a)->reg_lock |=  (1 << (n)))
#define UNLOCK_REGISTER(a,n)  ((a)->reg_lock &= ~(1 << (n)))


/* NB only implement GPR register allocation now */
void MVM_jit_register_allocator_init(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                     MVMJitRegisterAllocator *alc) {
    /* Store live ranges */
    MVM_DYNAR_INIT(alc->active, NUM_GPR);
    /* Initialize free register buffer */
    alc->free_reg = MVM_malloc(sizeof(free_gpr));
    memcpy(alc->free_reg, free_gpr, NUM_GPR);

    alc->reg_use   = MVM_calloc(16, sizeof(MVMint8));

    alc->reg_give  = 0;
    alc->reg_take  = 0;

    alc->spill_top = 1;
    alc->reg_lock  = 0;
    compiler->allocator = alc;
}

void MVM_jit_register_allocator_deinit(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                       MVMJitRegisterAllocator *alc) {
    MVM_free(alc->active);
    MVM_free(alc->free_reg);
    MVM_free(alc->reg_use);
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
        alc->free_reg[alc->reg_take] = 0xff; /* mark it for debugging purposes */
        alc->reg_take = NEXT_REG(alc->reg_take);
    }
    /* MVM_jit_log(tc, "Allocated register %d at order nr %d\n", reg_num, cl->order_nr); */
    return reg_num;
}


/* Freeing a register makes it available again */
void MVM_jit_register_free(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    if (reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    } else {
        MVMint32 i;
        /* MVM_jit_log(tc, "Trying to free register %d\n", reg_num); */

        for (i = 0; i < sizeof(free_gpr); i++) {
            if (free_gpr[i] == reg_num)
                goto ok;
        }
        MVM_oops(tc, "This is not a free register!");
    ok:
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
    i = alc->reg_take;
    do {
        if (alc->free_reg[i] == reg_num) {
            /* swap with next take register, which is overwritten anyway */
            alc->free_reg[i] = alc->free_reg[alc->reg_take];
            alc->free_reg[alc->reg_take] = 0xff;
            alc->reg_take    = NEXT_REG(alc->reg_take);
            /* MVM_jit_log(tc, "Taken register %d on order nr %d\n", reg_num, compiler->order_nr); */
            return;
        }
        i = NEXT_REG(i);
    } while (i != alc->reg_give);
    MVM_oops(tc, "Could not take register even after spilling");
}


/* Use marks a register currently in use */
void MVM_jit_register_use(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    if (reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    } else {
        /* MVM_jit_log(tc, "Locking register %d in order nr %d\n", reg_num, compiler->order_nr); */
        LOCK_REGISTER(compiler->allocator, reg_num);
    }

}

void MVM_jit_register_release(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls, MVMint8 reg_num) {
    if (reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    } else {
        /* MVM_jit_log(tc, "Unlocking register %d in order nr %d\n", reg_num, compiler->order_nr); */
        UNLOCK_REGISTER(compiler->allocator, reg_num);
    }
}


void spill_value(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprValue *value) {
    NYI(spill_value);
}

#define VALUE_IS_ASSIGNED(v, rc, rn) ((v)->state == MVM_JIT_VALUE_ALLOCATED && (v)->reg_cls == (rc) && (v)->reg_num == (rn))

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
        for (i = 1; i < alc->spill_top; i++) {
            if (spill_bmp[i] == 0) {
                spill_location = i;
                break;
            }
        }
        if (spill_location == 0) {
            /* Bitmap was full */
            spill_location = alc->spill_top++;
        }
        /* MVM_jit_log(tc, "Emit spill of register %d to location %d\n", reg_num, spill_location); */
        MVM_jit_emit_spill(tc, compiler, spill_location, reg_cls, reg_num, MVM_JIT_REG_SZ);
    } /* if it was spilled before, it's immutable now */
    MVM_free(spill_bmp);
    /* Mark nodes as spilled on the location */
    /* MVM_jit_log(tc, "Going to mark %d values as spilled\n", alc->reg_use[reg_num]); */
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
    } else {
        /*MVM_jit_log(tc, "All values were spilled\n"); */
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

            value->reg_cls    = reg_cls;
            value->reg_num    = reg_num;
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
    value->reg_num    = reg_num;
    value->reg_cls    = reg_cls;
    value->last_created = cl->order_nr;

    MVM_DYNAR_PUSH(alc->active, value);
    alc->reg_use[reg_num]++;
}


/* Expiring a value marks it dead and possibly releases its register */
void MVM_jit_register_expire(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprValue *value) {
    MVMJitRegisterAllocator *alc = compiler->allocator;
    MVMint8 reg_num = value->reg_num;
    MVMint32 i;
    if (value->reg_cls == MVM_JIT_REGCLS_NUM) {
        NYI(numeric_regs);
    }
    /* Remove value from active */
    i = 0;
    while (i < alc->active_num) {
        if (alc->active[i] == value) {
            /* splice it out */
            alc->active_num--;
            alc->active[i] = alc->active[alc->active_num];
        } else {
            i++;
        }
    }

    if (value->state == MVM_JIT_VALUE_ALLOCATED) {
        /* Decrease register number count and free if possible */
        alc->reg_use[reg_num]--;
        if (alc->reg_use[reg_num] == 0) {
            MVM_jit_register_free(tc, compiler, value->reg_cls, reg_num);
        }
    }
    /* Mark value as dead */
    value->state = MVM_JIT_VALUE_DEAD;
}

/* Put a value into a specific register */
void MVM_jit_register_put(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprValue *value, MVMint32 reg_cls, MVMint8 reg_num) {
    MVMJitRegisterAllocator *alc = cl->allocator;
    MVMint32 i;
    if (VALUE_IS_ASSIGNED(value, reg_cls, reg_num)) {
        /* happy case */
        return;
    }
    /* Take the register */
    MVM_jit_register_take(tc, cl, reg_cls, reg_num);
    if (value->state == MVM_JIT_VALUE_ALLOCATED) {
        MVMint32 cur_reg_cls = value->reg_cls, cur_reg_num = value->reg_num;
        MVM_jit_emit_copy(tc, cl, reg_cls, reg_num, value->reg_cls, value->reg_num);
        for (i = 0; i < alc->active_num; i++) {
            /* update active values to new register */
            MVMJitExprValue *active = alc->active[i];
            if (VALUE_IS_ASSIGNED(active, cur_reg_cls, cur_reg_num)) {
                /* Assign to new register */
                alc->reg_use[cur_reg_num]--;
                active->reg_cls = reg_cls;
                active->reg_num = reg_num;
                alc->reg_use[reg_num]++;
            }
        }
        if (alc->reg_use[cur_reg_num] == 0) {
            MVM_jit_register_free(tc, cl, cur_reg_cls, cur_reg_num);
        } else {
            MVM_oops(tc, "After copy, register is still not free");
        }
        /* An allocated value should have been live, so should have been updated... */
        if (value->reg_cls != reg_cls || value->reg_num != reg_num) {
            /* MVM_jit_log(tc, "Allocated value %x at register %d wasn't actually in active (active_num=%d)\n", value, value->reg_num, alc->active_num); */
            MVM_jit_register_assign(tc, cl, value, reg_cls, reg_num);
        }
    }
    else if (value->state == MVM_JIT_VALUE_SPILLED) {
        /* Spilled values ought to be life values, which means load should */
        MVM_jit_register_load(tc, cl, value->spill_location, reg_cls, reg_num, MVM_JIT_REG_SZ);
    } else if (value->state == MVM_JIT_VALUE_IMMORTAL) {
        /* Immortal values are always present */
        MVM_jit_emit_copy(tc, cl, reg_cls, reg_num, value->reg_cls, value->reg_num);
        /* However, they are not assigned to a register, so we assign it */
        MVM_jit_register_assign(tc, cl, value, reg_cls, reg_num);
    } else {
        MVM_oops(tc, "Tried to put a non-live value into a register");
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
            MVM_jit_register_spill(tc, cl, v->reg_cls, v->reg_num);
        }
    }
}

void MVM_jit_spill_before_conditional(MVMThreadContext *tc, MVMJitCompiler *cl, MVMJitExprTree *tree, MVMint32 node) {
    MVMJitRegisterAllocator *alc = cl->allocator;
    MVMint32 exit_order_nr = tree->info[node].value.first_created;
    MVMint32 i;
    for (i = 0; i < alc->active_num; i++) {
        MVMJitExprValue *value = alc->active[i];
        if (value->last_use > exit_order_nr) {
            /* Emit a spill - this inadvertently frees the register */
            MVM_jit_register_spill(tc, cl, value->reg_cls, value->reg_num);
            /* So we have to take it again */
            MVM_jit_register_take(tc, cl, value->reg_cls, value->reg_num);
            /* Because the value has been assigned before, we should
             * not reassign it, but update it's state directly */
            value->state = MVM_JIT_VALUE_ALLOCATED;
            alc->reg_use[value->reg_num]++;
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
