#include <moar.h>

MVMString * MVM_string_wide_decode(MVMThreadContext *tc, const MVMwchar *wstr, size_t size) {
    MVMString *str;
    char      *cstr;
    mbstate_t  mbstate;

#ifdef _MSC_VER
    str = MVM_string_utf16_decode(tc, tc->instance->VMString, (char *)wstr, size);
#else
    mbstate = (mbstate_t){ 0 };
    size    = wcsrtombs(NULL, &wstr, 0, &mbstate);
    if (size == (size_t)-1)
        MVM_exception_throw_adhoc(tc, "Internal error: failed to decode wide string with error '%s'", strerror(errno));
    cstr = MVM_calloc(size + 1, sizeof(char));
    (void)wcsrtombs(cstr, &wstr, size, &mbstate);
    str = MVM_string_utf8_decode(tc, tc->instance->VMString, cstr, size);
#endif

    return str;
}

MVMwchar * MVM_string_wide_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size) {
          MVMwchar  *wstr;
    const char      *cstr;
          size_t     size;
          mbstate_t  mbstate;

#ifdef _MSC_VER
    wstr = (MVMwchar *)MVM_string_utf16_encode_substr(tc, str, output_size, 0, -1, NULL, 0);
#else
    mbstate = (mbstate_t){ 0 };
    cstr    = MVM_string_utf8_encode_C_string(tc, str);
    size    = mbsrtowcs(NULL, &cstr, 0, &mbstate);
    if (size == (size_t)-1)
        MVM_exception_throw_adhoc(tc, "Internal error: failed to encode wide string with error '%s'", strerror(errno));

    wstr = MVM_calloc(size + 1, sizeof(MVMwchar));
    (void)mbsrtowcs(wstr, &cstr, size, &mbstate);
    if (output_size != NULL)
        *output_size = (MVMuint64)(size + 1);
#endif

    return wstr;
}
