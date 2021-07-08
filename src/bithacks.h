static MVMuint32 MVM_bithacks_count_bits(MVMuint64 value) {
    MVMuint32 count;

    for (count = 0; value; count++)
        value &= value - 1;

    return count;
}

static int MVM_bithacks_is_pow2z(MVMuint64 value)
{
    return (value & (value - 1)) == 0;
}

static MVMuint64 MVM_bithacks_next_greater_pow2(MVMuint64 value)
{
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return value + 1;
}
