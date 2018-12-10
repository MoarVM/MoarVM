/* basic bitmap implementation */
typedef MVMuint64 MVMBitmap;

/* Efficient find-first-set; on x86, using `bsf` primitive operation; something
 * else on other architectures. */
#ifdef __GNUC__
/* also works for clang and friends */
#define MVM_FFS(x) __builtin_ffsll(x)
#elif defined(_MSC_VER)
MVM_STATIC_INLINE MVMuint32 MVM_FFS(MVMBitmap x) {
    MVMuint32 i = 0;
    if (_BitScanForward64(&i, x) == 0)
        return 0;
    return i + 1;
}
#else
/* fallback, note that i=0 if no bits are set */
MVM_STATIC_INLINE MVMuint32 MVM_FFS(MVMBitmap x) {
    MVMuint32 i = 0;
    while (x) {
        if (x & (1 << i++))
            break;
    }
    return i;
}
#endif


/* NB - make this a separate 'library', use it for register bitmap */
/* Witness the elegance of the bitmap for our purposes. */
MVM_STATIC_INLINE void MVM_bitmap_set(MVMBitmap *bits, MVMint32 idx) {
    bits[idx >> 6] |= (UINT64_C(1) << (idx & 0x3f));
}

MVM_STATIC_INLINE void MVM_bitmap_set_low(MVMBitmap *bits, MVMint32 idx) {
    *bits |= (UINT64_C(1) << (idx & 0x3f));
}

MVM_STATIC_INLINE MVMuint64 MVM_bitmap_get(MVMBitmap *bits, MVMint32 idx) {
    return bits[idx >> 6] & (UINT64_C(1) << (idx & 0x3f));
}

MVM_STATIC_INLINE MVMuint64 MVM_bitmap_get_low(MVMBitmap bits, MVMint32 idx ) {
    return bits & (UINT64_C(1) << (idx & 0x3f));
}

MVM_STATIC_INLINE void MVM_bitmap_delete(MVMBitmap *bits, MVMint32 idx) {
    bits[idx >> 6] &= ~(UINT64_C(1) << (idx & 0x3f));
}

MVM_STATIC_INLINE void MVM_bitmap_union(MVMBitmap *out, MVMBitmap *a, MVMBitmap *b, MVMint32 n) {
    MVMint32 i;
    for (i = 0; i < n; i++) {
        out[i] = a[i] | b[i];
    }
}

MVM_STATIC_INLINE void MVM_bitmap_difference(MVMBitmap *out, MVMBitmap *a, MVMBitmap *b, MVMint32 n) {
    MVMint32 i;
    for (i = 0; i < n; i++) {
        out[i] = a[i] ^ b[i];
    }
}

MVM_STATIC_INLINE void MVM_bitmap_intersection(MVMBitmap *out, MVMBitmap *a, MVMBitmap *b, MVMint32 n) {
    MVMint32 i;
    for (i = 0; i < n; i++) {
        out[i] = a[i] & b[i];
    }
}
