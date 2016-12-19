/* escape.h */
#ifndef ESCAPE_H_INCLUDED
#define ESCAPE_H_INCLUDED

#include <stddef.h>
#include <stdio.h>

#define ESC_ERR_UCS            1
#define ESC_ERR_DGR_FILE       2
#define ESC_ERR_DGR_LINETYPE   3
#define ESC_ERR_DGR_LINEFORMAT 4

typedef struct esc_state_ esc_state;

typedef const char *(*esc_cb)(const char *);

esc_state *esc_create(FILE *);
void       esc_free(esc_state *);

int     esc_set_escape(esc_state *, int ch);
int     esc_set_subst(esc_state *, int ch);

esc_cb  esc_callback(esc_state *, esc_cb);

void    esc_nmstart(esc_state *, const char *);
void    esc_nmchar(esc_state *, const char *);

int	esc_define(esc_state *,
			const char ch[2], const long ucsdef[]);

size_t  esc_expand(esc_state *,
			char u8def[], const char ch[2]);

size_t  esc_fsubst(esc_state *,
			char buf[], size_t nbuf, FILE *in);

size_t  esc_ssubst(esc_state *,
			char buf[], size_t nbuf, const char *);



#endif/*ESCAPE_H_INCLUDED*/
