/*== escape.c ==========================================================


------------------------------------------------------------------------

======================================================================*/

#ifdef _MSC_VER
#pragma warning (disable: 4996)
#endif

#include "escape.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "xchar.h"

#include "octetbuf.h"

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

#define NDIGRAPH 1000

#define PRG "escape"


#define NUL  0
#define SP  32
#define CR  13
#define LF  10

/*
    We use Tcl's _variable substitution_ syntax:
    
    $name
	Name is the name of a scalar variable; the name is a
	sequence of one or more characters that are a letter, digit,
	underscore, or namespace separators (two or more colons).
	Letters and digits are only the standard ASCII ones (0-9,
	A-Z and a-z).

    $name(index)
	Name gives the name of an array variable and index gives
	the name of an element within that array. Name must
	contain only letters, digits, underscores, and namespace
	separators, and may be an empty string. Letters and digits
	are only the standard ASCII ones (0-9, A-Z and a-z). Command
	substitutions, variable substitutions, and backslash
	substitutions are performed on the characters of index.

    ${name}
	Name is the name of a scalar variable or array element.
	It may contain any characters whatsoever except for close
	braces. It indicates an array element if name is in the form
	"arrayName(index)" where arrayName does not contain any open
	parenthesis characters, "(", or close brace characters, "}",
	and index can be any sequence of characters except for close
	brace characters. No further substitutions are performed
	during the parsing of name.

	There may be any number of variable substitutions in a
	single word. Variable substitution is not performed on words
	enclosed in braces.

	Note that variables may contain character sequences other
	than those listed above, but in that case other mechanisms
	must be used to access them (e.g., via the set command's
	single-argument form).

    <http://tcl.tk/man/tcl8.6/TclCmd/Tcl.htm#M12>
*/

#define ESC_BMP  'u'
#define ESC_UCS  'U'
#define ESC_DGR  '^'
#define ESC_GRPO '{'
#define ESC_GRPC '}'
#define ESC_PARO '('
#define ESC_PARC ')'

#define SEQMAX    64 /* ( "\{1234578" , SEP , NUL ) */

char ESCAPE = '\\';
char SUBST  =  '$';

static const char INVC[]   = " !\"%&\'()*+,-./:;<=>?^_";
static const char CMARKC[] = "";
static const char NMSTRT[] = ":_";
static const char NMCHAR[] = ":_";

#define ISNMSTRT(ES, C) ( 32 < (C) && (C) < 128 &&			\
                     (isalpha(C) || isdigit(C) ||			\
                      strchr(ES->nmstart, (C)) != NULL) )
                      
#define ISNMCHAR(ES, C) ( 32 < (C) && (C) < 128 &&			\
                     (isalpha(C) || isdigit(C) ||			\
                      strchr(ES->nmchar, (C)) != NULL) )
                      
#define IS646INV(C) ( 32 < (C) && (C) < 128 &&				\
                     (isalpha(C) || isdigit(C) ||			\
                      strchr(INVC, (C)) != NULL) )

#define ISCMARK(C)  ( strchr(CMARKC, (C)) != NULL )

static void error(int errnum, const char *msg, ...)
{
    unsigned code = (unsigned)abs(errnum);
    va_list va;
    
    va_start(va, msg);
    fprintf(stderr, "%s: error %u: ", PRG, code);
    vfprintf(stderr, msg, va);
    va_end(va);
    
    if (errnum < 0) exit(EXIT_FAILURE);
}

/*
 * Estimation: say we have 2000 digraphs (a lot!), averaging
 * three UTF-8 octets each: that's 6000 raw octets we have in
 * an array. Because 13 bits index 0 .. 2**13-1 = 0 .. 8191 
 * positions, we have *three* spare bits in a uint16_t where we
 * can encode the *number of UCS chars* in the replacement string,
 * in the range 0..7.
 */
 
#define IDXMAX ~(~0U << 13)
#define IDX(OI) ((OI) & IDXMAX)
#define LEN(OI) ((OI) >> 13)
#define MKOI(I, L) ( ((L) << 13) | ((I) & IDXMAX) )

/*
 * A one-character "digraph" simply has ch[1] == NUL.
 */
struct dgr_ {
    char       ch[2];
    uint16_t   oi;
};

struct esc_state_ {
    unsigned        ndgr;
    struct dgr_    *dgrs;
    char           *defs;
    char            escape;
    char            subst;
    esc_cb          cb;
    
    unsigned        lno;
    const char     *nmstart;
    const char     *nmchar;
    
    octetbuf dgrs_buf; /* = {0}; */
    octetbuf octs_buf; /* = {0}; */
};

