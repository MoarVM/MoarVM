#include <stdint.h>  // for uint32_t, to get 32bit-wide rotates, regardless of the size of int.

MVM_STATIC_INLINE uint32_t rotl32 (uint32_t value, unsigned int shift) {
    return ((shift &= 31) == 0) ? value : (value << shift) | (value >> (32 - shift));
    /*const unsigned int mask = CHAR_BIT*sizeof(value) - 1;
    count &= mask;
    return (value << count) | (value >> (-count & mask));*/
}
MVMint64 MVM_proc_rand_i(MVMThreadContext *tc);
static void printBits(size_t const size, void const * const ptr)
{
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j;

    for (i=size-1;i>=0;i--)
    {
        for (j=7;j>=0;j--)
        {
            byte = (b[i] >> j) & 1;
            printf("%u", byte);
        }
    }
    puts("");
}
static uint64_t extract_bits (uint64_t data, unsigned startBit) {
    uint64_t mask;
    #define MVM_EXTRACT_NUM_BITS 5
    mask = ((1 << MVM_EXTRACT_NUM_BITS) - 1) << startBit;
    uint64_t isolatedXbits = data & mask;
    return isolatedXbits >> startBit;
}
static uint64_t get_0_to_31 (MVMThreadContext *tc) {
    MVMHashBucketRand *t = &(tc->hashbucket_rand);
    if ((sizeof(uint64_t) * 8) <= t->location + MVM_EXTRACT_NUM_BITS) {
        //printf("here\n");
        t->data = MVM_proc_rand_i(tc);
        //getrandom(&(t->data), sizeof(uint64_t), 0);
        t->location = 0;
    }
    fprintf(stderr, "location %i..%i. size %li\n", t->location, t->location + MVM_EXTRACT_NUM_BITS, sizeof(uint64_t) * 8);

    uint64_t blah = extract_bits(t->data, t->location);
    t->location += MVM_EXTRACT_NUM_BITS;
    fprintf(stderr, "returning %lu\n", blah);
    return blah;
}
