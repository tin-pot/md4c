/*== cm2doc.c =========================================================*

NAME
    cm2doc - CommonMark to document processor

SYNOPSIS
    cm2doc [ --version ] [ -h | --help ]
           [ --rast | { ( --repl | -r ) replfile } ]
           [ (--title | -t) string]
           [ (--css | -c) url]
           [ --sourcepos ] [ --hardbreaks ] [ --smart ] [ --safe ]
           [ --normalize ] [ --validate-utf8 ]
           file ...

DESCRIPTION
	A CommonMark parser based on `cmark` which produces output
	documents controlled by "replacement files".

OPTIONS
    --version
        Displays `cmark` version and copyright information and exits.
        
    --help
    -h
	Displays `cm2doc` usage information and diplays which 
	environment variables are accessed.
    
    --rast
    --rasta
        Produces RAST output. This precludes all use of "replacement
        files". The `--rasta` option also outputs the internal root 
        element which contains the document element.
        
    --digr
    -d
	Specifies a "digraph file" for use in preprocessing.
	
    --repl
    -r
	Specifies a "replacement file" to use. If this option occurs
	multiple time, the corresponding "replacements files" are
	read and processed as if concatenated in this order.


    --title
    -t
	Specifies a document title (`DC.title`); this overrides 
	the title given in an input file metadata bock, if there is
	one.

    --css
    -c
	Specifies a CSS style file (`CM.css`).

    --sourcepos
        Includes source position information in CommonMark elements.
        
    --hardbreaks
        Treats line breaks in input as "hard" line breaks.
        
    --smart
        Uses smart punctuation for quotation marks, dashes, and 
        ellipsis.
        
    --safe
        Suppresses rendering of raw HTML.
        
    --normalize
        Joins adjacent text nodes in the generated element structure.
        
    --validate-utf8
        Checks and sanitizes UTF-8 encoding of input files.
        
ENVIRONMENT
    DIGRAPHS
        Names the default "digraph file".
    REPL_DIR
        Names the directory where "replacement files" (given through
        the option `-r` or the `REPL_DEFAULT` variable) are searched.
        
    REPL_DEFAULT
        Names the "replacement file" to use if no `-r` option was
        given.
        
BUGS
    Certainly many. Please report bugs and issues on the GitHub
    page for this project, <https://github.com/tin-pot/cmark>.


------------------------------------------------------------------------

COPYRIGHT NOTICE AND LICENSE

Copyright (C) 2015 Martin Hofmann <mh@tin-pot.net>

Redistribution and use in source and binary forms, with or without 
modification, are permitted provided that the following conditions 
are met:

   1. Redistributions of source code must retain the above copyright 
      notice, this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copy-
      right notice, this list of conditions and the following dis-
      claimer in the documentation and/or other materials provided 
      with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY 
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLU-
DING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*=====================================================================*/

#if !defined(NDEBUG) && defined(_MSC_VER)
#include <CrtDbg.h>
#define BREAK() _CrtDbgBreak()
#endif
#include <assert.h>

#include <ctype.h> /* toupper() */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef _MSC_VER
#define snprintf _snprintf
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>  /* strftime(), gmtime(), time() */

#include "octetbuf.h"
#include "escape.h"
#include "xchar.h" /* Unicode utilities - UTF-8 conversion. */

/*
 * CommonMark library
 */
#define CMARK_NO_SHORT_NAMES 1
#include "config.h"
#include "cmark.h"
#include "cmark_ctype.h"
#include "node.h"
#include "buffer.h"
#include "houdini.h"


#define NUL  0

#define U(X)                    ((xchar_t)(0x ## X ##UL))
#define BMP(X)                  ((xchar_t)((X) & 0xFFFFUL))

/*
 * Prototypes here, because of ubiquituous use.
 */
void error(const char *msg, ...);
void syntax_error(const char *msg, ...);


/*== ESIS API ========================================================*/

/*
 * Callback function types for document content transmittal.
 */

/*
 * A convenience mechanism: In an API using arguments like
 *
 *     ..., char *data, size_t len, ...
 *
 * placing the value NTS in the actual parameter `len` indicates that
 * `data` is a NUL-terminated (byte) string aka NTBS, so the *callee*
 * can determine the correct value for `len` from `data`.
 */
#ifndef SIZE_MAX /* C99: in <stdint.h> (but in <limits.h> in MSC!) */
#define SIZE_MAX    (~(size_t)0U)
#endif
#ifndef NTS
#define NTS SIZE_MAX
#endif

typedef void *ESIS_UserData;

typedef void (ESIS_Attr)(ESIS_UserData,    const char *name,
                                           const char *val, size_t);
typedef void (ESIS_Start)(ESIS_UserData,               cmark_node_type);
typedef void (ESIS_Cdata)(ESIS_UserData,   const char *cdata, size_t);
typedef void (ESIS_End)(ESIS_UserData,                 cmark_node_type);

typedef struct ESIS_CB_ {
    ESIS_Attr	 *attr;
    ESIS_Start	 *start;
    ESIS_Cdata	 *cdata;
    ESIS_End	 *end;
} ESIS_CB;

typedef struct ESIS_Port_ {
    const ESIS_CB *cb;
    ESIS_UserData  ud;
} ESIS_Port;

/*
 * We have:
 *
 *  - input file parsers,
 *  - output file generators, and
 *  - ESIS filters.
 */
 
int        parse_cmark(FILE *from, ESIS_Port *to, cmark_option_t,
                                                    const char *meta[]);
                                   
int        parse_esis(FILE *from, ESIS_Port *to, unsigned options);

ESIS_Port* generate_repl(FILE *to,               unsigned options);
ESIS_Port* generate_rast(FILE *to,               unsigned options);
ESIS_Port* generate_esis(FILE *to,               unsigned options);

ESIS_Port* filter_toc(ESIS_Port*to,              unsigned options);

#define DO_ATTR(N, V, L)   esis_cb->attr(esis_ud, N, V, L)
#define DO_START(NT)       esis_cb->start(esis_ud, NT)
#define DO_CDATA(D, L)     esis_cb->cdata(esis_ud, D, L)
#define DO_END(NT)         esis_cb->end(esis_ud, NT)


/*== Meta-data =======================================================*/

/*
 * Names of environment variables used to 
 *  1. Point to a directory used in the search path.
 *  2. Point to a default replacement file if none given otherwise.
 */
#define REPL_DIR_VAR     "REPL_DIR"
#define REPL_DEFAULT_VAR "REPL_DEFAULT"
#define DIGRAPH_VAR      "DIGRAPHS"
#define DIGRAPH_PATH     "C:\\Projects\\escape\\doc\\digraphs.txt"

/*
 * Optionally make the Git commit ident and the repository URL 
 * available as character strings.
 *
 * Use -DWITH_GITIDENT=1 to switch this on (and have the strings
 * ready for the linker to find them!); or do nothing and use the
 * placeholder values given below. 
 */
 
#if WITH_GITIDENT
extern const char cmark_gitident[];
extern const char cmark_repourl[];
#else
static const char cmark_gitident[] = "n/a";
static const char cmark_repourl[]  = "https://github.com/tin-pot/cmark";
#endif

/*
 * Predefined "pseudo-attribute" names, usable in the "replacement" text
 * for @prolog (and for the document element), eg to set <META> ele-
 * ments in an HTML <HEAD>.
 *
 * At compile time, these names are accessible through META_... macros.
 *
 * NOTE: We use a "pseudo-namespace" for "cm2doc" (and "cmark")
 * specific "pseudo-attributes", to avoid any conflict with real
 * attributes in a document type.
 *
 * The first three are from Dublin Core, and can be set in the first
 * lines of the CommonMark input document by placing a PERCENT SIGN
 * at the very beginning of the line:
 *
 *
 *     % The Document Title
 *     % A. U. Thor
 *     % 2015-11-11T11:11:11+11
 *
 * In subsequent lines, you can set "user-defined" attributes for
 * use in the prolog, like:
 *
 *     % foo-val: Foo value
 *     % bar.val: Bar value
 *
 * *but* you can't use COLON ":" **in** these attribute name for obvious
 * reasons. (Maybe ending the attribute name at (the first) COLON
 * followed by SPACE would be a more reasonable approach ...).
 */
 
 /* TODO: Colon in meta attribute names `% bar:val: Bar value` */
 
#define META_DC_TITLE   "DC.title"
#define META_DC_CREATOR "DC.creator"
#define META_DC_DATE    "DC.date"
#define META_CSS        "CM.css"

/*
 * Default values for the "pseudo-attributes".
 *
 * At compile time, they are accessible through DEFAULT_... macros.
 */

/* Data and creator will be re-initialized in main(). */
static char default_date[11]    = "YYYY-MM-DD";
static char default_creator[81] = "N.N.";

#define DEFAULT_DC_CREATOR  default_creator
#define DEFAULT_DC_DATE     default_date

/* Hard-coded defaults for command-line options --title and --css. */
#define DEFAULT_DC_TITLE    "Untitled Document"
#define DEFAULT_CSS         "default.css"

/*== CommonMark Nodes ================================================*/

/*
 * For each CommonMark node type we define a GI conforming to the
 * ISO 8879 SGML Reference Concrete Syntax, which has:
 *
 *     NAMING LCNMSTRT ""
 *            UCNMSTRT ""
 *            LCNMCHAR "-."
 *            UCNMCHAR "-."
 *            NAMECASE GENERAL YES
 *                     ENTITY  NO
 *
 * (This is replicated in the IS...() character class macros below.)
 *
 * The Reference Quantity Set also sets NAMELEN to 8, so these GIs are
 * somewhat shorter than the ones in the CommonMark DTD -- which is
 * a good thing IMO.
 *
 * (All this is of course purely cosmetic and/or a nod to SGML, where
 * all this "structural mark-up" stuff came from. -- You could define
 * and use any GI and any NMSTART / NMCHAR character classes you want
 * for giving names to the CommonMark node types.)
 */
