/**
 * @file tinymt64.h
 *
 * @brief Tiny Mersenne Twister only 127 bit internal state
 *
 * @author Mutsuo Saito (Hiroshima University)
 * @author Makoto Matsumoto (The University of Tokyo)
 *
 * Copyright (C) 2011 Mutsuo Saito, Makoto Matsumoto,
 * Hiroshima University and The University of Tokyo.
 * All rights reserved.
 *
 * The 3-clause BSD License is applied to this software, see
 * LICENSE.txt
 *
 * @file tinymt64.c
 *
 * @brief 64-bit Tiny Mersenne Twister only 127 bit internal state
 *
 * @author Mutsuo Saito (Hiroshima University)
 * @author Makoto Matsumoto (The University of Tokyo)
 *
 * Copyright (C) 2011 Mutsuo Saito, Makoto Matsumoto,
 * Hiroshima University and The University of Tokyo.
 * All rights reserved.
 *
 * The 3-clause BSD License is applied to this software, see
 * LICENSE.txt
 */


#include <platform/inttypes.h>
#include "tinymt64.h"

#define TINYMT64_SH0 12
#define TINYMT64_SH1 11
#define TINYMT64_SH8 8
#define TINYMT64_MASK UINT64_C(0x7fffffffffffffff)
#define TINYMT64_MUL (1.0 / 9007199254740992.0)

/*
 * tinymt64 default parameters
 */
#ifndef TINYMT64_MAT1
#  define TINYMT64_MAT1 0x7a840f50
#endif
#ifndef TINYMT64_MAT2
#  define TINYMT64_MAT2 0xf3d8fcf6
#endif
#ifndef TINYMT64_TMAT
#  define TINYMT64_TMAT 0x9746beffffbffffe
#endif

/*
 * Initialization loop
 */
#define MIN_LOOP 8

static const uint32_t mat1 = TINYMT64_MAT1;
static const uint32_t mat2 = TINYMT64_MAT2;
static const uint64_t tmat = TINYMT64_TMAT;

/**
 * This function changes internal state of tinymt64.
 * Users should not call this function directly.
 * @param random tinymt internal status
 */
static void tinymt64_next_state(uint64_t * random) {
    uint64_t x;

    random[0] &= TINYMT64_MASK;
    x = random[0] ^ random[1];
    x ^= x << TINYMT64_SH0;
    x ^= x >> 32;
    x ^= x << 32;
    x ^= x << TINYMT64_SH1;
    random[0] = random[1];
    random[1] = x;
    random[0] ^= -((int64_t)(x & 1)) & mat1;
    random[1] ^= -((int64_t)(x & 1)) & (((uint64_t)mat2) << 32);
}

/**
 * This function outputs 64-bit unsigned integer from internal state.
 * Users should not call this function directly.
 * @param random tinymt internal status
 * @return 64-bit unsigned pseudorandom number
 */
static uint64_t uint64_temper(uint64_t * random) {
    uint64_t x;
    x = random[0] + random[1];
    x ^= random[0] >> TINYMT64_SH8;
    x ^= -((int64_t)(x & 1)) & tmat;
    return x;
}

/**
 * This function outputs 64-bit unsigned integer from internal state.
 * @param random tinymt internal status
 * @return 64-bit unsigned integer r (0 <= r < 2^64)
 */
uint64_t tinymt64_generate_uint64(uint64_t * random) {
    tinymt64_next_state(random);
    return uint64_temper(random);
}

/**
 * This function outputs floating point number from internal state.
 * This function is implemented using multiplying by 1 / 2^64.
 * @param random tinymt internal status
 * @return floating point number r (0.0 <= r < 1.0)
 */
double tinymt64_generate_double(uint64_t * random) {
    tinymt64_next_state(random);
    return (uint64_temper(random) >> 11) * TINYMT64_MUL;
}

/**
 * This function initializes the internal state array with a 64-bit
 * unsigned integer seed.
 * @param random tinymt state vector.
 * @param seed a 64-bit unsigned integer used as a seed.
 */
void tinymt64_init(uint64_t * random, uint64_t seed) {
    int i;
    random[0] = seed ^ ((uint64_t)mat1 << 32);
    random[1] = mat2 ^ tmat;
    for (i = 1; i < MIN_LOOP; i++) {
        random[i & 1] ^= i + UINT64_C(6364136223846793005)
            * (random[(i - 1) & 1]
               ^ (random[(i - 1) & 1] >> 62));
    }
}
