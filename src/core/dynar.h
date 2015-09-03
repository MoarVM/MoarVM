/* An as-small-as-possible dynamic array implementation. */
#define MVM_DYNAR_DECL(type, x) type *x; \
    size_t x ## _num; \
    size_t x ## _alloc;

#define MVM_DYNAR_INIT(x, size) do { \
        x = MVM_calloc((size), sizeof(*x));      \
        x ## _num = 0; \
        x ## _alloc = (size); \
    } while (0)

#define MVM_DYNAR_GROW(x, size) do {\
        x = MVM_realloc(x, (size)*sizeof(*x));   \
        memset(x + (x ## _alloc), 0, ((size) - (x ## _alloc)) * sizeof(*x)); \
        x ## _alloc = (size); \
    } while (0)


#define MVM_DYNAR_ENSURE_SIZE(x, size) \
    if ((size) >= (x ## _alloc)) {     \
        size_t newsize = (x ## _alloc) * 2 + 1; \
        while ((size) >= newsize) newsize *= 2;  \
        MVM_DYNAR_GROW(x, newsize); \
    }

#define MVM_DYNAR_ENSURE_SPACE(x, space) \
    MVM_DYNAR_ENSURE_SIZE(x, (x ## _num) + (space))

#define MVM_DYNAR_PUSH(x, value) do { \
        MVM_DYNAR_ENSURE_SPACE(x, 1); \
        x[x ## _num++] = (value); \
    } while(0)

#define MVM_DYNAR_POP(x) \
    (x)[--(x ## _num)]


#define MVM_DYNAR_APPEND(x, ar, len) do { \
        MVM_DYNAR_ENSURE_SPACE(x, len); \
        memcpy(x + x ## _num, ar, (len) * sizeof(x[0]));        \
        x ## _num += len; \
    } while(0)

