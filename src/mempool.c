
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define os_malloc   malloc
#define os_alloc    alloc
#define os_free     free


#define mempool_pagesize 4096

#define mem_align(d, a)         (((d) + (a - 1)) & ~(a - 1))
#define mem_align_ptr(p, a)     (unsigned char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))

#define MEM_MAX_ALLOC_FROM_POOL  (mempool_pagesize - 1)
#define MEM_DEFAULT_POOL_SIZE    (16 * 1024)

#define MEM_POOL_ALIGNMENT       16
#define MEM_MIN_POOL_SIZE        mem_align((sizeof(mempool_t) + 2 * sizeof(mempool_large_t)),MEM_POOL_ALIGNMENT)

typedef struct mempool_large_s  mempool_large_t;
typedef struct mempool_s            mempool_t;

struct mempool_large_s {
    mempool_large_t     *next;
    void                *alloc;
};


typedef struct {
    unsigned char        *last;
    unsigned char        *end;
    mempool_t            *next;
    unsigned int         failed;
} mempool_data_t;

struct mempool_s {
    mempool_data_t      d;
    size_t              max;
    mempool_t           *current;
    mempool_large_t     *large;
    size_t              pool_size;
    size_t              used_size;
};

static inline void *_mempool_alloc_small(mempool_t *pool, size_t size,int align);
static void *_mempool_alloc_block(mempool_t *pool, size_t size);
static void *_mempool_alloc_large(mempool_t *pool, size_t size);
static void * _os_memalign(size_t alignment, size_t size);

mempool_t * mempool_create(size_t size)
{
    mempool_t  *p;
    p = _os_memalign(MEM_POOL_ALIGNMENT, size);
    if (p == NULL) {
        return NULL;
    }
    p->d.last = (unsigned char *) p + sizeof(mempool_t);
    p->d.end = (unsigned char *) p + size;
    p->d.next = NULL;
    p->d.failed = 0;

    size = size - sizeof(mempool_t);
    p->max = (size < MEM_MAX_ALLOC_FROM_POOL) ? size : MEM_MAX_ALLOC_FROM_POOL;

    p->current = p;
    //p->chain = NULL;
    p->large = NULL;
    return p;
}


void mempool_destroy(mempool_t *pool)
{
    mempool_t          *p, *n;
    mempool_large_t    *l;
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            os_free(l->alloc);
        }
    }
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        os_free(p);
        if (n == NULL) {
            break;
        }
    }
}


void mempool_reset(mempool_t *pool)
{
    mempool_t        *p;
    mempool_large_t  *l;

    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            os_free(l->alloc);
        }
    }

    for (p = pool; p; p = p->d.next) {
        p->d.last = (unsigned char *) p + sizeof(mempool_t);
        p->d.failed = 0;
    }

    pool->current = pool;
    //pool->chain = NULL;
    pool->large = NULL;
}

void * mempool_alloc(mempool_t *pool, size_t size)
{
    if (size <= pool->max) {
        return _mempool_alloc_small(pool, size, 1);
    }
    return _mempool_alloc_large(pool, size);
}

static inline void * _mempool_alloc_small(mempool_t *pool, size_t size, int align)
{
    unsigned char   *m;
    mempool_t       *p;

    p = pool->current;
    do {
        m = p->d.last;
        if (align) {
            m = mem_align_ptr(m, MEM_POOL_ALIGNMENT);
        }
        if ((size_t) (p->d.end - m) >= size) {
            p->d.last = m + size;
            return m;
        }
        p = p->d.next;
    } while (p);

    return _mempool_alloc_block(pool, size);
}


static void * _mempool_alloc_block(mempool_t *pool, size_t size)
{
    unsigned char   *m;
    size_t          psize;
    mempool_t  *p, *new;

    psize = (size_t) (pool->d.end - (unsigned char *) pool);

    m = _os_memalign(MEM_POOL_ALIGNMENT, psize);
    if (m == NULL) {
        return NULL;
    }

    new = (mempool_t *) m;

    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;

    m += sizeof(mempool_data_t);
    m = mem_align_ptr(m, MEM_POOL_ALIGNMENT);
    new->d.last = m + size;

    for (p = pool->current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }

    p->d.next = new;

    return m;
}


static void * _mempool_alloc_large(mempool_t *pool, size_t size)
{
    void              *p;
    unsigned int       n;
    mempool_large_t  *large;

    p = os_malloc(size);
    if (p == NULL) {
        return NULL;
    }

    n = 0;

    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {
            large->alloc = p;
            return p;
        }

        if (n++ > 3) {
            break;
        }
    }

    large = _mempool_alloc_small(pool, sizeof(mempool_large_t), 1);
    if (large == NULL) {
        os_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


void * mempool_memalign(mempool_t *pool, size_t size, size_t alignment)
{
    void              *p;
    mempool_large_t  *large;

    p = _os_memalign(alignment, size);
    if (p == NULL) {
        return NULL;
    }

    large = _mempool_alloc_small(pool, sizeof(mempool_large_t), 1);
    if (large == NULL) {
        os_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


void mempool_free(mempool_t *pool, void *p)
{
    mempool_large_t  *l;
    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            free(l->alloc);
            l->alloc = NULL;
            return;
        }
    }
    return;
}


void * mempool_calloc(mempool_t *pool, size_t size)
{
    void *p;
    p = mempool_alloc(pool, size);
    if (p) {
        memset(p,0,size);
    }
    return p;
}


//执行实际的系统内存分配操作
void * _os_memalign(size_t alignment, size_t size)
{
    void  *p = NULL;
    int   err = 0;
#ifdef _WIN32
    p = _aligned_malloc(alignment, size);
    if(p == NULL)
        err = -1;
#else
    err = posix_memalign(&p, alignment, size);
#endif
    if (err) {
        p = NULL;
    }
    return p;
}

//线程本地存储的内存池分配

__thread mempool_t * tlmp_ctx;

int tlmp_create(size_t size)
{
    if(tlmp_ctx != NULL){
        return 1;
    }
    tlmp_ctx = mempool_create(size);

    if(tlmp_ctx != NULL)
        return 0;
    else
        return -1;
}

void tlmp_destroy()
{
    if(tlmp_ctx == NULL){
        return;
    }
    return mempool_destroy(tlmp_ctx);
}
void tlmp_reset()
{
    if(tlmp_ctx == NULL){
        return;
    }
    return mempool_reset(tlmp_ctx);
}

void * tlmp_malloc(size_t size)
{
    if(tlmp_ctx == NULL){
        tlmp_ctx = mempool_create(size);
        if(tlmp_ctx == NULL)
            return NULL;
    }
    return mempool_alloc(tlmp_ctx,size);
}
void tlmp_free(void * p)
{
    if(tlmp_ctx == NULL){
        return;
    }
    mempool_free(tlmp_ctx,p);
}



#if 1

#include <stdio.h>
#include <pthread.h>



void *thr_fn(void *arg)
{
    printf("thread1 tlp_mempool addr: %p;\n", tlmp_ctx);
	return((void *)0);
}

int main(int argc,char ** argv)
{


    getchar();
    return 0;
}

#endif







