/* octetbuf.h */

#ifndef OCTETBUF_H_INCLUDED
#define OCTETBUF_H_INCLUDED 1

#ifndef STDBOOL_H
#define STDBOOL_H <stdbool.h>
#endif
#ifndef STDINT_H
#define STDINT_H <stdint.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef NTS
#define NTS ((size_t)-1)
#endif

struct octetbuf_;

typedef struct octetbuf_    octetbuf;
typedef size_t              octetidx_t;
typedef uint8_t             octet;

void       octetbuf_init(octetbuf *, size_t);

bool       octetbuf_empty(const octetbuf *);
size_t     octetbuf_size(const octetbuf *);
size_t     octetbuf_capacity(const octetbuf *);
void      *octetbuf_ptr(const octetbuf *o);
octet     *octetbuf_at(const octetbuf *, octetidx_t);
void      *octetbuf_elem_at(const octetbuf *, octetidx_t, size_t);
octet     *octetbuf_begin(const octetbuf *);
octet     *octetbuf_end(const octetbuf *);
octet     *octetbuf_extend(octetbuf *, int);
void      *octetbuf_elem_extend(octetbuf *, int, size_t);
void      *octetbuf_dup(const octetbuf *);

void      *octetbuf_reserve(octetbuf *, size_t);
void      *octetbuf_resize(octetbuf *, size_t, int value);
void      *octetbuf_shrink_to_fit(octetbuf *);
void      *octetbuf_clear(octetbuf *);
size_t     octetbuf_shift(octetbuf *, octetidx_t);
       
octetidx_t octetbuf_push_s(octetbuf *, const char *s);
octetidx_t octetbuf_push_ws(octetbuf *, const wchar_t *ws);

octetidx_t octetbuf_push_back(octetbuf *, const void *p, size_t n);
octetidx_t octetbuf_push_c(octetbuf *, int c);
octetidx_t octetbuf_push_wc(octetbuf *, wchar_t wc);

octetidx_t octetbuf_push_uint8_t(octetbuf *,  uint8_t  u8);
octetidx_t octetbuf_push_uint16_t(octetbuf *, uint16_t u16);
octetidx_t octetbuf_push_uint32_t(octetbuf *, uint32_t u32);
octetidx_t octetbuf_push_int8_t(octetbuf *,   int8_t   i8);
octetidx_t octetbuf_push_int16_t(octetbuf *,  int16_t  i16);
octetidx_t octetbuf_push_int32_t(octetbuf *,  int32_t  i32);

void      *octetbuf_pop_back(octetbuf *, size_t);
int        octetbuf_pop_c(octetbuf *);
wchar_t    octetbuf_pop_wc(octetbuf *);

uint8_t    octetbuf_pop_uint8_t(octetbuf *);
uint16_t   octetbuf_pop_uint16_t(octetbuf *);
uint32_t   octetbuf_pop_uint32_t(octetbuf *);
int8_t     octetbuf_pop_int8_t(octetbuf *);
int16_t    octetbuf_pop_int16_t(octetbuf *);
int32_t    octetbuf_pop_int32_t(octetbuf *);
       
void      *octetbuf_release(octetbuf *);
void       octetbuf_fini(octetbuf *);

bool       octetbuf_is_locked(const octetbuf *);
bool       octetbuf_lock(octetbuf *);
bool       octetbuf_unlock(octetbuf *);
#ifdef NDEBUG
#define    octetbuf_is_locked(O) (false)
#define    octetbuf_lock(O)      (true)
#define    octetbuf_unlock(O)    (true)
#endif

/* Private parts here---Don't you dare look! */

#define OCTETBUF_SIZE 512

#ifndef NDEBUG
#define octetbuf_front							\
    octet   *p;								\
    size_t   n;								\
    size_t   c;								\
    unsigned k;								
#else
#define octetbuf_front							\
    octet   *p;								\
    size_t   n;								\
    size_t   c;								
#endif

struct octetbuf_dummy_ {
    octetbuf_front
    octet    a[1];
};

#define OCTETBUF_ASIZE \
                   (OCTETBUF_SIZE - offsetof(struct octetbuf_dummy_, a))

struct octetbuf_ {
    octetbuf_front
    octet    a[OCTETBUF_ASIZE];
};

#define octetbuf_size(O)        ((O)->n)
#define octetbuf_capacity(O)    ((O)->c)
#define octetbuf_empty(O)       (0U == (O)->n)
#define octetbuf_at(O, I)       ((void*)&(O)->p[I])
#define octetbuf_elem_at(O, I, S)					\
                                ((void*)octetbuf_at((O),(I)*(int)(S)))
#define octetbuf_ptr(O)         ((void*)(O)->p)
#define octetbuf_begin(O)       ((O)->p)
#define octetbuf_end(O)         (&(O)->p[(O)->n])
#define octetbuf_elem_extend(O, I, S)					\
                              ((void*)octetbuf_extend((O),(I)*(int)(S)))
#define octetbuf_push_s(O, S)   (octetbuf_push_back((O), (S), NTS))
#define octetbuf_fini(O)        (free(octetbuf_release(O)))
#ifndef NDEBUG
#define octetbuf_is_locked(O)   ((O)->k > 0U)
#endif

#endif/*OCTETBUF_H_INCLUDED*/