#define NAMELEN      8 /* The Reference Core Syntax value. */
#define ATTCNT      40 /* The Reference Quantity Set value. */
#define ATTSPLEN   960 /* The Reference Quantity Set value. */

#define ISUCNMSTRT(C) ( 'A' <= (C) && (C) <= 'Z' )
#define ISLCNMSTRT(C) ( 'a' <= (C) && (C) <= 'z' )
#define ISUCNMCHAR(C) ( ISUCNMSTRT(C) || (C) == '-' || (C) == '.' )
#define ISLCNMCHAR(C) ( ISLCNMSTRT(C) || (C) == '-' || (C) == '.' )

/* How many node types there are, and what the name length limit is. */
#define NODE_NUM       (CMARK_NODE_LAST_INLINE+2)
#define NODE_MARKUP    (CMARK_NODE_LAST_INLINE+1)
#define NODENAME_LEN    NAMELEN

static const char* const nodename[NODE_NUM+1] = {
     NULL,	/* The "none" type (enum const 0) is invalid! */
   /*12345678*/
    "CM.DOC",
    "CM.QUO-B",
    "CM.LIST",
    "CM.LI",
    "CM.COD-B",
    "CM.HTM-B",
    "CM.CUS-B",
    "CM.PAR",
    "CM.HDR",
    "CM.HR",
    "CM.TXT",
    "CM.SF-BR",
    "CM.LN-BR",
    "CM.COD",
    "CM.HTM",
    "CM.CUS",
    "CM.EMPH",
    "CM.STRN",
    "CM.LNK",
    "CM.IMG",
    "MARKUP"
};


/*== Replacement Backend =============================================*/

/*
 * "Reserved Names" to bind special "replacement texts" to:
 * The output document's prolog (and, if needed, epilog).
 */
enum rn_ {
    RN_INVALID,
    RN_PROLOG,
    RN_EPILOG,
    RN_NUM,	/* Number of defined "reserved names". */
};

static const char *const rn_name[] = {
    NULL,
    "PROLOG",
    "EPILOG",
    NULL
};
    
static const char *rn_repl[RN_NUM]; /* Replacement texts for RNs. */

/*--------------------------------------------------------------------*/

/*
 * Some C0 control characters (internally used to encode the 
 * replacement texts).
 */
 
#define SOH  1
#define STX  2
#define ETX  3
#define EOT  4
#define VT  11 /* Encodes the begin-of-line "+". */
#define SO  14 /* Encodes the attribute substitution "[". */
#define SI  15 /* Encodes the attribute substitution "]". */

/*
 * The C0 control characters allowed in SGML/XML; all other C0 are
 * **not** usable in a document, and thus free for our private use.
 */
#define HT   9	/* SGML SEPCHAR */
#define LF  10	/* SGML RS */
#define CR  13	/* SGML RE */
#define SP  32	/* SGML SPACE */

#define EOL          LF	    /* Per ISO C90 text stream. */
 
/*--------------------------------------------------------------------*/

/*
 * SGML function characters, character classes, and delimiters.
 */
#define RE           LF
#define RS           CR
#define SPACE        SP
#define ISSEPCHAR(C) ((C) == HT)

#define MSSCHAR      '\\'          /* Markup-scan-suppress character. */

#define LIT          '\"'
#define LITA         '\''


#define ISDIGIT(C)   ( '0' <= (C) && (C) <= '9' )
#define ISHEX(C)     ( ISDIGIT(C) || \
                      ('A'<=(C) && (C)<='F') || ('a'<=(C) && (C)<='f') )
#define ISSPACE(C)   ( (C) == RS || (C) == RE || (C) == SPACE || \
                                                          ISSEPCHAR(C) )
#define ISNMSTART(C) ( ISDIGIT(C) || ISUCNMSTRT(C) || ISLCNMSTRT(C) )
#define ISNMCHAR(C)  ( ISNMSTART(C) || (C) == '-' || (C) == '.' )

/*
 * Notation indicator in "info string"
 * ===================================
 *
 * NOTA_DELIM is (currently pre-defined to be) 
 *
 *     U+007C VERTICAL BAR (decimal 124) `|`
 *
 * It is used to put an "info string" into an *inline* code span
 * like this:
 *
 *     dolor sit amet, `Z|x %e %N` consectetuer adipiscing elit. 
 *
 * This *inline* "info string" has the exact same meaning as the
 * standard "info string" on a code block fence:
 *
 *     ~~~ Z|
 *     x %e %N
 *     ~~~
 *
 * NOTE that the trailing `|` is needed, otherwise this gets treated
 * as a "regular" info string on a code block!
 *
 * In a code block info string, the `|` can be used to separate the 
 * notation name from other info (which ends up in the `info` 
 * attribute):
 *
 *     ~~~ Z|informative
 *     x %e %N
 *     ~~~
 *
 * Both examples produce (in the HTML output file):
 *
 *     <MARKUP notation="Z" ...>x %e %N</MARKUP>
 *
 * but the *inline* code span produces the attribute `display="inline"`,
 * while the fenced code *block* gives `display="block"` as the second
 * attribute in the `<MARKUP>` element. (And the second block example
 * has also an attribute `info="informative"` ...)
 */
 
#define NOTA_DELIM '|'

/*== Replacement Definitions =========================================*/

/*
 * Replacement definitions for a node type are hold in a 
 * `struct repl_`
 */
 
#define STAG_REPL       0x0001
#define ETAG_REPL       0x0002
#define STAG_BOL_START  0x0010
#define STAG_BOL_END    0x0020
#define ETAG_BOL_START  0x0040
#define ETAG_BOL_END    0x0080

typedef size_t textidx_t;  /* Index into cmark_strbuf text_buf. */

struct taginfo_ {
    cmark_node_type nt;
    textidx_t	    atts[2*ATTCNT + 2];
};

struct repl_ {
    struct taginfo_ taginfo;
    const char     *repl[2];
    bool            is_cdata;
    struct repl_   *next;
};

/*
 * Replacement definitions: one array member per node type,
 * plus the (currently unused) member at index 0 == NODE_NONE.
 */
 
static struct repl_ *repl_tab[NODE_NUM];

static octetbuf text_buf = { 0 };



/*== Element Stack Keeping ===========================================*/

typedef size_t nameidx_t; /* Index into nameidx_buf. */
typedef size_t validx_t;  /* Index into validx_buf. */

static const size_t NULLIDX = 0U; /* Common NULL value for indices. */

/*
 * Attribute names and values of current node(s).
 */
static octetbuf attr_buf = { 0 };

/*
 * We "misuse" a `octetbuf` here to store a growing array
 * of `attridx_t` (not `char`) elements. There are no alignment issues
 * as long as the array stays homogenuous, as the buffer is from
 * `malloc()`, and thus suitably aligned.
 *
 * An attribute name index of 0U marks the end of the attribute
 * list (of the currently active node).
 */
static octetbuf nameidx_buf = { 0 };
static octetbuf validx_buf = { 0 };
#define NATTR ( octetbuf_size(&nameidx_buf) / sizeof(nameidx_t) )

/*
 * The name index and value index arrays as seen as `nameidx_t *`
 * and `validx_t *` rvalues, ie as "regular C arrays".
 */
#define NAMEIDX(I) (*(nameidx_t*)octetbuf_elem_at(&nameidx_buf, (I),	\
                                                     sizeof(nameidx_t)))
#define VALIDX(I)  (* (validx_t*)octetbuf_elem_at(&validx_buf,  (I),	\
                                                      sizeof(validx_t)))

/*
 * Append one `nameidx_t` element to the `NAMEIDX` array, and 
 * dito for `validx_t` and the `VALIDX` array.
 */
#define PUT_NAMEIDX(I) ( octetbuf_push_back(&nameidx_buf, \
                                              &(I), sizeof(nameidx_t)) )
#define PUT_VALIDX(I)  ( octetbuf_push_back(&validx_buf, \
                                               &(I), sizeof(validx_t)) )

/*
 * We use NULLIDX to delimit "activation records" for the currently
 * open elements: the first invocation of `push_att()` after a 
 * `lock_attr()` call will push a NULLIDX first, then the given
 * attribute name and value into a "new" activation record, while
 * "unlocking" the new activation record.
 */
#define close_atts(NT)   ( PUT_NAMEIDX(NULLIDX), PUT_VALIDX(NT) )
#define pop_atts()       POP_ATTS()
#define current_nt()     ( VALIDX(NATTR-1U) )

void push_att(const char *name, const char *val, size_t len)
{
    nameidx_t nameidx;
    validx_t  validx;   
    
    if (len == NTS) len = strlen(val);
    
    nameidx = octetbuf_size(&attr_buf);
    octetbuf_push_s(&attr_buf, name);
    octetbuf_push_c(&attr_buf, NUL);
    
    validx = octetbuf_size(&attr_buf);
    octetbuf_push_back(&attr_buf, val, len);
    octetbuf_push_c(&attr_buf, NUL);
    
    PUT_NAMEIDX(nameidx);
    PUT_VALIDX(validx);
}

/*
 * Remove the current activation record. 
 */
 
