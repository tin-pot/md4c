/* ucs_fc32.c - UTF-32 to UTF-8 */

#include <assert.h>
#include <errno.h>  /* errno, EILSEQ */

#define U8_NAMES 0
#include "xchar.h" 

#define U(X) ((U8_CHAR_32)(0x ## X ##UL))

#define U8__N(L) U8_DUMMY_ ## L
#define U8_ASSERT(P) typedef unsigned U8__N(__LINE__)[P]

U8_ASSERT(sizeof(U8_CHAR_32) == 4);
U8_ASSERT(sizeof(U8_CHAR_16) == 2);

/*== UTF-8 -> UTF-32 =================================================*/

/*
   u8fc32 - Emulate wctomb() for wc := UTF-32 and mb := UTF-8
 */
 
size_t u8fc32(char *s, U8_CHAR_32 c32)
{
    char *p = s;
    
    if (U(D800) <= c32 && c32 < U(E000))
	return errno = EILSEQ, (size_t)-1; /* Surrogate, invalid. */
    if (U(10FFFF) < c32)
	return errno = EILSEQ, (size_t)-1; /* Out of range. */
	
    if (c32 <= U(007F))
	*p++ = (char)c32;
    else if (c32 <= U(07FF)) {
	unsigned oct_1 = ((c32 >>  6) & U(001F)) | U(00C0);
	unsigned oct_2 = ((c32 >>  0) & U(003F)) | U(0080);
	
	*p++ = (char)oct_1;
	*p++ = (char)oct_2;
    } else if (c32 < U(FFFF)) {
	unsigned oct_1 = ((c32 >> 12) & U(000F)) | U(00E0);
	unsigned oct_2 = ((c32 >>  6) & U(003F)) | U(0080);
	unsigned oct_3 = ((c32 >>  0) & U(003F)) | U(0080);
	
	*p++ = (char)oct_1;
	*p++ = (char)oct_2;
	*p++ = (char)oct_3;
    } else if (c32 < U(110000)) {
	unsigned oct_1 = ((c32 >> 18) & U(0007)) | U(00F0);
	unsigned oct_2 = ((c32 >> 12) & U(003F)) | U(0080);
	unsigned oct_3 = ((c32 >>  6) & U(003F)) | U(0080);
	unsigned oct_4 = ((c32 >>  0) & U(003F)) | U(0080);
	
	*p++ = (char)oct_1;
	*p++ = (char)oct_2;
	*p++ = (char)oct_3;
	*p++ = (char)oct_4;
    } else
	return errno = EILSEQ, (size_t)-1;

    return (size_t)(p - s);
}

/*== EOF ============================ vim:tw=72:sts=0:et:cin:fo=croq:sta
                                               ex: set ts=8 sw=4 ai : */
