#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "cstalloc.h"

#define _CST_POOL_MOD		64
#define _CST_POOL_COUNT		128 // 128 * 8 (x64) = 1024 = 1kb max struct size
#define _CST_ALIGN_SIZE		sizeof(void*)
#define _CST_PTR_SIZE		sizeof(void**)
#define _CST_INC_SIZE		(getpagesize())

typedef struct {
	void   *block_head;
	void   *empty_head;
	size_t  size;
	size_t	count;
} cst_pool_t;

cst_pool_t cst_pools[_CST_POOL_COUNT];

inline void *
_cst_format_pool(void *start, void *end, size_t step) {
	void *ptr, *next;

	ptr  = start;
	*(void**) ptr = NULL;
	next = ptr + step;
	while (next <= (end - step)) {
		*(void**) next = ptr;
		ptr   = next;
		next += step;
	}

	return ptr;
}

inline void
_cst_alloc_pool(cst_pool_t *pool, size_t size) {
	void *ptr = pool->block_head;
	pool->size = _CST_INC_SIZE + _CST_INC_SIZE * (size / _CST_POOL_MOD);
	pool->block_head = malloc(pool->size);
	*(void **) pool->block_head = ptr;
	++pool->count;

	pool->empty_head = _cst_format_pool(pool->block_head + _CST_PTR_SIZE, pool->block_head + pool->size, size + _CST_PTR_SIZE);
}

void *
cstalloc(size_t size) {
	void *ptr;

	if (size % _CST_ALIGN_SIZE)
		size = size - (size % _CST_ALIGN_SIZE) + _CST_ALIGN_SIZE;

	cst_pool_t *pool = &cst_pools[size / _CST_ALIGN_SIZE - 1];

	if (!pool->empty_head)
		_cst_alloc_pool(pool, size);

	ptr = pool->empty_head;
	pool->empty_head = *(void**) ptr;

	*(cst_pool_t **) ptr = pool;
	ptr += _CST_PTR_SIZE;
	return ptr;
}

void
cstfree(void *ptr) {
	cst_pool_t *pool;

	ptr -= _CST_PTR_SIZE;
	pool = *(cst_pool_t **) ptr;

	*(void**) ptr = pool->empty_head;
	pool->empty_head = ptr;
}

void
cstgc(void) {
}