int esc_set_escape(esc_state *esp, int ch)
{
    if (NUL < ch && ch < 128 && ch != esp->subst)
	esp->escape = ch;
    else if (ch != NUL)
	return EOF;
    return esp->escape;
}

int esc_set_subst(esc_state *esp, int ch)
{
    if (NUL < ch && ch < 128 && ch != esp->escape)
	esp->subst = ch;
    else if (ch != NUL)
	return EOF;
    return esp->subst;
}
esc_cb esc_callback(esc_state *esp, esc_cb cb)
{
    esc_cb oldcb = esp->cb;
    
    esp->cb = cb;
    return oldcb;
}

void esc_nmstart(esc_state *esp, const char *chars)
{
    assert(chars != NULL);
    esp->nmstart = chars;
}

void esc_nmchar(esc_state *esp, const char *chars)
{
    assert(chars != NULL);
    esp->nmchar = chars;
}

int cmpdgr(const void *lhs, const void *rhs)
{
    const struct dgr_ *const ldgr = (struct dgr_*)lhs;
    const struct dgr_ *const rdgr = (struct dgr_*)rhs;
    int d;
    
    (d = ldgr->ch[0] - rdgr->ch[0]) || 
    (d = ldgr->ch[1] - rdgr->ch[1]);
    return d;
}

int esc_define(esc_state *esp, const char ch[2], const long ucsdef[])
{
    octetidx_t  oi;
    struct dgr_ dgr;
    char        u8seq[U8_LEN_MAX];
    size_t      u8nseq;
    
    u8nseq = xctomb(u8seq, ucsdef[0]);
    if (u8nseq == (size_t)-1) {
	assert(errno == EILSEQ);
	error(ESC_ERR_UCS, "Invalid UCS code point: %06lX\n", ucsdef[0]);
	xctomb(NULL, L'0');
	return -1;
    }
#ifndef NDEBUG
    {
    xchar_t u32;
    size_t s32 = mbtoxc(&u32, u8seq, u8nseq);
    assert (s32 < 5U);
    assert(u32 == ucsdef[0]);
    }
#endif

    oi = octetbuf_push_back(&esp->octs_buf, u8seq, u8nseq);

    dgr.ch[0] =  ch[0];
    dgr.ch[1] = (ch[1] == SP) ? NUL : ch[1];
    dgr.oi    = MKOI(oi, 1U);

    assert(LEN(dgr.oi) == 1U);
    assert(IDX(dgr.oi) == oi);

    octetbuf_push_back(&esp->dgrs_buf, &dgr, sizeof dgr);
    return 0;
}

int readline(esc_state *esp, FILE *infp, char dgr[2], xchar_t *ucs)
{
    int ch1, ch;
    bool valid = false;
    
    while ((ch1 = getc(infp)) != EOF) switch (ch1) {
	int nf;
	long cp;
	
    case SP:
	if ((ch = getc(infp)) == EOF) return EOF;
	dgr[0] = ch;
	if ((ch = getc(infp)) == EOF) return EOF;
	dgr[1] = ch;
	nf = fscanf(infp, " %lx", &cp);
	valid = (nf == 1);
	if (!valid)
	    error(ESC_ERR_DGR_LINEFORMAT, "Line %u: invalid.\n", esp->lno);
	/* FALLTHROUGH */
    case '#': default:
	if (ch1 != '#' && ch1 != SP)
	    error(ESC_ERR_DGR_LINETYPE, "Line %u: '%c' invalid.\n", esp->lno, ch1);
	while ((ch = getc(infp)) != EOF && ch != LF)
	    ;
	return (valid) ? *ucs = cp, SP : '#';
    }
    return EOF;
}

esc_state *esc_create(FILE *infp)
{
    char           ch[2];
    unsigned long  cp[2];
    
    struct esc_state_   es = { 0 };
    struct esc_state_ *esp = NULL;
    
    es.nmstart = NMSTRT;
    es.nmchar  = NMCHAR;
    es.escape  = ESCAPE;
    es.subst   = SUBST;
    
    if (infp == NULL)
	goto create;
	
    while (readline(esp, infp, ch, cp) != EOF) {
#if !defined(NDEBUG)
	/* fprintf(stderr, "%c%c --> U+%06lX\n", ch[0], ch[1], cp); */
	assert(IS646INV(ch[0]) || ch[0] == SP);
	assert(IS646INV(ch[1]) || ch[1] == SP);
#endif
	cp[1] = 0U;
	esc_define(&es, ch, cp);
    }
    
    es.ndgr   = octetbuf_size(&es.dgrs_buf)/sizeof(struct dgr_);
    es.dgrs   = octetbuf_release(&es.dgrs_buf);
    es.defs   = octetbuf_release(&es.octs_buf);
    qsort(es.dgrs, es.ndgr, sizeof es.dgrs[0], cmpdgr);
    
#ifndef NDEBUG
    fprintf(stderr, "%s: %u digraphs defined.\n", PRG, es.ndgr);
#endif
create:
    esp = malloc(sizeof es);
    if (esp != NULL)
	memcpy(esp, &es, sizeof es);
    return esp;
}