#define POP_ATTS()							\
do {									\
    size_t top = NATTR;							\
    if (top > 0U) {							\
	nameidx_t nameidx = NAMEIDX(--top);				\
	assert(nameidx == NULLIDX);					\
	do {								\
	    octetbuf_pop_back(&nameidx_buf, sizeof(nameidx_t));		\
	    octetbuf_pop_back(&validx_buf,  sizeof(validx_t);		\
	    if (nameidx != NULLIDX) attr_buf.n = nameidx;		\
	} while (top > 0U && (nameidx = NAMEIDX(--top)) != NULLIDX);	\
    }									\
    assert(top == NATTR);						\
} while (0)

#if 1
#undef pop_atts
#endif

void pop_atts(void)
{
    nameidx_t nameidx = 0U;
    
    size_t top = NATTR;
    if (top > 0U) {
	nameidx = NAMEIDX(--top);
	assert(nameidx == NULLIDX);
	do {
	    octetbuf_pop_back(&nameidx_buf, sizeof(nameidx_t));
	    octetbuf_pop_back(&validx_buf,  sizeof(validx_t));
	    assert(top == NATTR);
	    if (nameidx != NULLIDX) attr_buf.n = nameidx;
	} while (top > 0U && (nameidx = NAMEIDX(--top)) != NULLIDX);
    }
}


/*
 * Find attribute in active input element (ie in buf_atts),
 * return the value.
 */
 
const char* att_val(const char *name, unsigned depth)
{
    size_t k;
    
    assert ((k = NATTR) > 0U);
    assert (NAMEIDX(k-1U) == NULLIDX);
    assert (depth > 0U);
	
    if (depth == 0U) return NULL;
    
    for (k = NATTR; k > 0U; --k) {
	nameidx_t nameidx;
	validx_t validx;
	const size_t idx = k-1U;
	
	assert(nameidx_buf.n == 0U || nameidx_buf.p != NULL);
	assert(idx*sizeof(nameidx_t) < nameidx_buf.n);
	
	nameidx = NAMEIDX(idx);
	
	assert(attr_buf.n == 0U || attr_buf.p != NULL);
	assert(nameidx < attr_buf.n);
	
	if (nameidx == NULLIDX && depth-- == 0U)
	    break;
	    
	assert(validx_buf.n == 0U  || validx_buf.p != NULL);
	assert(idx*sizeof(validx_t) < validx_buf.n);
	
	validx = VALIDX(idx);
	
	assert(validx  < attr_buf.n);
	if (!strcmp(octetbuf_at(&attr_buf, nameidx), name))
	    return octetbuf_at(&attr_buf, validx);
    }
	    
    return NULL;
}


/*== Replacement Definitions =========================================*/

void register_notation(const char *nmtoken, size_t len);

/*
 * Set the replacement text for a node type.
 */
void set_repl(struct taginfo_ *pti,
              const char *repl_text[2],
              bool is_cdata)
{
    cmark_node_type nt = pti->nt;
    struct repl_ *rp = malloc(sizeof *rp);
    
    assert(0 <= nt);
    assert(nt < NODE_NUM);
    
    rp->repl[0]  = repl_text[0];
    rp->repl[1]  = repl_text[1];
    rp->taginfo  = *pti;
    rp->is_cdata = is_cdata;
    rp->next     = repl_tab[nt];
    
    repl_tab[nt] = rp;
    
    if (nt == NODE_MARKUP) {
        int i;
        size_t ai, vi;
        for (i = 0; (ai = pti->atts[i+0]) != 0U; i += 2) {
            if (strcmp(octetbuf_at(&text_buf, ai), "notation") == 0 &&
                    (vi = pti->atts[i+1]) != 0U) {
                /*
                 * A rule for the `MARKUP` element mentions a
                 * value for the `notation` attribute.
                 */
                const char *s = octetbuf_at(&text_buf, vi);
                register_notation(s, NTS);
            }
        }
    }
}

/*--------------------------------------------------------------------*/

/*
 * The output stream (right now, this is always stdout).
 */
FILE *outfp;
bool  outbol = true;

#define PUTC(ch)							\
do {									\
    putc(ch, outfp);							\
    outbol = (ch == EOL);						\
} while (0)

/*
 * Attribute substitution and replacement text output.
 */

const char *put_subst(const char *p)
{
    const char *name;
    const char *val = NULL;
    unsigned depth = *p & 0xFFU;
    
    assert(p[-1] == SO);
    name = p + 1U;
    p = name + strlen(name)+2U;
    assert(p[-1] == SI);

    val = att_val(name, depth);
    
    if (val != NULL) {
        size_t k;
        for (k = 0U; val[k] != NUL; ++k)
    	PUTC(val[k]);
    } else
	error("Undefined attribute '%s'\n", name);
    
    return p;
}

void put_repl(const char *repl)
{
    const char *p = repl;
    char ch;
    
    assert(repl != NULL);
    while ((ch = *p++) != NUL) {
	if (ch == VT) {
	    if (!outbol)
		PUTC(EOL);
	} else if (ch == SO)
	    p = put_subst(p);
	else 
	    PUTC(ch);
    }
}

struct repl_ *select_rule(cmark_node_type nt)
{
    struct repl_ *rp;
    
    for (rp = repl_tab[nt]; rp != NULL; rp = rp->next) {
	const textidx_t *const atts = rp->taginfo.atts;
	int i;
	
	for (i = 0; atts[2*i] != NULLIDX; ++i) {
	    const char *name, *sel_val = NULL;
	    const char *cur_val;
	    
	    name = octetbuf_at(&text_buf, atts[2*i+0]);
	    cur_val = att_val(name, 1);
	    if (atts[2*i+1] != NULLIDX) {
		sel_val = octetbuf_at(&text_buf, atts[2*i+1]);
	    }
	    if (sel_val == NULL && cur_val == NULL)
		break; /* Attribute existence mismatch. */
		
	    if (sel_val != NULL && (cur_val == NULL ||
		             strcmp(cur_val, sel_val) != 0))
		break; /* Attribute value mismatched. */
	}
	if (atts[2*i] == NULLIDX)
	    return rp; /* Matched all attribute selectors. */
    }
    return NULL; /* No matching rule found. */
}

/*== ESIS API for the Replacement Backend ============================*/

struct notation_name_ {
    const char *name;
    struct notation_name_ *next;
} *notations = NULL;

bool is_notation(const char *nmtoken, size_t len)
{
    const struct notation_name_ *pn;
    
    if (nmtoken == NULL || len == 0U)
	return false;
	
    if (len == NTS) len = strlen(nmtoken);
    assert(len > 0U);
	
    for (pn = notations; pn != NULL; pn = pn->next)
	if (strncmp(pn->name, nmtoken, len) == 0)
	    return true;
    return false;
}

void register_notation(const char *nmtoken, size_t len)
{
    struct notation_name_ *pn = malloc(sizeof *pn);
    char *name = malloc(len + 1);
    size_t k;
    
    assert(nmtoken != NULL);
    if (len == NTS) len = strlen(nmtoken);
    assert(len > 0U);
    
    if (is_notation(nmtoken, len)) {
        /*
         * Name is already known and registered - nothing to do.
         */
        return;
    }
    
    for (k = 0U; k < len; ++k) {
	if (!ISNMCHAR(nmtoken[k]))
	    error("\"%*.s\": Invalid NOTATION name.\n", (int)len,
                                                               nmtoken);
    }
    memcpy(name, nmtoken, len);
    name[len] = NUL;
    pn->name = name;
    pn->next = notations;
    notations = pn;
}

/*
 * The `is_cdata` flag is a rough solution to transmit state information
 * from the start-tag handler to the subsequent cdata handler ...
 */
bool is_cdata = false;

void repl_Attr(ESIS_UserData ud,
                          const char *name, const char *val, size_t len)
{
    push_att(name, val, len);
}

void repl_Start(ESIS_UserData ud, cmark_node_type nt)
{
    const struct repl_ *rp = NULL;
    
    close_atts(nt);

    /*
     * Find matching replacement definition, and output the
     * substituted "start string".
     */
     
    if (nt != CMARK_NODE_NONE && (rp = select_rule(nt)) != NULL) {
	if (rp->repl[0] != NULL) {
	    put_repl(rp->repl[0]);
	} 
    }
    
    /*
     * Let the cdata handler know if HTML markup or current
     * replacement definition dictates literal cdata output ...
     */
     
    is_cdata = (rp != NULL && rp->is_cdata) ||
               (nt == CMARK_NODE_HTML_BLOCK) ||
               (nt == CMARK_NODE_HTML_INLINE);
    
    /*
     * If no matching definition was found, or no start string was
     * given there, we're done already.
     *
     * This amounts to a "default replacement definition" of
     *
     *     * - / -
     *
     * (except that the "universal element selector" is not available
     * in our replacement definition syntax (yet?).
     */ 
}


void repl_Cdata(ESIS_UserData ud, const char *cdata, size_t len)
{
    static cmark_mem stdmem          = { calloc, realloc, free };
    static cmark_strbuf houdini      = { 0 };
    static int          houdini_init = 0;
    
    const cmark_node_type nt = current_nt();
    size_t      k;
    const char *p;
    
    if (len == NTS) len = strlen(cdata);
    p = cdata;
    
    if (!is_cdata) {
        /*
         * For HTML content and elements declared to be CDATA by
         * the replacement definition, `is_cdata` should be true
         * here.
         *
         * The content of every other node is written "escaped".
         */
         
	if (!houdini_init) {
	    cmark_strbuf_init(&stdmem, &houdini, 1024);
	    houdini_init = 1;
	}

	/*
	 * The last argument `0` indicates that SOLIDUS is *not*
	 * to be escaped -- which would prevent us from using it
	 * as the SGML NET.
	 */
	
	houdini_escape_html0(&houdini, (uint8_t*)cdata, len, 0);
	p   = cmark_strbuf_cstr(&houdini);
	len = cmark_strbuf_len(&houdini);
    }

    /*
     * Output the character data. We must do this character by
     * character using `PUTC()` in order to keep track of line
     * breaks and update the `outbol` flag accordingly.
     */
     
    for (k = 0U; k < len; ++k)
        PUTC(p[k]);
	
    cmark_strbuf_clear(&houdini);
}

void repl_End(ESIS_UserData ud, cmark_node_type nt)
{
    const struct repl_ *rp = repl_tab[nt];
    
    if (nt != CMARK_NODE_NONE && (rp = select_rule(nt)) != NULL) {
	if (rp->repl[1] != NULL) {
	    put_repl(rp->repl[1]);
	}
    }
    
    /*
     * Reset the `is_cdata` switch. This will only work
     * if no other element is nested inside an element for
     * which the replacement definition indicated `<![CDATA[`
     * (but this seems a reasonable assumption after all!).
     */
     
    is_cdata = false;
	
    pop_atts();
}
 
static const struct ESIS_CB_ repl_CB = {
    repl_Attr,
    repl_Start,
    repl_Cdata,
    repl_End
};

ESIS_Port* generate_repl(FILE *to, unsigned options)
{
    static struct ESIS_Port_ port = { &repl_CB, NULL };
    
    options = 0U; /* NOT USED */
    /*
     * Only one output file at a time.
     */
    assert(port.ud == NULL);
    port.ud = to;
    outfp  = to;
    outbol = true;
    return &port;
}

/*== ESIS API for RAST Output Generator ==============================*/

#define RAST_ALL 1U

struct RAST_Param_ {
    FILE *outfp;
    unsigned options;
} rast_param;



void rast_data(FILE *fp, const char *data, size_t len, char delim)
{
    size_t k;
    int in_special = 1;
    int at_bol = 1;
    
    for (k = 0U; k < len; ++k) {
	int ch = data[k] & 0xFF;
	if (32 <= ch && ch < 128) {
	    if (in_special) {
		if (!at_bol) {
		    fputc(EOL, fp);
		}
		fputc(delim, fp);
		in_special = 0;
		at_bol = 0;
	    }
	    fputc(ch, fp);
	} else {
	    if (!in_special) {
		if (!at_bol) {
		    fputc(delim, fp);
		    fputc('\n', fp);
		}
		in_special = 1;
		at_bol = 1;
	    }
	    if (128 < ch) {
		size_t n = len - k;
		xchar_t c32;
		int i = mbtoxc(&c32, &data[k], n);
		if (i > 0) {
		    fprintf(fp, "#%lu\n", c32);
		    k += i-1;
		} else {
		    unsigned m;
		    n = (unsigned)(-i);
		    if (n == 0) n = 1;
		    
		    for (m = 0; m < n; ++m) {
			fprintf(fp, "#X%02X\n", 0xFFU & data[k+m]);
		    }
		    k = k + m - 1;
		    fprintf(stderr,
		              "Invalid UTF-8 sequence in data line!\n");
		}
	    } else {
		switch (ch) {
		case RS:  fputs("#RS\n", fp);	    break;
		case RE:  fputs("#RE\n", fp);	    break;
		case HT:  fputs("#TAB\n", fp);	    break;
		default:  fprintf(fp, "#%u\n", ch); break;
		}
	    }
	    
	    at_bol = 1;
	}
    }
    if (!in_special) { fputc(delim, fp); at_bol = 0; }
    if (!at_bol) fputc(EOL, fp);
}


void discard_atts(void)
{
    /*
     * Occupy index 0 position after clearing the buffer, so that 
     * index == 0U can be used as a sentinel.
     */
    octetbuf_clear(&attr_buf);
    octetbuf_push_c(&attr_buf, NUL);
    
    octetbuf_clear(&nameidx_buf);
    octetbuf_clear(&validx_buf);
}

void rast_Attr(ESIS_UserData ud, const char *name, const char *val, size_t len)
{
    FILE *fp = ((struct RAST_Param_*)ud)->outfp;
    
    if (len == NTS) len = strlen(val);
    
    push_att(name, val, len);
    return;
}

void rast_Start(ESIS_UserData ud, cmark_node_type nt)
{
    struct RAST_Param_* rastp = ud;
    FILE *fp = rastp->outfp;
    size_t nattr = NATTR;
    
    if (nt == 0 && !(rastp->options & RAST_ALL)) {
	discard_atts();
	return;
    }
	
    if (nattr > 0U) {
	size_t k;
	const char *GI = nodename[nt];

	fprintf(fp, "[%s\n", (GI == NULL) ? "#0" : GI);
	for (k = nattr; k > 0U; --k) {
	    nameidx_t nameidx = NAMEIDX(k-1);
	    nameidx_t validx  = VALIDX(k-1);
	    const char *name = attr_buf.p + nameidx;
	    const char *val  = attr_buf.p + validx;
	    fprintf(fp, "%s=\n", name);
	    rast_data(fp, val, strlen(val), '!');
	}
	fprintf(fp, "]\n");
    } else
	fprintf(fp, "[%s]\n", nodename[nt]);
	
    discard_atts();
    return;
}


void rast_Cdata(ESIS_UserData ud, const char *cdata, size_t len)
{
    FILE *fp = ((struct RAST_Param_*)ud)->outfp;
    if (len == NTS) len = strlen(cdata);
    
    rast_data(fp, cdata, len, '|');
}

void rast_End(ESIS_UserData ud, cmark_node_type nt)
{
    struct RAST_Param_* rastp = ud;
    FILE *fp = rastp->outfp;
    
    if (nt == 0 && !(rastp->options & RAST_ALL))
	return;
    else {
	const char *GI = nodename[nt];
	fprintf(fp, "[/%s]\n", (GI == NULL) ? "#0" : nodename[nt]);
    }
    return;   
}

const struct ESIS_CB_ rast_CB = {
    rast_Attr,
    rast_Start,
    rast_Cdata,
    rast_End,
};


ESIS_Port* generate_rast(FILE *to, unsigned options)
{
    static ESIS_Port rast_port = { &rast_CB, &rast_param, };
    /*
     * Only one output file at a time.
     */
    assert(rast_param.outfp == NULL);
    rast_param.outfp   = to;
    rast_param.options = options;
    return &rast_port;
}

/*== Generator for OpenSP format ESIS output =========================*/

/*== Parser for OpenSP format ESIS input  ============================*/

/*== CommonMark Document Rendering into an ESIS Port ================*/

/*
 * Rendering a document node into the ESIS callbacks.
 */
 

struct infosplit {
    const char  *name, *suffix;
    size_t       nlen,  slen;
};

static int infosplit(struct infosplit *ps, const char *s, size_t n)
{
    const char *t, *u;
    bool suppress = false, found = false;
    
    while (n > 0U && (*s == SP || *s == HT)) {
        ++s, --n;
    }
    t = s;
    ps->suffix = s;
    ps->slen   = n;
    if (n > 0U && *t == NOTA_DELIM) {
        ++t, --n;
        suppress = true;
    }
    for (u = t; n > 0U && ISNMCHAR(*u); ++u, --n)
        ;
    if (t < u && n > 0U && *u == NOTA_DELIM) {
        ps->name = t;
        ps->nlen = (size_t)(u - t);
        found = is_notation(ps->name, ps->nlen); 
        if (found) {
            if (suppress) {
                ++ps->suffix;
                --ps->slen;
            } else {
                ps->suffix = (n > 1) ? u + 1 : NULL;
                ps->slen   = (n > 1) ? n - 1 : 0U;
            }
        }
    }
    return found && !suppress;
}

static int S_render_node_esis(cmark_node *node,
                              cmark_event_type ev_type,
                              ESIS_Port *to)
{
    cmark_delim_type delim;
    bool entering = (ev_type == CMARK_EVENT_ENTER);
    char buffer[100];

    const ESIS_CB  *esis_cb = to->cb;
    ESIS_UserData   esis_ud = to->ud;

    if (!entering) {
	if (node->first_child) {
	    DO_END(node->type);
	}
	return 1;
    }
    
    switch (node->type) {
    case CMARK_NODE_TEXT:
    case CMARK_NODE_HTML_BLOCK:
    case CMARK_NODE_HTML_INLINE:
	if (node->type != CMARK_NODE_TEXT) {
	    DO_ATTR("type", "HTML", NTS);
	    DO_ATTR("display", node->type == CMARK_NODE_HTML_BLOCK ? 
		    "block" : "inline", NTS);
	}
	DO_START(node->type);
	DO_CDATA(node->as.literal.data, node->as.literal.len);
	DO_END(node->type);
	break;

    case CMARK_NODE_LIST:
	switch (cmark_node_get_list_type(node)) {
	case CMARK_ORDERED_LIST:
	    DO_ATTR("type", "ordered", NTS); 
	    sprintf(buffer, "%d", cmark_node_get_list_start(node));
	    DO_ATTR("start", buffer, NTS);
	    delim = cmark_node_get_list_delim(node);
	    DO_ATTR("delim", (delim == CMARK_PAREN_DELIM) ?
		"paren" : "period", NTS);
	    break;
	case CMARK_BULLET_LIST:
	    DO_ATTR("type", "bullet", NTS);
	    break;
	default:
	    break;
	}
	DO_ATTR("tight", cmark_node_get_list_tight(node) ?
	    "true" : "false", NTS);
	DO_START(node->type);
	break;

    case CMARK_NODE_HEADING:
	sprintf(buffer, "%d", node->as.heading.level);
	DO_ATTR("level", buffer, NTS);
	DO_START(node->type);
	break;

    case CMARK_NODE_CODE:
    case CMARK_NODE_CODE_BLOCK:
	/*
	 * If the info string (for code block) rsp the data string (for
	 * inline code) has the form:
	 *
	 *     ( { S } , name , "|" , suffix )
	 *
	 * where *S* is `SP` or `TAB`, *name* is the name of a known
	 * notation, and *suffix* any string, then we convert the
	 * code element into a custom element.
	 *
	 * What if the info/data string is nevertheless the intended
	 * content and this conversion should not take place?
	 *
	 *     ( { S } , "|", name , "|" , suffix )
	 */
	{
	    cmark_node_type    nt = node->type;
	    struct infosplit   split;
	    const char        *info, *data;
	    size_t             ilen,  dlen;
	    const bool         is_inline = (nt == CMARK_NODE_CODE);

	    info = node->as.code.info.data;
	    ilen = node->as.code.info.len;

	    if (infosplit(&split, info, ilen)) {
		/*
		 * Use split.name as notation name,
		 * and if inline, split.suffix as content or (if block)
		 * suffix as extra info.
		 */
		nt = NODE_MARKUP;
		DO_ATTR("notation", split.name, split.nlen);
		if (is_inline) {
		    DO_ATTR("display", "inline", 6U);
		    data = split.suffix;
		    dlen = split.slen;
		} else {
		    DO_ATTR("display", "block", 5U);
		    data = node->as.code.literal.data;
		    dlen = node->as.code.literal.len;
		    if (split.slen > 0U)
			DO_ATTR("info", split.suffix, split.slen);
		}
	    } else {
		/*
		 * Regular code element, if inline use info as content,
		 * if block it is the info attribute.
		 */
		if (is_inline) {
		    data = split.suffix;
		    dlen = split.slen;
		} else {
		    data = node->as.code.literal.data;
		    dlen = node->as.code.literal.len;
		    if (split.slen > 0U)
			DO_ATTR("info", split.suffix, split.slen);
		}
	    }

	    DO_START(nt);
	    DO_CDATA(data, dlen);
	    DO_END(nt);
	}
	break;

    case CMARK_NODE_LINK:
    case CMARK_NODE_IMAGE:
	DO_ATTR("destination",
	    node->as.link.url.data, node->as.link.url.len);
	DO_ATTR("title", node->as.link.title.data,
	    node->as.link.title.len);
	DO_START(node->type);
	break;

    case CMARK_NODE_HRULE:
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
	DO_START(node->type);
	DO_END(node->type);
	break;

    case CMARK_NODE_DOCUMENT:
    default:
	DO_START(node->type);
	break;
    } /* switch */

    return 1;
}

char *cmark_render_esis(cmark_node *root, ESIS_Port *to)
{
  cmark_event_type ev_type;
  cmark_node *cur;
  cmark_iter *iter = cmark_iter_new(root);

  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cur = cmark_iter_get_node(iter);
    S_render_node_esis(cur, ev_type, to);
  }
  cmark_iter_free(iter);
  return NULL;
}


