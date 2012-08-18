#include "moarvm.h"

/* Looks up address of some codepoint information. */
MVMCodePoint * MVM_unicode_codepoint_info(MVMThreadContext *tc, MVMint32 codepoint) {
    MVMint32 plane = codepoint >> 16;
    MVMint32 idx   = codepoint & 0xFFFF;
    if (plane < MVM_UNICODE_PLANES)
        if (idx < MVM_unicode_planes[plane].num_codepoints)
            return &MVM_unicode_codepoints[
                MVM_unicode_planes[plane].first_codepoint + idx];
    return NULL;
}

typedef struct _MVMUnicodeNameHashEntry {
    char *name;
    MVMint32 codepoint;
    UT_hash_handle hh;
} MVMUnicodeNameHashEntry;

/* Lazily constructed hashtable of Unicode names to codepoints.
    Okay not threadsafe since its value is deterministic. */
static MVMUnicodeNameHashEntry *codepoints_by_name = NULL;

/* Looks up a codepoint by name. Lazily constructs a hash. */
MVMint32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    char *cname = MVM_string_ascii_encode(tc, name, &size);
    MVMUnicodeNameHashEntry *result;
    if (!codepoints_by_name) {
        
    }
    HASH_FIND_STR(codepoints_by_name, cname, result);
    free(cname);
    return result ? result->codepoint : -1;
}

MVMint64 MVM_unicode_has_property_value(MVMThreadContext *tc, MVMint32 codepoint, MVMint64 property_code, MVMint64 property_value_code) {
    return (MVMint64)MVM_unicode_get_property_value(tc, codepoint, property_code) == property_value_code ? 1 : 0;
}

/*

        MVMuint32 plane;
        MVMint32 idx = 0;
        for (plane = 0; plane < MVM_UNICODE_PLANES; plane++) {
            MVMuint32 codepoint = MVM_unicode_planes[plane].first_codepoint;
            for (; codepoint < MVM_unicode_planes[plane].first_codepoint + MVM_unicode_planes[plane].num_codepoints; codepoint++) {
                MVMUnicodeNameHashEntry *entry = malloc(sizeof(MVMUnicodeNameHashEntry));
                MVMCodePoint codepoint_struct = MVM_unicode_codepoints[idx++];
                char *name = codepoint_struct.name;
                if (name) {
                    entry->name = name;
                    entry->codepoint = codepoint;
                    HASH_ADD_KEYPTR(hh, codepoints_by_name, name, strlen(name), entry);
                }
            }
        }
        */
