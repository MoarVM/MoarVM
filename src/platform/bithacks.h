static MVMuint32 count_bits(MVMuint64 value) {
    MVMuint32 count;

    for (count = 0; value; count++)
        value &= value - 1;

    return count;
}