/*====================================================================*/

/*
 * do_meta_lines -- Set meta-data attributes from pandoc-style header.
 */
 
size_t do_meta_lines(char *buffer, size_t nbuf, const ESIS_Port *to)
{
    size_t ibol, nused;
    unsigned dc_count = 0U;
    static const char *dc_name[] = {
	META_DC_TITLE,
	META_DC_CREATOR,
	META_DC_DATE,
	NULL,
    };
    char version[1024];
    const ESIS_CB *esis_cb = to->cb;
    ESIS_UserData  esis_ud = to->ud;
		
    ibol = 0U;
    nused = 0U;
    
    to->cb->attr(to->ud, META_DC_TITLE,   DEFAULT_DC_TITLE,   NTS);
    to->cb->attr(to->ud, META_DC_CREATOR, DEFAULT_DC_CREATOR, NTS);
    to->cb->attr(to->ud, META_DC_DATE,    DEFAULT_DC_DATE,    NTS);
    
    sprintf(version, "            %s;\n"
		     "            date: %s;\n"
		     "            id: %s\n"
		     "        ",
	    cmark_repourl,
	    __DATE__ ", " __TIME__,
	    cmark_gitident);
    DO_ATTR("CM.doc.v", version, NTS);
    DO_ATTR("CM.ver", CMARK_VERSION_STRING, NTS);
		
    for (ibol = 0U; buffer[ibol] == '%'; ) {
	size_t len;
        char *p;
        size_t ifield;
        
	/*
	 * Field starts after '%', ends before *p = LF.
	 */
        ifield = ibol + 1U;
        if (buffer[ifield] == ' ') ++ifield;
        if (ifield >= nbuf)
            break;
            
        p = memchr(buffer+ifield, '\n', nbuf - ifield);
        if (p == NULL)
            break; /* No EOL ==> fragment buffer too short. */
            
        /*
         * One after '\n'.
         */
        ibol = (p - buffer) + 1U;
        
        /*
         * We copy buffer[ifield .. ibol-2], ie the line content
         * from ifield to just before the '\n', and append a NUL 
         * terminator, of course.
         */
        len = ibol - ifield - 1U;
        if (len > 1U) {
	    if (dc_name[dc_count] != NULL)
		DO_ATTR(dc_name[dc_count++], buffer+ifield, len);
	    else {
		const char *colon, *val;
		char name[NAMELEN+1];
		size_t nname, nval;
		colon = strstr(buffer+ifield, ": ");
		if (colon != NULL && 
		              (nname = colon - (buffer+ifield)) < len) {
		    if (nname > NAMELEN) nname = NAMELEN;
		    strncpy(name, buffer + ifield, nname);
		    name[nname] = NUL;
    		
		    val     = colon + 2;
		    while (val[0] != EOL && ISSPACE(val[0]) &&
		                                          val[1] != EOL)
			++val;
		    nval = 0U;
		    while (val[nval] != EOL)
			++nval;
		    
		    DO_ATTR(name, val, nval);
		} else
		    fprintf(stderr, "Meta line \"%% %.*s\" ignored: "
		                           "No ': ' delimiter found.\n",
		                               (int)len, buffer+ifield);
	    }
        }
    }
    
    nused = ibol;
    return nused;
}

