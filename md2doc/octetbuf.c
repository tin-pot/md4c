/* octetbuf.c */
#include "octetbuf.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h> /* strlen() */

#define CHUNK 1024

#define ROUND(N, O) ( ((N) > sizeof (O)->a)					\
                        ? (CHUNK*(((N) + (CHUNK-1U))/CHUNK))		\
                        : sizeof (O)->a )				\
                      
#define CAPAC(NEWC, OLDC, O) ROUND((NEWC), (O))

/*
 * If c is not the current capacity, realloc to make it so.
 */
static octet *grow(octetbuf *o, size_t c)
{
    octet *p;
    size_t nn;
    
    if (c > o->c)
	c = CAPAC(c, o->c, o);
    else if (c == o->c)
	return o->p;
	
    
    nn = (o->n > c) ? c : o->n;
    
    if (c <= sizeof o->a) {
	if (o->p != o->a) {
	    if (nn > 0U)
		memcpy(o->a, o->p, nn);
	    free(o->p);
	}
	p = o->a;
	c = sizeof o->a;
    } else if (o->p == o->a) {
        assert(!octetbuf_is_locked(o));
	if ((p = malloc(c)) != NULL)
	    memcpy(p, o->p, nn);
    } else {
        assert(!octetbuf_is_locked(o));
	p = realloc(o->p, c);
    }
    
    assert(p != NULL);
    
    if (p != NULL) {
	o->p = p;
	o->n = nn;
	o->c = c;
    }
    return o->p;
}

void octetbuf_init(octetbuf *o, size_t c)
{
    o->p = NULL;
    o->c = o->n = 0U;
    assert((o->k = 0U) == 0U);
    grow(o, c);
}


void *octetbuf_reserve(octetbuf *o, size_t c)
{
    return (c > o->c) ? grow(o, c) : o->p;
}

void *octetbuf_resize(octetbuf *o, size_t n, int value)
{
    size_t k, m = o->n;
    octet *p  = (n > o->c) ? grow(o, n) : o->p;
    
    for (k = m; k < n; ++k)
	p[k] = value;
    o->n = n;
	
    return p;
}

void *octetbuf_shrink_to_fit(octetbuf *o)
{
    size_t n = o->n;
    
    return (n < o->c) ? grow(o, n) : o->p;
}

void *octetbuf_clear(octetbuf *o)
{
    o->n = 0U;
    return o->p;
}
       
size_t octetbuf_shift(octetbuf *o, octetidx_t k)
{
    size_t n;
    
    if (k > o->n) k = o->n;
    if ((n = o->n-k) > 0U) 
	memmove(o->p, o->p + k, n);
    return o->n = n;
}

octetidx_t octetbuf_push_back(octetbuf *o, void *p, size_t n)
{
    size_t m, nn;
    octetidx_t i = o->n;
    octet *dst;
    
    assert(p != NULL || n == 0U);
    
    if (n == NTS) n = strlen(p);
    if (n == 0U) return i;
    
    dst = ((nn = (m = o->n) + n) > o->c) ? grow(o, nn)+m : o->p+o->n;
    assert(o->n + n <= o->c);
    memcpy( dst, p, n );
    o->n += n;
    return i;
}

octetidx_t octetbuf_push_c(octetbuf *o, int c)
{
    if (o->n >= o->c) octetbuf_reserve(o, o->n + 1U);
    assert(o->n < o->c);
    o->p[o->n++] = c;
    return o->n-1U;
}

void *octetbuf_pop_back(octetbuf *o, size_t n)
{
    assert(n <= o->n);
    return o->p + (o->n -= n);
}
       
octet *octetbuf_extend(octetbuf *o, int d)
{
    size_t m  = o->n;
    size_t nn = (d < 0 && (size_t)-d > m) ? 0U : m + d;
    octet *p  = octetbuf_resize(o, nn, 0);
    return (d >= 0) ? p + m : p + o->n;
}


