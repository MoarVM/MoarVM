/* An as-small-as-possible dynamic array implementation. */
#define MVM_VECTOR_DECL(type, x) type *x; \
    size_t x ## _num; \
    size_t x ## _alloc


#define MVM_VECTOR_INIT(x, size) do { \
        size_t _s = (size); \
        x = (_s > 0) ? MVM_calloc(_s, sizeof(*x)) : NULL; \
        x ## _num = 0; \
        x ## _alloc = _s; \
    } while (0)

#define MVM_VECTOR_CLEAR(x) do { \
        x ## _num = 0; \
    } while (0)

#define MVM_VECTOR_DESTROY(x) do { \
        MVM_free(x); \
        x = NULL; \
        x ## _num = 0; \
        x ## _alloc = 0; \
    } while (0)

#define MVM_VECTOR_ELEMS(x) \
    (x ## _num)

#define MVM_VECTOR_SIZE(x) \
    (sizeof(*x) * (x ## _alloc))

#define MVM_VECTOR_TOP(x) \
    ((x) + (x ## _num))

#define MVM_VECTOR_ELEMS(x) \
    (x ## _num)

#define MVM_VECTOR_GROW(x, size) do {\
        size_t _s = (size); \
        x = MVM_realloc(x, _s*sizeof(*x));   \
        memset(x + (x ## _alloc), 0, (_s - (x ## _alloc)) * sizeof(*x)); \
        x ## _alloc = _s; \
    } while (0)


#define MVM_VECTOR_ENSURE_SIZE(x, size) do {\
        size_t _s = (size); \
        if (_s >= (x ## _alloc)) {    \
            size_t newsize = (x ## _alloc) * 2 + 2; \
            while (_s >= newsize) newsize *= 2; \
            MVM_VECTOR_GROW(x, newsize); \
        } \
    } while (0)

#define MVM_VECTOR_ENSURE_SPACE(x, space) \
    MVM_VECTOR_ENSURE_SIZE(x, (x ## _num) + (space))

#define MVM_VECTOR_PUSH(x, value) do { \
        MVM_VECTOR_ENSURE_SPACE(x, 1); \
        x[x ## _num++] = (value); \
    } while(0)

#define MVM_VECTOR_POP(x) \
    (x)[--(x ## _num)]


#define MVM_VECTOR_APPEND(x, ar, len) do { \
        size_t _l = (len); \
        MVM_VECTOR_ENSURE_SPACE(x, _l); \
        memcpy(MVM_VECTOR_TOP(x), ar, _l * sizeof(x[0])); \
        x ## _num += _l; \
    } while(0)

#define MVM_VECTOR_SPLICE(x, ofs, len, out) do { \
        size_t _l = (len), _o = (ofs); \
        void * buf = (out); \
        if (buf != NULL) { memcpy(buf, (x) + _o, _l * sizeof(x[0])); } \
        memmove((x) + _o, (x) + _o + _l, ((x ## _num) - _l - _o) * sizeof(x[0])); \
        x ## _num -= _l; \
    } while (0)

#define MVM_VECTOR_ASSIGN(a, b) do { \
        a = b; \
        a ## _alloc = b ## _alloc; \
        a ## _num = b ## _num; \
    } while (0)