/*== Replacement Definitions Parsing =================================*/

/*
 * During parsing of the "Replacement Definition" file we keep these
 * globals around, mostly to simplify the job of keeping track of
 * the position in the input text file (for diagnostic messages).
 */
 
FILE *replfp         = NULL;
const char *filename = "<no file>";
unsigned lineno      = 0U;  /* Text input position: Line number. */
unsigned colno       = 0U;  /* Text input position: Column number. */

#define LA_SIZE 4

char        la_buf[LA_SIZE], ch0;
unsigned    la_num = 0U;

#define COUNT_EOL(CH) (((CH) == EOL) ? (++lineno, colno = 0U, (CH)) :	\
                                                       (++colno, (CH)) )

#define GETC(CH) ( la_num ? ( (CH) = la_buf[--la_num] ) :		\
                            ( (CH) = getc(replfp), COUNT_EOL(CH) ) )

#define UNGETC(CH) (la_buf[la_num++] = (CH))

#define PEEK()   ( la_buf[la_num++] = ch0 = getc(replfp),		\
                                                        COUNT_EOL(ch0) )

/*--------------------------------------------------------------------*/

/*
 * Error and syntax error diagnostics.
 */
 
void error(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    vfprintf(stderr, msg, va);
    va_end(va);
    exit(EXIT_FAILURE);
}


void syntax_error(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    fprintf(stderr, "%s(%u:%u): error: ", filename, lineno, colno);
    vfprintf(stderr, msg, va);
    va_end(va);
}

/*--------------------------------------------------------------------*/

/*
 * Parsing the replacement definition file format.
 */
 

/*
 * S = SPACE | SEPCHAR | RS | RE
 *
 * P_S(ch) accepts ( { S } )
 */
#define P_S(CH)								\
do {									\
    while (ISSPACE(CH))							\
	CH = GETC(CH);							\
} while (0)

/*
 * An _attribute substitution_ in the _replacement text_ gets encoded
 * like this:
 *
 *     attrib subst = "${" , [ prefix ] , nmstart , { nmchar } , "}"
 *                  | "$"  , [ prefix ] , nmstart , { nmchar } ;
 *
 *     encoded form:  SO ,  precode   ,  char   , {  char  } ,  NUL , SI 
 *
 * The (optional) prefix character ":" or **Digit** is encoded like
 * this (using SP for "no prefix"):
 *
 *     prefix  precode
 *
 *      ./.      SP
 *      "."     0xFF
 *      "0"     0x01
 *      "1"     0x02
 *      ...      ...
 *
 *      "9"     0x0A
 *
 *   - Thus "precode" can be used as a "depth" argument directly, and
 *     because SI = 13, 
 *
 *   - we can still search for SI starting from the SO
 *     right at the front of this _attribute substitution_ encoding.
 *
 *   - And the _attribute name_ is a NTBS starting at offset 2 after the
 *     initial SO.
 */
 