size_t esc_expand(esc_state *esp, char buf[5], const char ch[2])
{
    struct dgr_ dgr, *pdgr;
    const char *u8seq;
    size_t u8nseq, nuc;
    
    dgr.ch[0] = ch[0];
    dgr.ch[1] = ch[1];
    
    pdgr = bsearch(&dgr, esp->dgrs, esp->ndgr,
                                           sizeof esp->dgrs[0], cmpdgr);
                   
    if (pdgr == NULL)
	return (size_t)-1;
	
    u8seq  = esp->defs + IDX(pdgr->oi);
    nuc = LEN(pdgr->oi);
    assert(nuc == 1U);
    u8nseq = u8len(u8seq, U8_LEN_MAX);
    
    if (u8nseq > 0U)
	memcpy(buf, u8seq, u8nseq);
    return u8nseq;
}

size_t esc_bsubst(esc_state *esp, octetbuf *dst, octetbuf *src)
{
    const unsigned char *p, *begin, *end;
    enum { 
	ST_OUTSIDE,   ST_ESCAPE,   ST_BMP,    ST_UCS,	    ST_SUBST,
	ST_SUB_GRP,   ST_SUB_PAR,
	ST_UNI,       ST_DGR0,	   ST_DGR,    ST_CODE,      ST_CALLBACK,
	ST_INVALID
    } st = ST_OUTSIDE;
    
    char          seq[SEQMAX], u8seq1[U8_LEN_MAX], u8seq2[U8_LEN_MAX];
    unsigned      nseq = 0U, u8nseq1 = 0U, u8nseq2 = 0U;
    char          dgr[2];
    const char    escape = esp->escape;
    const char    subst  = esp->subst;
    
    begin = octetbuf_begin(src);
    end   = octetbuf_end(src);
    for (p = begin; p < end; ++p) {
	unsigned char ch = *p;
	
	switch (st) {
	    case ST_OUTSIDE:
		if (ch == escape)
		    st = ST_ESCAPE, seq[nseq++] = ch;
		else if (ch == subst)
		    st = ST_SUBST, seq[nseq++] = ch;
		break;   
	    case ST_ESCAPE:
		switch (ch) {
		    case ESC_BMP:  st = ST_BMP; break;
		    case ESC_UCS:  st = ST_UCS; break;
		    case ESC_GRPO: st = ST_UNI; break;
		    case ESC_DGR:  st = ST_DGR0; break;
		    default:
			if (ch == escape)
			    st = ST_OUTSIDE;
			else if ((IS646INV(ch) || ch == SP) && !ISCMARK(ch))
			    st = ST_DGR;
		}
		if (st == ST_ESCAPE)
		    st = ST_INVALID;
		else
		    seq[nseq++] = ch;
		break;
	    case ST_BMP:
		if (isxdigit(ch)) 
		    seq[nseq++] = ch;
		else
		    st = ST_INVALID;
		if (nseq == 6) 
		    st = ST_CODE;
		break;
	    case ST_UCS:
		if (isxdigit(ch))
		    seq[nseq++] = ch;
		else
		    st = ST_INVALID;
		if (nseq == 8)
		    st = ST_CODE;
		break;
	    case ST_UNI:
		if (isxdigit(ch))
		    seq[nseq++] = ch;
		else if (ch == ESC_GRPC)
		    st = ST_CODE;
		else if (nseq == 10)
		    st = ST_INVALID;
		break;
	    case ST_SUBST:
		if (nseq == 1 && ISNMSTRT(esp, ch) ||
		    nseq  > 1 && ISNMCHAR(esp, ch))
		    seq[nseq++] = ch;
		else if (nseq == 1 && ch == subst)
		    st = ST_OUTSIDE;
		else if (nseq == 2 && ch == ESC_GRPO)
		    st = ST_SUB_GRP;
		else if (nseq > 2 && ch == ESC_PARO)
		    seq[nseq++] = ch, st = ST_SUB_PAR;
		else if (nseq > 2 && esp->cb != NULL)
		    st = ST_CALLBACK;
		else
		    st = ST_INVALID;
		break;
	    case ST_SUB_GRP:
		if (ch == ESC_GRPC)
		    st = ST_CALLBACK;
		else if (nseq + 1 >= SEQMAX)
		    st = ST_INVALID;
		else 
		    seq[nseq++] = ch;
		break;
	    case ST_SUB_PAR:
		if (ch == ESC_PARC && nseq < SEQMAX)
		    seq[nseq++] = ch, st = ST_CALLBACK;
		else if (nseq + 1 >= SEQMAX)
		    st = ST_INVALID;
		else 
		    seq[nseq++] = ch;
		break;
	    case ST_DGR0:
		if (IS646INV(ch) || ch == SP)
		    seq[nseq++] = ch, st = ST_DGR;
		else
		    st = ST_INVALID;
		break;
	    case ST_DGR:
		assert (nseq > 2 && seq[1] == ESC_DGR ||
		        nseq > 1 && seq[1] != ESC_DGR);
		dgr[0] = seq[nseq-1];
		dgr[1] = seq[nseq] = NUL;

		assert(IS646INV(dgr[0]) || dgr[0] == SP);
		u8nseq1 = esc_expand(esp, u8seq1, dgr);

		if (IS646INV(ch)) {
		    dgr[1] = seq[nseq++] = ch;
		    u8nseq2 = esc_expand(esp, u8seq2, dgr);
		    if (u8nseq1 == -1)
			ch = NUL;
		}
		st = ST_OUTSIDE;
		if (u8nseq2 != -1)
		    ch = NUL, octetbuf_push_back(dst, u8seq2, u8nseq2);
		else if (u8nseq1 != -1)
		    octetbuf_push_back(dst, u8seq1, u8nseq1);
		else
		    st = ST_INVALID;
		break;
	    default:
		assert(!"Can't happen!");
		break;
	}
	
	if (st == ST_CODE) {
	    xchar_t ucs;
	    int sn, d = U8_LEN_MAX;
	    size_t u8n;
	    octet *op;
	    
	    seq[nseq] = NUL;
	    assert(isxdigit(seq[2]));
	    sn = sscanf(seq+2, "%lx", &ucs);
	    assert(sn == 1);
	    op = octetbuf_extend(dst, d);
	    errno = 0;
	    u8n = xctomb(op, ucs);
	    assert(u8n <= U8_LEN_MAX || errno == EILSEQ);
	    if (u8n <= U8_LEN_MAX)
		d -= u8n;
	    if (errno == EILSEQ)
		st = ST_INVALID;
	    else
		ch = NUL;
	    octetbuf_extend(dst, -d);
	} else if (st == ST_CALLBACK) {
	    const char *sub = NULL;
	    seq[nseq] = NUL;
	    assert(nseq >= 2);
	    if (esp->cb != NULL && seq[1] != NUL)
		sub = esp->cb(seq+1);
	    if (sub != NULL)
		octetbuf_push_s(dst, sub);
	    else
		st = ST_INVALID;
	    if (ch == ESC_GRPC || ch == ESC_PARC) {
		ch = NUL;
	    }
	}
	
	if (st == ST_INVALID)
	    octetbuf_push_back(dst, seq, nseq);
	    
	switch (st) case ST_INVALID: case ST_CODE: 
	            case ST_CALLBACK: case ST_OUTSIDE: {
	    nseq = 0U;
	    st = ST_OUTSIDE;
	    if (ch != NUL)
		octetbuf_push_c(dst, ch);
	}
    }
    return octetbuf_shift(src, p-begin);
}

size_t esc_fsubst(esc_state *esp, char buffer[], size_t nbuffer, FILE *in)
{
    static octetbuf dst = {0};
    static octetbuf src = {0};
    size_t nbuf  = nbuffer - 1U;
    size_t nread = nbuf;
    octet *pread;
    int d;
    
    octetbuf_reserve(&dst, nbuf);
    octetbuf_reserve(&src, nbuf);
    
    while (octetbuf_size(&dst) < nbuf && nread == nbuf) {
	pread = octetbuf_extend(&src, (int)nbuf);
        octetbuf_lock(&src);
	nread = fread(pread, 1U, nbuf, in);
	if ((d = nbuf - nread) > 0)
	    pread = octetbuf_extend(&src, -d);
	esc_bsubst(esp, &dst, &src);
	octetbuf_unlock(&src);
    }
    
    if ((nread = octetbuf_size(&dst)) < nbuf)
	nbuf = nread;
    memcpy(buffer, octetbuf_begin(&dst), nbuf);
    buffer[nbuf] = NUL;
    octetbuf_shift(&dst, nbuf);
    return nbuf;
}

/*== EOF ============================ vim:tw=72:sts=0:et:cin:fo=croq:sta
                                               ex: set ts=8 sw=4 ai : */
