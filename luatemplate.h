/*
 * Copyright (C) 2011 - 2021 Micro Systems Marc Balmer, CH-5073 Gipf-Oberfrick.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __LUATEMPLATE_H__
#define __LUATEMPLATE_H__

#ifdef LT_DEBUG
#define LINEBUFSIZ	64
#endif

#define	TEMPLATE_CONTEXT_METATABLE	"Lua template rendering context"

enum lt_escapes {
	e_none = 0,
	e_html,
	e_xml,
	e_latex,
	e_url
};

struct lt_state {
#ifdef LT_DEBUG
	int		 tline;	/* current line in template */
	int		 lline;	/* current line in produced Lua code */
	int		*lines;	/* To find template line number */
	int		 linemax;	/*  size of lines */
#endif
};

struct lt_include {
	SLIST_ENTRY(lt_include) next;
	char *fnam;
};
SLIST_HEAD(lt_include_head, lt_include);

struct render_context {
	struct lt_include_head	ihead;
	int			debug;
};

extern int process_file(lua_State *L, const char *fnam,
    struct lt_include_head *ihead, int print);
extern const char *reader(lua_State *L, char *, struct lt_state *,
    struct lt_include_head *includes, const char *, int);
#ifdef LT_DEBUG
extern const char *lt_errmsg(lua_State *L);
#endif
extern const char *lt_escape(int escape, char c);

#endif /* __LUATEMPLATE_H__ */
