/* ucs_tc32.c - UTF-8 to UTF-32 */

#include <assert.h>
#include <errno.h>  /* errno, EILSEQ */

#define U8_NAMES 0
#include "xchar.h" 

#define U(X) ((U8_CHAR_32)(0x ## X ##UL))

#define U8__N(L) U8_DUMMY_ ## L
#define U8_ASSERT(P) typedef unsigned U8__N(__LINE__)[P]

U8_ASSERT(sizeof(U8_CHAR_32) == 4);
U8_ASSERT(sizeof(U8_CHAR_16) == 2);

typedef struct {
    U8_CHAR_32 ucs;
    unsigned   acc, req;
} u8_state;

/*== UTF-8 -> UTF-32 =================================================*/

#define U8_TAIL(B)    ( ((B) &  0xC0U) == 0x80U )
#define U8_HEAD(B, L) (  (B) & (0xFFU  >> (L)) )
#define U8_LEN(B)     ( u8_tab[(((B) & 0xFFU) >> 3) & 0x1FU] )

static const size_t u8_tab[32] = {
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0,   2, 2, 2, 2, 3, 3, 4, 5,
};

/*
    u8len - Emulate mblen() for mb := UTF-8.
 */


size_t u8len(const char *s, size_t n)
{
    return u8tc32(NULL, s, n);
}


/*
    u8tc32 - Emulate mbtowc() for mb := UTF-8 and wc := UTF-32.
 */

size_t u8tc32(U8_CHAR_32 *pc32, const char *s, size_t n)
{
    static u8_state st;
    
    size_t u8_req, u8_use;
    const char *p = s;
    U8_CHAR_32 c32;
    
    /*
     * No octets provided? - we need as many as indicated in `*ps`.
     */
    if (s == NULL) return st.req;
    
    /*
       Tail octets provided, but not expected?
       Or head octet provided, but tail octets required?
       
       In other words: exactly one of u8_req and st.req must be
       zero.
     */
    if (((u8_req = U8_LEN(*s)) == 0) != (st.req > 0U))
	goto ilseq;
	
    /*
       Because only one of the two `u8_req` and `st.req` is non-zero,
       we get the number of actually required octets here: either
       `u8_req` or `st.req`.
     */
    u8_req += st.req;
    
    /*
       We can only use what is provided to us (`n`). If this is zero
       octets, we return the number of octets still required to complete
       the accumulated code point in `st.acc`.
     */
    if ((u8_use = (u8_req > n) ? n : u8_req) == 0)
	return n;
    
    /*
     * At least one octet is provided, we get the *value* in it first.
     * This either starts a new code point (if `st.acc` == 0U), or
     * adds 6 bits from a U8_TAIL byte to the accumulated code point
     * `st.ucs`.
     */
    if (st.acc == 0U)
        c32 = U8_HEAD(*p++, (u8_req > 1 ? u8_req + 1 : u8_req));
    else
        c32 = (st.ucs << 6) | U8_TAIL(*p++);
    
    switch (u8_use) {
    case 5:
	if (!U8_TAIL(*p)) goto ilseq;
	c32 = (c32 << 6) | (*p++ & 0x3FU);
    case 4:
	if (!U8_TAIL(*p)) goto ilseq;
	c32 = (c32 << 6) | (*p++ & 0x3FU);
    case 3:
	if (!U8_TAIL(*p)) goto ilseq;
	c32 = (c32 << 6) | (*p++ & 0x3FU);
    case 2:
	if (!U8_TAIL(*p)) goto ilseq;
	c32 = (c32 << 6) | (*p++ & 0x3FU);
    case 1:
	break;
    case 0: ilseq:
	errno = EILSEQ;
	return (size_t)-1;
    }
    
    /*
     * If the code point is still incomplete, store it in the state,
     * return the indicator `(size_t)-2` for this situation.
     */
    if (u8_use < u8_req) {
        st.ucs = c32;
        st.acc += u8_use;
        st.req  = u8_req - u8_use;
        return (size_t)-2;
    }

    /*
     * We have a complete code point - reset the state ...
     */
    st.ucs = U(0000);
    st.acc = st.req = 0U;
    
    if (c32 == U(0000)) {
	/* NUL decoded: Release the state, we're done. */
        if (pc32 != NULL) *pc32 = c32;
        return 0U;
    }

    /*
     * ... and check the code point.
     */
    if (U(D800) <= c32 && c32 < U(E000) || U(10FFFF) < c32) {
	errno = EILSEQ;
	return (size_t)-1;
    }

    if (pc32 != NULL) *pc32 = c32;
    return u8_use;
}

/*== EOF ============================ vim:tw=72:sts=0:et:cin:fo=croq:sta
                                               ex: set ts=8 sw=4 ai : */
