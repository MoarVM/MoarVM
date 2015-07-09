/* An as-small-as-possible dynamic array implementation. */
#define MVM_DYNAR_DECL(type, x) type *x; \
    size_t x ## _num; \
    size_t x ## _alloc;

#define MVM_DYNAR_INIT(x, size) do { \
        x = MVM_malloc((size)*sizeof(*x));    \
        x ## _num = 0; \
        x ## _alloc = (size); \
    } while (0)

#define MVM_DYNAR_GROW(x, size) do {\
        x = MVM_realloc(x, (size)*sizeof(*x));   \
        x ## _alloc = (size); \
    } while (0)

#define MVM_DYNAR_ENSURE(x, space) \
    if (((x ## _num) + (space)) >= (x ## _alloc)) {     \
        MVM_DYNAR_GROW(x, (x ## _alloc)*2); \
    }

#define MVM_DYNAR_PUSH(x, value) do { \
        MVM_DYNAR_ENSURE(x, 1); \
        x[x ## _num++] = (value); \
    } while(0)

#define MVM_DYNAR_APPEND(x, ar, len) do { \
        MVM_DYNAR_ENSURE(x, len); \
        memcpy(x + x ## _num, ar, (len) * sizeof(x[0]));        \
        x ## _num += len; \
    } while(0)