int P_attr_subst(int ch, octetbuf *pbuf, const char lit)
{
    int code = 0;
    int brace = 0;
       
    assert(ch == '$');
    ch = GETC(ch);
    if (ch == '{') {
        brace = ch;
        ch = GETC(ch);
    }
    
    if (ISNMSTART(ch))
	code = SP;
    else if (ISDIGIT(ch) || ch == ':') {
	static const char in_ [] = ":0123456789";
	static const char out_[] = "\xFF\0x01\0x02\0x03\0x04\0x05"
	                               "\0x06\0x07\0x08\0x09\0x0A";
	ptrdiff_t idx = strchr(in_, ch) - in_;
	
	code = out_[idx];
	ch = GETC(ch);
    } else {
	syntax_error("Expected NMSTART or ':' or Digit, got '%c'\n",
	                                                            ch);
	return ch;
    }
    
    /*
     * Code the _**atto**_ delimiter and the "prefix char".
     */
    octetbuf_push_c(pbuf, SO);
    octetbuf_push_c(pbuf, code);
    if (code == SP) {
        octetbuf_push_c(pbuf, ch); /* The NMSTART char of name */
        ch = GETC(ch);
    }
    
    while (ch != EOF) {
	if (ISNMCHAR(ch)) {
	    octetbuf_push_c(pbuf, ch);
	} else if (ch == lit) {
	    if (brace) { /* Hit the string delimiter! */
	        syntax_error("Unclosed attribute reference "
			"(missing '}').\n");
	    }
	    break;
	} else if (ISSPACE(ch)) {
	    if (brace) {
                syntax_error("SPACE in attribute name discarded.\n");
            } else {
                break;
            }
	} else if (ch == MSSCHAR) {
	    if (brace) {
	        syntax_error("You can't use '%c' in attribute names.\n", ch);
            } else {
                break;
            }
	} else if (brace && ch == '}') {
	    break;
        } else if (brace) {
	    syntax_error("Expected NMCHAR, got '%c'.\n", ch);
	    break;
	} else {
	    break;
        }
	ch = GETC(ch);
    }
    if (brace && ch == '}') {
        ch = GETC(ch);
    }
    
    /*
     * Finish the encoded _attribute substitution_.
     */
	
    octetbuf_push_c(pbuf, NUL); /* Make the name a NTBS. */
    octetbuf_push_c(pbuf, SI);  /* Mark the end of the coded thing. */
    
    return ch;
}

#define P_repl_string(CH, P, D)   P_string((CH), (P), (D), 1)
#define P_attr_val_lit(CH, P, D)  P_string((CH), (P), (D), 0)

int P_string(int ch, octetbuf *pbuf, char delim, int is_repl)
{
    char last = NUL;
    
    if (ch == '"' || ch == '\'') {
        assert(delim == ch);
        ch = GETC(ch);
    } else {
        assert(ch == '{');
        assert(delim == '}');
        ch = GETC(ch);
    }
    while (ch != delim && (last = ch) != NUL) {
	if (ch == MSSCHAR) {
	
	    switch (ch = GETC(ch)) {
	    case MSSCHAR: ch = MSSCHAR; break;
	    case  'n': ch = '\n';  break;
	    case  'r': ch = '\r';  break;
	    case  's': ch =  SP ;  break;
	    case  't': ch = '\t';  break;
	    case  '$': ch = '$' ;  break;
	    case  '{': ch = '{' ;  break;
	    case  '}': ch = '}' ;  break;
	    case  LIT: ch = LIT ; break;
	    case LITA: ch = LITA ; break;
	    default:   octetbuf_push_c(pbuf, MSSCHAR);
	    }
	    if (ch != EOF)
		octetbuf_push_c(pbuf, ch);
	    last = ch, ch = GETC(ch);
	    
	} else if (ch == '$' && is_repl) {
	
	    ch = P_attr_subst(ch, pbuf, delim);
	    
	} else if (ch == EOL) {
	    octetbuf_push_c(pbuf, ch);
	    last = ch, ch = GETC(ch);
	} else {
	    octetbuf_push_c(pbuf, ch);
	    last = ch, ch = GETC(ch);
	}
    }    
    if (!is_repl)
	octetbuf_push_c(pbuf, NUL);
    
    assert(ch == delim);
    ch = GETC(ch);
    return ch;
}


int P_repl_text(int ch, char *repl_text[1])
{
    static octetbuf repl = { 0 };
    unsigned nstrings = 0U;
    
    P_S(ch);
    octetbuf_clear(&repl);
    
    if (ch == '+') {
        octetbuf_push_c(&repl, VT);
        ch = GETC(ch);
    }
    
    while (ch != EOF) {

	P_S(ch);
	
	if (ch == LIT || ch == LITA || ch == '{')
	    ch = P_repl_string(ch, &repl, (ch == '{') ? '}' : ch);
	else
	    break;
	++nstrings;
    }
    
    P_S(ch);
    
    if (ch == '+') {
        octetbuf_push_c(&repl, VT);
        ch = GETC(ch);
    }
    
    if (nstrings > 0U) {
	char *res;
	octetbuf_push_c(&repl, NUL);
	res = octetbuf_dup(&repl);
	repl_text[0] = res;
	return ch;
    } else {
	octetbuf_clear(&repl);
	repl_text[0] = NULL;
	return ch;
    }
}

int P_repl_text_pair(int ch, char *repl_text[2])
{
    repl_text[0] = repl_text[1] = NULL;
    
    P_S(ch);
    if (ch == '-') {
	ch = GETC(ch);
    } else if (ch == '/') {
	;
    } else {
	ch = P_repl_text(ch, repl_text+0);
    }
    
    P_S(ch);
    
    if (ch == '/') {
	ch = GETC(ch);
	P_S(ch);
	if (ch == '-') {
	    ch = GETC(ch);
	} else {
	    ch = P_repl_text(ch, repl_text+1);
	}
    }
    
    return ch;
}


int P_name(int ch, cmark_node_type *pnt, char name[NAMELEN+1], bool fold)
{
    char *p = name;
    cmark_node_type nt;
    
    assert(ISNMSTART(ch));
    
    do {
	*p++ = fold ? toupper(ch) : ch;
	ch = GETC(ch);
    } while (p < name + NAMELEN + 1 && ISNMCHAR(ch));
    
    *p = NUL;
    if (p == name + NAMELEN + 1) {
	syntax_error("\"%s\": Name truncated after NAMELEN = %u "
	                                "characters.\n", name, NAMELEN);
    }
    while (ISNMCHAR(ch))
	ch = GETC(ch);
    
    if (pnt == NULL)
	return ch;
    else
	*pnt = 0;
    /*
     * Look up the "GI" for a CommonMark node type.
     */
     
    for (nt = 1; nodename[nt] != NULL; ++nt)
	if (nodename[nt] != NULL && strcmp(nodename[nt], name) == 0) {
	    *pnt = nt;
	    break;
	}
	    
    if (*pnt == 0)
	syntax_error("\"%s\": Not a CommonMark node type.", name);

    return ch;
}


int P_rni_name(int ch, enum rn_ *prn, char name[NAMELEN+1])
{
    char *p = name;
    enum rn_ rn;
 
    assert(ch == '@');
    
    ch = GETC(ch);
    ch = P_name(ch, NULL, name, true);
    
    if (prn == NULL)
	return ch;
    else
	*prn = 0;
	
    /*
     * Look up the "reserved name".
     */
    for (rn = 1; rn_name[rn] != NULL; ++rn)
	if (strcmp(rn_name[rn], name) == 0) {
	    *prn = rn;
	    break;
	}
    
    if (*prn == 0)
	syntax_error("\"%s\": Unknown reserved name.\n", name);

    return ch;
}

int P_sel(int ch, struct taginfo_ taginfo[1])
{
    char name[NAMELEN+1];
    cmark_node_type nt;
    const int fold = 1;
    unsigned nattr = 0U;
    
    assert(ISNMSTART(ch));
    
    ch = P_name(ch, &nt, name, fold);
    taginfo->nt = nt;
    
    while (ch == '[') {
	bufsize_t name_idx = 0, val_idx = 0;
	
	ch = GETC(ch);
        P_S(ch);
	ch = P_name(ch, NULL, name, false);
	P_S(ch);
	if (ch == '=') {
	    ch = GETC(ch);
	    P_S(ch);
	    if (ch == LIT || ch == LITA) {
		val_idx = octetbuf_size(&text_buf);
		ch = P_attr_val_lit(ch, &text_buf, ch);
	    } else if (ISNMSTART(ch)) {
	        char val[NAMELEN+1];
	        ch = P_name(ch, NULL, val, false);
	        val_idx = octetbuf_size(&text_buf);
	        octetbuf_push_s(&text_buf, val);
	        octetbuf_push_c(&text_buf, NUL);
	    } else {
                syntax_error("Expected name or string, got '%c'\n", ch);
	    }
	    P_S(ch);
	}
	if (ch != ']') {
            syntax_error("Expected ']', got '%c'\n", ch);
	}
	ch = GETC(ch);
	
	name_idx = octetbuf_size(&text_buf);
	octetbuf_push_s(&text_buf, name);
	octetbuf_push_c(&text_buf, NUL);
	taginfo->atts[2*nattr+0] = name_idx;
	taginfo->atts[2*nattr+1] = val_idx;
	++nattr;
    }
    taginfo->atts[2*nattr+0] = NULLIDX;
    
    return ch;
}

int P_cdata_flag(int ch, bool *is_cdata)
{
    static const char s_cdata[]  = "CDATA",
                      s_rcdata[] = "RCDATA";

    P_S(ch);
    if (ISNMSTART(ch)) {
        char nmbuf[NAMELEN+1];

        ch = P_name(ch, NULL, nmbuf, true);
        if (strcmp(nmbuf, s_cdata) == 0)
            *is_cdata = true;
        else
            syntax_error("Expected 'CDATA', got '%s'\n", nmbuf);
    }
    return ch;
}

