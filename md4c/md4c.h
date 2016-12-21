/*
 * MD4C: Markdown parser for C
 * (http://github.com/mity/md4c)
 *
 * Copyright (c) 2016 Martin Mitas
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef MD4C_MARKDOWN_H
#define MD4C_MARKDOWN_H

#ifdef __cplusplus
    extern "C" {
#endif


#define MD_VERSION_MAJOR        0
#define MD_VERSION_MINOR        2
#define MD_VERSION_RELEASE      0


/* Magic to support UTF-16. */
#if defined MD4C_USE_UTF16
    #ifdef _WIN32
        #include <wchar.h>
        typedef wchar_t MD_CHAR;
    #else
        #error MD4C_USE_UTF16 is only upported on Windows.
    #endif
#else
    typedef char            MD_CHAR;
#endif

typedef unsigned MD_SIZE;
typedef unsigned MD_OFFSET;


/* Block represents a part of document hierarchy structure like a paragraph
 * or list item. */
typedef enum MD_BLOCKTYPE_tag MD_BLOCKTYPE;
enum MD_BLOCKTYPE_tag {
    /* <body>...</body> */
    MD_BLOCK_DOC = 0,

    /* <blockquote>...</blockquote> */
    MD_BLOCK_QUOTE,

    /* <ul>...</ul>
     * Detail: Structure MD_BLOCK_UL_DETAIL. */
    MD_BLOCK_UL,

    /* <ol>...</ol>
     * Detail: Structure MD_BLOCK_OL_DETAIL. */
    MD_BLOCK_OL,

    /* <li>...</li> */
    MD_BLOCK_LI,

    /* <hr> */
    MD_BLOCK_HR,

    /* <h1>...</h1> (for levels up to 6)
     * Detail: Structure MD_BLOCK_H_DETAIL. */
    MD_BLOCK_H,

    /* <pre><code>...</code></pre>
     * Note the text lines within code blocks are terminated with '\n'
     * instead of explicit MD_TEXT_BR. */
    MD_BLOCK_CODE,

    /* Raw HTML block. This itself does not correspond to any particular HTML
     * tag. The contents of it _is_ raw HTML source intended to be put
     * in verbatim form to the HTML output. */
    MD_BLOCK_HTML,

    /* <p>...</p> */
    MD_BLOCK_P,

    /* <table>...</table> and its contents.
     * Detail: Structure MD_BLOCK_TD_DETAIL (used with MD_BLOCK_TH and MD_BLOCK_TD)
     * Note all of these are used only if extension MD_FLAG_TABLES is enabled. */
    MD_BLOCK_TABLE,
    MD_BLOCK_THEAD,
    MD_BLOCK_TBODY,
    MD_BLOCK_TR,
    MD_BLOCK_TH,
    MD_BLOCK_TD
};

/* Span represents an in-line piece of a document which should be rendered with
 * the same font, color and other attributes. A sequence of spans forms a block
 * like paragraph or list item. */
typedef enum MD_SPANTYPE_tag MD_SPANTYPE;
enum MD_SPANTYPE_tag {
    /* <em>...</em> */
    MD_SPAN_EM,

    /* <strong>...</strong> */
    MD_SPAN_STRONG,

    /* <a href="xxx">...</a>
     * Detail: Structure MD_SPAN_A_DETAIL. */
    MD_SPAN_A,

    /* <img src="xxx">...</a>
     * Detail: Structure MD_SPAN_IMG_DETAIL.
     * Note: Image text can contain nested spans and even nested images.
     * If rendered into ALT attribute of HTML <IMG> tag, it's responsibility
     * of the renderer to deal with it.
     */
    MD_SPAN_IMG,

    /* <code>...</code> */
    MD_SPAN_CODE
};

/* Text is the actual textual contents of span. */
typedef enum MD_TEXTTYPE_tag MD_TEXTTYPE;
enum MD_TEXTTYPE_tag {
    /* Normal text. */
    MD_TEXT_NORMAL = 0,

    /* NULL character. CommonMark requires replacing NULL character with
     * the replacement char U+FFFD, so this allows caller to do that easily. */
    MD_TEXT_NULLCHAR,

    /* Line breaks.
     * Note these are not sent from blocks with verbatim output (MD_BLOCK_CODE
     * or MD_BLOCK_HTML). In such cases, '\n' is part of the text itself. */
    MD_TEXT_BR,         /* <br> (hard break) */
    MD_TEXT_SOFTBR,     /* '\n' in source text where it is not semantically meaningful (soft break) */

