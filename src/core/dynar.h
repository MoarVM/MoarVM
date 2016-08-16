/* An as-small-as-possible dynamic array implementation. */
#define MVM_DYNAR_DECL(type, x) type *x; \
    size_t x ## _num; \
    size_t x ## _alloc;

#define MVM_DYNAR_INIT(x, size) do { \
        size_t _s = (size); \
        x = (_s > 0) ? MVM_calloc(_s, sizeof(*x)) : NULL; \
        x ## _num = 0; \
        x ## _alloc = _s; \
    } while (0)

#define MVM_DYNAR_GROW(x, size) do {\
        size_t _s = (size); \
        x = MVM_realloc(x, _s*sizeof(*x));   \
        memset(x + (x ## _alloc), 0, (_s - (x ## _alloc)) * sizeof(*x)); \
        x ## _alloc = _s; \
    } while (0)


#define MVM_DYNAR_ENSURE_SIZE(x, size) do {\
        size_t _s = (size); \
        if (_s >= (x ## _alloc)) {    \
            size_t newsize = (x ## _alloc) * 2 + 2; \
            while (_s >= newsize) newsize *= 2; \
            MVM_DYNAR_GROW(x, newsize); \
        } \
    } while (0)

#define MVM_DYNAR_ENSURE_SPACE(x, space) \
    MVM_DYNAR_ENSURE_SIZE(x, (x ## _num) + (space))

#define MVM_DYNAR_PUSH(x, value) do { \
        MVM_DYNAR_ENSURE_SPACE(x, 1); \
        x[x ## _num++] = (value); \
    } while(0)

#define MVM_DYNAR_POP(x) \
    (x)[--(x ## _num)]

#define MVM_DYNAR_APPEND(x, ar, len) do { \
        size_t _l = (len); \
        MVM_DYNAR_ENSURE_SPACE(x, _l); \
        memcpy(x + x ## _num, ar, _l * sizeof(x[0])); \
        x ## _num += _l; \
    } while(0)
