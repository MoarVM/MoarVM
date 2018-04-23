#include "moar.h"
#include "shiftjis_codeindex.h"
MVMint16 shift_jis_index_to_cp_array_offset (MVMThreadContext *tc, MVMint16 index) {
    MVMuint16 offset = 0;
    int i = 0;
    if (index < 0 || SHIFTJIS_MAX_INDEX < index) return SHIFTJIS_NULL;
    for (; i < SHIFTJIS_OFFSET_VALUES_ELEMS && shiftjis_offset_values[i].location < index; i++) {
        if (index <= shiftjis_offset_values[i].location + shiftjis_offset_values[i].offset) {
            return SHIFTJIS_NULL;
        }
        offset += shiftjis_offset_values[i].offset;
    }
    return index - offset;
}
MVMGrapheme32 shift_jis_index_to_cp (MVMThreadContext *tc, MVMint16 index) {
    MVMint16 offset = shift_jis_index_to_cp_array_offset(tc, index);
    return offset == SHIFTJIS_NULL ? SHIFTJIS_NULL : shiftjis_index_to_cp_codepoints[offset];
}