    /* Entity.
     * (a) Named entity, e.g. &nbsp; 
     *     (Note MD4C does not have a list of known entities.
     *     Anything matching the regexp /&[A-Za-z][A-Za-z0-9]{1,47};/ is
     *     treated as a named entity.)
     * (b) Numerical entity, e.g. &#1234;
     * (c) Hexadecimal entity, e.g. &#x12AB;
     *
     * As MD4C is mostly encoding agnostic, application gets the verbatim
     * entity text into the MD_RENDERER::text_callback(). */
    MD_TEXT_ENTITY,

    /* Text in a code block (inside MD_BLOCK_CODE) or inlined code (`code`).
     * If it is inside MD_BLOCK_CODE, it includes spaces for indentation and
     * '\n' for new lines. MD_TEXT_BR and MD_TEXT_SOFTBR are not sent for this
     * kind of text. */
    MD_TEXT_CODE,

    /* Text is a raw HTML. If it is contents of a raw HTML block (i.e. not
     * an inline raw HTML), then MD_TEXT_BR and MD_TEXT_SOFTBR are not used.
     * The text contains verbatim '\n' for the new lines. */
    MD_TEXT_HTML
};


/* Alignment enumeration. */
typedef enum MD_ALIGN_tag MD_ALIGN;
enum MD_ALIGN_tag {
    MD_ALIGN_DEFAULT = 0,   /* When unspecified. */
    MD_ALIGN_LEFT,
    MD_ALIGN_CENTER,
    MD_ALIGN_RIGHT
};


/* String attribute.
 *
 * This wraps strings which are outside of a normal text flow and which are
 * propagated within various detailed structures, but which still may contain
 * string portions of different types like e.g. entities.
 *
 * So, for example, lets consider an image has a title attribute string
 * set to "foo &quot; bar". (Note the string size is 14.)
 *
 * Then:
 *  -- [0]: "foo "   (substr_types[0] == MD_TEXT_NORMAL; substr_offsets[0] == 0)
 *  -- [1]: "&quot;" (substr_types[1] == MD_TEXT_ENTITY; substr_offsets[1] == 4)
 *  -- [2]: " bar"   (substr_types[2] == MD_TEXT_NORMAL; substr_offsets[2] == 10)
 *  -- [3]: (n/a)    (n/a                              ; substr_offsets[3] == 14)
 *
 * Note that these conditions are guaranteed:
 *  -- substr_offsets[0] == 0
 *  -- substr_offsets[LAST+1] == size
 *  -- Only MD_TEXT_NORMAL and MD_TEXT_ENTITY substrings can appear.
 */
typedef struct MD_ATTRIBUTE_tag MD_ATTRIBUTE;
struct MD_ATTRIBUTE_tag {
    const MD_CHAR* text;
    MD_SIZE size;
    const MD_TEXTTYPE* substr_types;
    const MD_OFFSET* substr_offsets;
};


/* Detailed info for MD_BLOCK_UL_DETAIL. */
typedef struct MD_BLOCK_UL_DETAIL_tag MD_BLOCK_UL_DETAIL;
struct MD_BLOCK_UL_DETAIL_tag {
    int is_tight;           /* Non-zero if tight list, zero of loose. */
    MD_CHAR mark;           /* Item bullet character in MarkDown source of the list, e.g. '-', '+', '*'. */
};

/* Detailed info for MD_BLOCK_OL_DETAIL. */
typedef struct MD_BLOCK_OL_DETAIL_tag MD_BLOCK_OL_DETAIL;
struct MD_BLOCK_OL_DETAIL_tag {
    unsigned start;         /* Start index of the ordered list. */
    int is_tight;           /* Non-zero if tight list, zero of loose. */
    MD_CHAR mark_delimiter; /* Character delimiting the item marks in MarkDown source, e.g. '.' or ')' */
};

/* Detailed info for MD_BLOCK_H. */
typedef struct MD_BLOCK_H_DETAIL_tag MD_BLOCK_H_DETAIL;
struct MD_BLOCK_H_DETAIL_tag {
    unsigned level;         /* Header level (1 - 6) */
};

/* Detailed info for MD_BLOCK_CODE. */
typedef struct MD_BLOCK_CODE_DETAIL_tag MD_BLOCK_CODE_DETAIL;
struct MD_BLOCK_CODE_DETAIL_tag {
    MD_ATTRIBUTE info;
    MD_ATTRIBUTE lang;
};

/* Detailed info for MD_BLOCK_TH and MD_BLOCK_TD. */
typedef struct MD_BLOCK_TD_DETAIL_tag MD_BLOCK_TD_DETAIL;
struct MD_BLOCK_TD_DETAIL_tag {
    MD_ALIGN align;
};

