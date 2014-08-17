#include <unistd.h>
#include <stdlib.h>

#include "cstalloc.h"

#define _CST_POOL_COUNT		128 // 128 * 8 (x64) = 1024 = 1kb max struct size
#define _CST_ALIGN_SIZE		sizeof(void*)
#define _CST_PTR_SIZE		sizeof(void**)

typedef struct {
	void   *start;
	void   *head;
	size_t  size;
	size_t  count;
} cst_pool_t;

cst_pool_t cst_pools[_CST_POOL_COUNT];

inline void *
_cst_format_pool(void *start, void *end, size_t step) {
	void *ptr, *next;

	ptr  = start;
	*(void**) ptr = NULL;
	next = ptr + step;
	while (next < (end - step)) {
		*(void**) next = ptr;
		ptr   = next;
		next += step;
	}

	return ptr;
}

inline void
_cst_create_pool(cst_pool_t *pool, size_t size) {
	pool->size = getpagesize();
	pool->start = malloc(pool->size);

	pool->head = _cst_format_pool(pool->start, pool->start + pool->size, size + _CST_PTR_SIZE);
}

inline void
_cst_realloc_pool(cst_pool_t *pool, size_t size) {
	void *tmp;
	pool->start = realloc(pool->start, pool->size + getpagesize());

	tmp = pool->start + pool->size - (pool->size % (_CST_PTR_SIZE + size));
	pool->size += getpagesize();
	pool->head = _cst_format_pool(tmp, pool->start + pool->size, size + _CST_PTR_SIZE);
}

void *
cstalloc(size_t size) {
	void *ptr;

	if (size % _CST_ALIGN_SIZE)
		size = size - (size % _CST_ALIGN_SIZE) + _CST_ALIGN_SIZE;

	cst_pool_t *pool = &cst_pools[size / _CST_ALIGN_SIZE - 1];
	if (!pool->start)
		_cst_create_pool(pool, size);

	if (!pool->head)
		_cst_realloc_pool(pool, size);

	ptr = pool->head;
	pool->head = *(void**) ptr;

	*(cst_pool_t **) ptr = pool;
	ptr += _CST_PTR_SIZE;
	return ptr;
}

void
cstfree(void *ptr) {
	cst_pool_t *pool;

	ptr -= _CST_PTR_SIZE;
	pool = *(cst_pool_t **) ptr;

	*(void**) ptr = pool->head;
	pool->head = ptr;
}