int P_sel_rule(int ch)
{
    struct taginfo_ taginfo[1];
    char *repl_texts[2];
    bool is_cdata = false;

    if (!ISNMSTART(ch)) {
        syntax_error("Expected name, got '%c'\n", ch);
        ch = GETC(ch);
        return ch;
    }

    ch = P_sel(ch, taginfo);
    ch = P_cdata_flag(ch, &is_cdata);
    ch = P_repl_text_pair(ch, repl_texts);
            
    set_repl(taginfo, repl_texts, is_cdata);

    return ch;
}

int P_rn_rule(int ch)
{
    char name[NAMELEN+1];
    char *repl_text[1];
    enum rn_ rn;
    
    assert(ch == '@');
    
    ch = P_rni_name(ch, &rn, name);
    ch = P_repl_text(ch, repl_text);
    rn_repl[rn] = repl_text[0];
    return ch;
}

int P_comment(int ch, const char lit)
{
    assert(ch == '/' && lit == '*');
       
    ch = GETC(ch);
    assert(ch == '*');
    
    while ((ch = GETC(ch)) != EOF)
	if (ch == '*' && PEEK() == '/') {
	    break;
	}

    assert(ch == '*' || ch == EOF);
    ch = GETC(ch);
    assert(ch == '/' || ch == EOF);
    ch = GETC(ch);
	    
    return ch;
}

int P_repl_defs(int ch)
{
    while (ch != EOF) {
	P_S(ch);
	
	if (ch == EOF)
	    break;

	if (ch == '@')
	    ch = P_rn_rule(ch);
	else if (ch == '/' && PEEK() == '*')
	    ch = P_comment(ch, '*');
	else
	    ch = P_sel_rule(ch);
    }
    return ch;
}

/*--------------------------------------------------------------------*/

/*
 * Loading (ie parsing and interpreting) a Replacement Definition file.
 */
 
void load_repl_defs(FILE *fp)
{
    int ch;
    
    if (fp == NULL) return;
    
    replfp = fp;
    
    /*
     * Move to start of first line.
     */
    COUNT_EOL(EOL); /* 
    
    /*
     * Initializing character buffers. Note that NULLIDX acts as
     * a sentinel, thus we push a NUL in both buffers so that
     * index 0 (ie NULLIDX) is occupied and "out of use".
     */
    octetbuf_init(&text_buf, 2048U);
    octetbuf_push_c(&text_buf, NUL);
    
    octetbuf_init(&attr_buf, ATTSPLEN);
    octetbuf_push_c(&attr_buf, NUL);
    
    /*
     * The name-index and value-index buffers are simply empty
     * at the beginning.
     */
    octetbuf_init(&nameidx_buf, ATTCNT * sizeof(nameidx_t));
    octetbuf_init(&validx_buf,  ATTCNT * sizeof(validx_t));

    /*
     * Parse and process replacement definitions. All parsing
     * results are stored in the four buffers initialized above.
     */
    ch = GETC(ch);
    ch = P_repl_defs(ch);
    assert(ch == EOF);
    
    fclose(replfp);
    replfp = NULL;
}


/*--------------------------------------------------------------------*/

/*
 * Find and open a Replacement Definition file.
 *
 * A NULL argument refers to the "default" repl def file.
 */
 
static const char *repl_dir = NULL;
static const char *repl_default = NULL;
    
#ifdef _WIN32
#define DIRSEP "\\"
#else
#define DIRSEP "/"
#endif
static const char dirsep[] = DIRSEP;

bool is_relpath(const char *pathname)
{
    if (pathname[0] == dirsep[0])
	return 0;
#ifdef _WIN32
    /*
     * A DOS-style path starting with a "drive letter" like "C:..."
     * is taken to be "absolute" - although it technically can be 
     * relative (to the cwd for this drive): we would have to
     * _insert_ the path prefix after the ":", not simply prefix it.
     */
    if ( (('A' <= pathname[0] && pathname[0] <= 'Z') || 
          ('a' <= pathname[0] && pathname[0] <= 'z')) && 
         pathname[1] == ':' )
    {
	return 0;
    }
#endif
    return 1;
}

/*
 * Find and open a replacement definition file.
 *
 * If NULL is given as the filename, the "default repl def" is used
 * (specified by environment).
 *
 * The "verbose" argument can name a text stream (ie stdout or stderr)
 * into which report is written on the use of environment variables and
 * which replacement file pathnames were tried etc. (This is used
 * in `usage()`, invoked eg by the `--help` option);
 */
 
FILE *open_repl_file(const char *repl_filename, FILE *verbose)
{
    FILE *fp;
    bool is_rel;
    
    
    if (repl_dir == NULL)     repl_dir = getenv(REPL_DIR_VAR);
    if (repl_default == NULL) repl_default = getenv(REPL_DEFAULT_VAR);
    
    if (verbose) {
	putc(EOL, verbose);
	fprintf(verbose, "%s =\n\t\"%s\"\n", REPL_DIR_VAR,
		(repl_dir) ? repl_dir : "<not set>");
	fprintf(verbose, "%s =\n\t\"%s\"\n", REPL_DEFAULT_VAR,
		(repl_default) ? repl_default : "<not set>");
	putc(EOL, verbose);
    }
    /*
     * Passing in NULL means: use the default replacement definition.
     */
    if (repl_filename == NULL && (repl_filename = repl_default) == NULL)
	if (verbose) {
	    fprintf(verbose, "No default replacement file!\n");
	    return NULL;
	} else
	    error("No replacement definition file specified, "
	          "nor a default - giving up!\n");
	
    /*
     * First try the given filename literally.
     */
    filename = repl_filename;
    assert(filename != NULL);
    is_rel = is_relpath(filename);
    
    if (verbose) fprintf(verbose, "Trying\t\"%s%s\" ... ",
                         is_rel ? "." DIRSEP : "" , filename);
    fp = fopen(filename, "r");
    if (verbose) fprintf(verbose, "%s.\n", (fp) ? "ok" : "failed");

    
    /*
     * Otherwise, try a *relative* pathname with the REPL_DIR_VAR
     * environment variable.
     */
    if (fp == NULL && repl_dir != NULL) {
	size_t fnlen, rdlen;
	int trailsep;
	char *pathname;

	if (is_relpath(filename)) {
	    fnlen = strlen(filename);
	    rdlen = strlen(repl_dir);
	    trailsep = rdlen > 0U && (repl_dir[rdlen-1U] == dirsep[0]);
	    pathname = malloc(rdlen + !trailsep + fnlen + 16);
	    sprintf(pathname, "%s%s%s",
		repl_dir, dirsep+trailsep, filename);
		
	    if (verbose) fprintf(verbose, "Trying\t\"%s\" ... ",
	                                                      pathname);
	    fp = fopen(pathname, "r");
	    if (verbose) fprintf(verbose, "%s.\n",
	                                (fp == NULL) ? "failed" : "ok");
	                                
	    if (fp != NULL) filename = pathname;
	    else free(pathname);
	}
    }
    
    /*
     * If we **still** have no replacement definition file, it is
     * time to give up.
     */
    if (fp == NULL) {
	if (verbose) fprintf(verbose, "Can't open \"%s\": %s.\n",
	                                     filename, strerror(errno));
	else
	    error("Can't open replacement file \"%s\": %s.", filename,
	                                               strerror(errno));
    }
    return fp;
}

/*====================================================================*/

/*
 * gen_document -- Driver for the replacement backend
 *
 *  1. Start the outermost "universal" pseudo-element.
 *  2. Output the replacement text for #PROLOG, if any.
 *  3. Render the document into the given ESIS API callbacks.
 *  4. Output the replacement text for #EPILOG, if any.
 *  5. End the outermost pseudo-element.
 *  6. [Not needed]: Clean up the attribute stack.
 */
 
static void gen_document(cmark_node *document,
                         cmark_option_t options,
                         ESIS_Port *to)
{
    const ESIS_CB *esis_cb = to->cb;
    ESIS_UserData  esis_ud = to->ud;
    int bol[2];
    const cmark_node_type none = CMARK_NODE_NONE;
    
    DO_START(CMARK_NODE_NONE);
    bol[0] = bol[1] = 0;
    
    if (rn_repl[RN_PROLOG] != NULL) {
	put_repl(rn_repl[RN_PROLOG]);
    }

    cmark_render_esis(document, to);

    if (rn_repl[RN_EPILOG] != NULL) {
	put_repl(rn_repl[RN_EPILOG]);
    }
    DO_END(CMARK_NODE_NONE);
}


/*====================================================================*/

/*
 * Preprocessor.
 */

esc_state *esp;

const char *prep_cb(const char *arg)
{
    static octetbuf chars = { 0 };
    static octetbuf strings = { 0 };
    static char id[128];
    size_t n, k;
    bool isref = strncmp(arg, "ref(", 4) == 0;
    bool isdef = strncmp(arg, "def(", 4) == 0;
    
    if (!isref && !isdef) return NULL;

    if (strlen(arg) >= sizeof id - 1U) return arg;
    
    strcpy(id, arg += 4);
    for (k = 0; id[k] != NUL; ++k)
	if (id[k] == ')')
	    id[k] = NUL;
    
    n = octetbuf_size(&strings)/sizeof(char *);
    for (k = 0; k < n; ++k) {
	const char **pp = octetbuf_elem_at(&strings, k, sizeof *pp);
	const char *p = *pp;
	if (strcmp(p, id) == 0)
	    break;
    }
    if (k == n) {
	octetidx_t i = octetbuf_push_s(&chars, id);
	const char *s = octetbuf_at(&chars, i);
	octetbuf_push_c(&chars, NUL);
	octetbuf_push_back(&strings, &s, sizeof s);
    }
    if (isref)
	snprintf(id, sizeof id, "#ref%002u", (unsigned)k);
    else
	snprintf(id, sizeof id,
	    "<a name=\"ref%03u\" id=\"ref%03u\"></a>",
	                                      (unsigned)k, (unsigned)k);
    return id;
}