/* Detailed info for MD_SPAN_A. */
typedef struct MD_SPAN_A_DETAIL_tag MD_SPAN_A_DETAIL;
struct MD_SPAN_A_DETAIL_tag {
    MD_ATTRIBUTE href;
    MD_ATTRIBUTE title;
};

/* Detailed info for MD_SPAN_IMG. */
typedef struct MD_SPAN_IMG_DETAIL_tag MD_SPAN_IMG_DETAIL;
struct MD_SPAN_IMG_DETAIL_tag {
    MD_ATTRIBUTE src;
    MD_ATTRIBUTE title;
};


/* Flags specifying Markdown dialect.
 *
 * By default (when MD_RENDERER::flags == 0), we follow CommonMark specification.
 * The following flags may allow some extensions or deviations from it.
 */
#define MD_FLAG_COLLAPSEWHITESPACE          0x0001  /* In MD_TEXT_NORMAL, collapse non-trivial whitespace into single ' ' */
#define MD_FLAG_PERMISSIVEATXHEADERS        0x0002  /* Do not require space in ATX headers ( ###header ) */
#define MD_FLAG_PERMISSIVEURLAUTOLINKS      0x0004  /* Recognize URLs as autolinks even without '<', '>' */
#define MD_FLAG_PERMISSIVEEMAILAUTOLINKS    0x0008  /* Recognize e-mails as autolinks even without '<', '>' and 'mailto:' */
#define MD_FLAG_PERMISSIVEAUTOLINKS         (MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_PERMISSIVEEMAILAUTOLINKS)
#define MD_FLAG_NOINDENTEDCODEBLOCKS        0x0010  /* Disable indented code blocks. (Only fenced code works.) */
#define MD_FLAG_NOHTMLBLOCKS                0x0020  /* Disable raw HTML blocks. */
#define MD_FLAG_NOHTMLSPANS                 0x0040  /* Disable raw HTML (inline). */
#define MD_FLAG_NOHTML                      (MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS)
#define MD_FLAG_TABLES                      0x0100  /* Enable tables extension. */

/* Renderer structure.
 */
typedef struct MD_RENDERER_tag MD_RENDERER;
struct MD_RENDERER_tag {
    /* Caller-provided rendering callbacks.
     *
     * For some block/span types, more detailed information is provided in a
     * type-specific structure pointed by the argument 'detail'.
     *
     * The last argument of all callbacks, 'userdata', is just propagated from
     * md_parse() and is available for any use by the application.
     *
     * Note any strings provided to the callbacks as their arguments or as
     * members of any detail structure are generally not zero-terminated.
     * Application has take the respective size information into account.
     *
     * Callbacks may abort further parsing of the document by returning non-zero.
     */
    int (*enter_block)(MD_BLOCKTYPE /*type*/, void* /*detail*/, void* /*userdata*/);
    int (*leave_block)(MD_BLOCKTYPE /*type*/, void* /*detail*/, void* /*userdata*/);

    int (*enter_span)(MD_SPANTYPE /*type*/, void* /*detail*/, void* /*userdata*/);
    int (*leave_span)(MD_SPANTYPE /*type*/, void* /*detail*/, void* /*userdata*/);

    int (*text)(MD_TEXTTYPE /*type*/, const MD_CHAR* /*text*/, MD_SIZE /*size*/, void* /*userdata*/);

    /* Debug callback. Optional (may be NULL).
     *
     * If provided and something goes wrong, this function gets called.
     * This is intended for debugging and problem diagnosis for developers;
     * it is not intended to provide any errors suitable for displaying to an
     * end user.
     */
    void (*debug_log)(const char* /*msg*/, void* /*userdata*/);

    /* Dialect options. Bitmask of MD_FLAG_xxxx values.
     */
    unsigned flags;
};


/* Parse the Markdown document stored in the string 'text' of size 'size'.
 * The renderer provides callbacks to be called during the parsing so the
 * caller can render the document on the screen or convert the Markdown
 * to another format.
 *
 * Zero is returned on success. If a runtime error occurs (e.g. a memory
 * fails), -1 is returned. If the processing is aborted due any callback
 * returning non-zero, md_parse() the return value of the callback is returned.
 */
int md_parse(const MD_CHAR* text, MD_SIZE size, const MD_RENDERER* renderer, void* userdata);


#ifdef __cplusplus
    }  /* extern "C" { */
#endif

#endif  /* MD4C_MARKDOWN_H */
