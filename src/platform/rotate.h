#include <stdint.h>  // for uint32_t, to get 32bit-wide rotates, regardless of the size of int.

MVM_STATIC_INLINE uint32_t rotl32 (uint32_t value, unsigned int shift) {
    return ((shift &= 31) == 0) ? value : (value << shift) | (value >> (32 - shift));
    /*const unsigned int mask = CHAR_BIT*sizeof(value) - 1;
    count &= mask;
    return (value << count) | (value >> (-count & mask));*/
}
