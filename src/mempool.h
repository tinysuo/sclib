
/*
 * Copyright (C) suo cunliang
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _MEMPOOL_H_INCLUDED_
#define _MEMPOOL_H_INCLUDED_

typedef struct {}  mempool_t;

mempool_t *mempool_create(size_t size);
void mempool_destroy(mempool_t *pool);
void mempool_reset(mempool_t *pool);

void *mempool_alloc(mempool_t *pool, size_t size);
void *mempool_nalloc(mempool_t *pool, size_t size);
void *mempool_calloc(mempool_t *pool, size_t size);
void *mempool_memalign(mempool_t *pool, size_t size, size_t alignment);
void  mempool_free(mempool_t *pool, void *p);


#endif /* _MEMPOOL_H_INCLUDED_ */