void *octetbuf_dup(const octetbuf *o)
{
    size_t n = o->n;
    void *p  = malloc(n);
    
    if (p != NULL)
	memcpy(p, o->p, n);
    return p;
}

void *octetbuf_release(octetbuf *o)
{
    void *p = (o->p == o->a) ? octetbuf_dup(o) : o->p;
    
    assert(o->p == o->a || !octetbuf_is_locked(o));
    
    o->p = o->a;
    o->n = 0U;
    o->c = sizeof o->a;
    
    return p;
}

#ifndef NDEBUG
/*
 * Return true if old state was "unlocked".
 *
 * Do time-consuming "reading" work only if buffer is *already* locked:
 *
 *     if (!octetbuf_lock(o)) {
 *         / * Do time-consuming "reading" * /
 *     }
 */
bool octetbuf_lock(octetbuf *o)
{
    return o->k++ == 0U;
}

/*
 * Return true if new state is "unlocked".
 *
 * Do *forcefully* remove all locks:
 * 
 *     while (!octetbuf_unlock(o))
 *         ;
 *
 */
bool octetbuf_unlock(octetbuf *o)
{
    return o->k == 0U || --o->k == 0U;
}
#endif

/*--------------------------------------------------------------------*/
#if 0
#define octetbuf_size(O)        ((O)->n)
#define octetbuf_capacity(O)    ((O)->c)
#define octetbuf_empty(O)       (0U == (O)->n)
#define octetbuf_at(O, I)       ((void*)&(O)->p[I])
#define octetbuf_ptr(O)         ((void*)(O)->p)
#define octetbuf_begin(O)       ((O)->p)
#define octetbuf_end(O)         (&(O)->p[(O)->n])
#define octetbuf_fini(O)        (free(octetbuf_release(O))
#define octetbuf_is_locked(O)   ((O)->k > 0U)
#endif

#undef octetbuf_size
size_t octetbuf_size(const octetbuf *o)
{
    return o->n;
}

#undef octetbuf_capacity
size_t octetbuf_capacity(const octetbuf *o)
{
    return o->c;
}

#undef octetbuf_empty
bool octetbuf_empty(const octetbuf *o)
{
    return o->n == 0U;
}

#undef octetbuf_at
octet *octetbuf_at(const octetbuf *o, octetidx_t i)
{
    assert(o->p != NULL);
    assert(i < o->n);
    return o->p + i;
}

#undef octetbuf_elem_at
void *octetbuf_elem_at(const octetbuf *o, octetidx_t i, size_t n)
{
    n *= i;
    assert(o->p != NULL);
    assert(i < o->n);
    return o->p + i;
}

#undef octetbuf_ptr
void *octetbuf_ptr(const octetbuf *o)
{
    return o->p;
}

#undef octetbuf_begin
octet *octetbuf_begin(const octetbuf *o)
{
    assert(o->p != NULL);
    return o->p;
}

#undef octetbuf_end
octet *octetbuf_end(const octetbuf *o)
{
    assert(o->p != NULL);
    return o->p + o->n;
}

#undef octetbuf_elem_extend
void *octetbuf_elem_extend(octetbuf *o, int d, size_t n)
{
    int dd = d * (int)n;
    size_t m  = o->n;
    size_t nn = (dd < 0 && (size_t)-dd > m) ? 0U : m + dd;
    octet *p  = octetbuf_resize(o, nn, 0);
    return (dd >= 0) ? p + m : p + o->n;
}

#undef octetbuf_fini
void octetbuf_fini(octetbuf *o)
{
    free(octetbuf_release(o));
}

#undef octetbuf_is_locked
#ifndef NDEBUG
bool octetbuf_is_locked(const octetbuf *o)
{
    return o->k > 0U;
}
#endif

/*== EOF ============================ vim:tw=72:sts=0:et:cin:fo=croq:sta
                                               ex: set ts=8 sw=4 ai : */