int prep_init(const char *dgrfile)
{
    FILE *fp = NULL;
    
    if (dgrfile == NULL)
	dgrfile = getenv(DIGRAPH_VAR);
    if (dgrfile == NULL)
	dgrfile = DIGRAPH_PATH;

    if (dgrfile != NULL && (fp = fopen(dgrfile, "r")) == NULL)
	error("Can't open \"%s\": %s\n.", dgrfile, strerror(errno));
	
    esp = esc_create(fp);
    fclose(fp);
    
    esc_callback(esp, prep_cb);
    esc_set_escape(esp, '\\');
    esc_set_subst(esp,  '$');
    return 0;
}

size_t prep(char *p, size_t n, FILE *fp)
{
    return esc_fsubst(esp, p, n, fp);
}

/*====================================================================*/

int parse_cmark(FILE *from, ESIS_Port *to, cmark_option_t options,
                                                     const char *meta[])
{
    static cmark_parser *parser = NULL;
    
    static bool in_header = true;
    static char buffer[8*BUFSIZ];
    
    size_t bytes;
    
    if (parser == NULL)
	parser = cmark_parser_new(options);
    
    if (from != NULL)
	while ((bytes = prep(buffer, sizeof buffer,from)) > 0) {
	    /*
	     * Read and parse the input file block by block.
	     */
	    size_t hbytes = 0U;

	    if (in_header) {
		int imeta;
		const ESIS_CB *cb = to->cb;
		ESIS_UserData  ud = to->ud;

		hbytes = do_meta_lines(buffer, sizeof buffer, to);

		/*
		 * Override meta-data from meta-lines with meta-data
		 * given in command-line option arguments, eg `--title`.
		 */
		if (meta != NULL)
		    for (imeta = 0; meta[2*imeta] != NULL; ++imeta)
			cb->attr(ud, meta[2*imeta+0], meta[2*imeta+1],
			                                           NTS);

		in_header = false;
	    }

	    assert(hbytes <= bytes);
	    
	    if (hbytes < bytes)
		cmark_parser_feed(parser, buffer + hbytes, 
		                                        bytes - hbytes);
	    else
		break;
	}
    else {
	/*
	 * Finished parsing, generate document content into
	 * ESIS port.
	 */
        cmark_node   *document;
        
	document = cmark_parser_finish(parser);
	cmark_parser_free(parser); parser = NULL;

	gen_document(document, options, to);

	cmark_node_free(document);
    }
    
    return 0;
}

/*== Main function ===================================================*/

void usage()
{
    const char *dgrfile = getenv(DIGRAPH_VAR);
    
    printf("Usage:   cm2doc [FILE*]\n\n");
    printf("Options:\n");
    printf("  -t --title TITLE Set the document title\n");
    printf("  -c --css CSS     Set the document style sheet to CSS\n");
    printf("  -r --repl file   Use replacement definition file\n");
    printf("  --sourcepos      Include source position attribute\n");
    printf("  --hardbreaks     Treat newlines as hard line breaks\n");
    printf("  --safe           Suppress raw HTML and dangerous URLs\n");
    printf("  --smart          Use smart punctuation\n");
    printf("  --normalize      Consolidate adjacent text nodes\n");
    printf("  --rast           Output RAST format "
                                              "(ISO/IEC 13673:2000)\n");
    printf("  --help, -h       Print usage information\n");
    printf("  --version        Print version\n");
    
    printf("\nDigraph file:\n\n");
    printf("%s =\n\t\"%s\"\n", DIGRAPH_VAR,
		(dgrfile != NULL) ? dgrfile : "<not set>");
    if (dgrfile != NULL) {
        FILE *fp = fopen(dgrfile, "r");
        
        printf("\nTrying \"%s\" ... %s.\n", dgrfile,
                (fp != NULL) ? (fclose(fp), "ok") : "failed");
    }

    printf("\nReplacement files:\n");
    open_repl_file(NULL, stdout);
}


int main(int argc, char *argv[])
{
    const char *username         = "N.N.";
    const char *title_arg        = NULL;
    const char *css_arg          = NULL;
    const char *dgr_arg          = NULL;

    cmark_option_t cmark_options = CMARK_OPT_NORMALIZE;
    unsigned       rast_options  = 0U;
    bool doing_rast              = false;
    unsigned repl_file_count     = 0U;
    
    ESIS_Port *port   = NULL;
    FILE      *infp   = stdin;
    FILE      *outfp  = stdout;
    FILE      *replfp = NULL;
    
    const char *meta[42], **pmeta = meta;
    time_t now;
    int argi;
    
    meta[0] = NULL;
    
    if ( (username = getenv("LOGNAME"))   != NULL ||
         (username = getenv("USERNAME"))  != NULL )
	strncpy(default_creator, username, sizeof default_creator -1U);
	
    time(&now);
    strftime(default_date, sizeof default_date, "%Y-%m-%d",
                                                          gmtime(&now));
	    
    for (argi = 1; argi < argc && argv[argi][0] == '-'; ++argi) {
	if (strcmp(argv[argi], "--version") == 0) {
	    printf("cmark %s", CMARK_VERSION_STRING);
	    printf(" ( %s %s )\n", cmark_repourl, cmark_gitident);
	    printf(" cmark:  (C) 2014, 2015 John MacFarlane\n");
	    printf(" cm2doc: (C) 2016 M. Hofmann\n");
	    exit(EXIT_SUCCESS);
	} else if ((strcmp(argv[argi], "--repl") == 0) ||
	    (strcmp(argv[argi], "-r") == 0)) {
	    const char *filename = argv[++argi];
	    load_repl_defs(open_repl_file(filename, NULL));
	    ++repl_file_count;
	} else if (strcmp(argv[argi], "--rast") == 0) {
	    doing_rast = true;
	} else if (strcmp(argv[argi], "--rasta") == 0) {
	    doing_rast = true;
	    rast_options |= RAST_ALL;
	} else if ((strcmp(argv[argi], "--title") == 0) ||
	    (strcmp(argv[argi], "-t") == 0)) {
		title_arg = argv[++argi];
	} else if ((strcmp(argv[argi], "--css") == 0) ||
	    (strcmp(argv[argi], "-c") == 0)) {
		css_arg = argv[++argi];
	} else if ((strcmp(argv[argi], "--digr") == 0) ||
	    (strcmp(argv[argi], "-d") == 0)) {
		dgr_arg = argv[++argi];
	} else if (strcmp(argv[argi], "--sourcepos") == 0) {
	    cmark_options |= CMARK_OPT_SOURCEPOS;
	} else if (strcmp(argv[argi], "--hardbreaks") == 0) {
	    cmark_options |= CMARK_OPT_HARDBREAKS;
	} else if (strcmp(argv[argi], "--smart") == 0) {
	    cmark_options |= CMARK_OPT_SMART;
	} else if (strcmp(argv[argi], "--safe") == 0) {
	    cmark_options |= CMARK_OPT_SAFE;
	} else if (strcmp(argv[argi], "--normalize") == 0) {
	    cmark_options |= CMARK_OPT_NORMALIZE;
	} else if (strcmp(argv[argi], "--validate-utf8") == 0) {
	    cmark_options |= CMARK_OPT_VALIDATE_UTF8;
	} else if ((strcmp(argv[argi], "--help") == 0) ||
	    (strcmp(argv[argi], "-h") == 0)) {
		usage();
		exit(EXIT_SUCCESS);
	} else if (argv[argi][1] == NUL) {
	    ++argi;
	    break;
	} else {
	    usage();
	    error("\"%s\": Invalid option.\n", argv[argi]);
	}
    }
    
    prep_init(dgr_arg);
    
#if 0
    {
	static const char *const notations[] = {
	    "HTML",	"Z",    "EBNF",	    "VDM",
	    "C90",	"C11",	"ASN.1",    "Ada95",
	    NULL
	};
	int i;
	
	for (i = 0; notations[i] != NULL; ++i)
	    register_notation(notations[i], NTS);
    }
#endif
    
    /*
     * If no replacement file was mentioned (and processed),
     * try using the default replacement file given in the
     * environment.
     */
    if (doing_rast)
	if (repl_file_count > 0U)
	    error("Can't use RAST with replacement files.\n");
	else
	    /* Do RAST. */
	    port = generate_rast(outfp, rast_options);
    else {
	if (repl_file_count == 0U)
	    /* Succeed or die. */
	    load_repl_defs(open_repl_file(NULL, NULL));
	    
	if (title_arg != NULL) {
	    *pmeta++ = META_DC_TITLE;
	    *pmeta++ = title_arg;
	}
	*pmeta++ = META_CSS;
	*pmeta++ = (css_arg) ? css_arg : DEFAULT_CSS;
	*pmeta++ = "lang";
	*pmeta++ = "en"; /* TODO command-line option "--lang" */
	*pmeta = NULL;
	
	port = generate_repl(outfp, 0U);
    }

    /*
     * Loop through the input files.
     */
    switch (argc - argi) do {
    default:
	if ((outfp = freopen(argv[argi], "r", stdin)) == NULL)
	    error("Can't open \"%s\": %s\n", argv[argi],
	                                               strerror(errno));
    case 0:
	parse_cmark(outfp, port, cmark_options, meta);
    } while (++argi < argc);
    
    parse_cmark(NULL, port, 0U, NULL);


    return EXIT_SUCCESS;
}

/*== EOF ============================ vim:tw=72:sts=0:et:cin:fo=croq:sta
                                               ex: set ts=8 sw=4 ai : */
