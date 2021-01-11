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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/queue.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__) && !defined(LIBBSD_OVERLAY)
#include <bsd/bsd.h>
#endif

#include <lua.h>
#include <lauxlib.h>

#include "buffer.h"
#include "luatemplate.h"

enum lt_states {
	s_startup = 0,
	s_initial,
	s_output,
	s_code,
	s_expression,
	s_instruction,
	s_terminate,
	s_error
};

struct lt_escape_entity {
	char c;
	const char *escape;
};

/* XXX Merge HTML and XML table? */
static struct lt_escape_entity html_escape[] = {
	{ '&',	"&amp;" },
	{ '<',	"&lt;" },
	{ '>',	"&gt;" },
	{ '"',	"&#034;" },
	{ '\'',	"&#039;" },
	{ '\0', NULL }
};

static struct lt_escape_entity xml_escape[] = {
	{ '&',	"&amp;" },
	{ '<',	"&lt;" },
	{ '>',	"&gt;" },
	{ '"',	"&quot;" },
	{ '\'',	"&apos;" },
	{ '\0', NULL }
};

static struct lt_escape_entity latex_escape[] = {
	{ '&',	"\\&" },
	{ '$',	"\\$" },
	{ '\\',	"$\\backslash$" },
	{ '_',	"\\_" },
	{ '<',	"$<$" },
	{ '>',	"$>$" },
	{ '%',	"\\%" },
	{ '#',	"\\#" },
	{ '^',	"$^$" },
	{ '\0', NULL }
};

static struct lt_escape_entity url_escape[] = {
	{ ' ',	"%20"},
	{ '<',	"%3C"},
	{ '>',	"%3E"},
	{ '#',	"%23"},
	{ '%',	"%25"},
	{ '{',	"%7B"},
	{ '}',	"%7D"},
	{ '|',	"%7C"},
	{ '\\',	"%5C"},
	{ '^',	"%5E"},
	{ '~',	"%7E"},
	{ '[',	"%5B"},
	{ ']',	"%5D"},
	{ '`',	"%60"},
	{ ';',	"%3B"},
	{ '/',	"%2F"},
	{ '?',	"%3F"},
	{ ':',	"%3A"},
	{ '@',	"%40"},
	{ '=',	"%3D"},
	{ '&',	"%26"},
	{ '$',	"%24"},
	{ '\0',	NULL }
};

const char *
lt_escape(int escape, char c)
{
	struct lt_escape_entity *esc;

	switch (escape) {
	case e_html:
		esc = html_escape;
		break;
	case e_xml:
		esc = xml_escape;
		break;
	case e_url:
		esc = url_escape;
		break;
	case e_latex:
		esc = latex_escape;
		break;
	default:
		return NULL;
	}

	for (; esc->escape != NULL; esc++)
		if (esc->c == c)
			break;
	return esc->escape;
}

#ifdef LT_DEBUG
void
lt_pusherrmsg(lua_State *L, struct lt_state *s)
{
	static char errmsg[256];
	char *errstr, *p, *fnam;
	int n, lline, tline;

	snprintf(errmsg, sizeof(errmsg), "unknown error");

	errstr = strdup(lua_tostring(L, -1));
	if (!errstr)
		return;

	fnam = strchr(errstr, '"');
	if (!fnam)
		goto out_1;

	fnam++;
	p = strchr(fnam, '"');
	if (!p)
		goto out_1;

	*p++ = '\0';
	lline = 0;
	if ((p = strchr(p, ':')))
		lline = atoi(++p);
	else
		goto out_1;

	p = strchr(p, ':');
	if (!p)
		goto out_1;
	p++;

	tline = 0;
	for (n = 0; s->lines[n]; n++) {
		if (s->lines[n] >= lline)
			break;
		tline++;
	}

	snprintf(errmsg, sizeof(errmsg), "[template \"%s\"]:%d:%s",
	    fnam, tline, p);
	lua_pushstring(L, errmsg);
out_1:
	free(errstr);
}

const char *
lt_errmsg(lua_State *L)
{
	static char errmsg[256];
	char *errstr, *p, *fnam;
	int lline, tline, t;

	snprintf(errmsg, sizeof(errmsg), "unknown error");

	errstr = strdup(lua_tostring(L, -1));
	if (!errstr)
		goto out_2;

	fnam = strchr(errstr, '"');
	if (!fnam)
		goto out_1;

	fnam++;
	p = strchr(fnam, '"');
	if (!p)
		goto out_1;

	*p++ = '\0';
	lline = 0;
	if ((p = strchr(p, ':')))
		lline = atoi(++p);
	else
		goto out_1;

	p = strchr(p, ':');
	if (!p)
		goto out_1;
	p++;

	lua_getglobal(L, "lt");
	lua_pushstring(L, fnam);
	lua_gettable(L, -2);
	lua_pushstring(L, "lines");
	lua_gettable(L, -2);
	t = lua_gettop(L);
	lua_pushnil(L);
	tline = 1;
	while (lua_next(L, t)) {
		if (lua_tointeger(L, -1) >= lline) {
			lua_pop(L, 1);
			break;
		}
		tline++;
		lua_pop(L, 1);
	}

	lua_pop(L, 3);

	snprintf(errmsg, sizeof(errmsg), "[template \"%s\"]:%d:%s",
	    fnam, tline, p);

out_1:
	free(errstr);
out_2:
	return errmsg;
}
#endif

/*
 * lt_process is required in addition to lt_reader to process include
 * directives and to detect recursion.
 */
int
process_file(lua_State *L, const char *fnam, struct lt_include_head *ihead,
    int print)
{
	struct stat sb;
	struct lt_include *include, *cg;
	struct lt_state s;
	int fd, errval;
	char errmsg[BUFSIZ];
	char path[PATH_MAX];
	char *buf;
#ifdef LT_DEBUG
	int n;
#endif
	const char *buff;
	size_t len;
	struct lt_include_head includes;

	snprintf(path, sizeof path, "custom/%s", fnam);
	if (stat(path, &sb)) {
		strncpy(path, fnam, sizeof path);

		if (stat(path, &sb)) {
			snprintf(errmsg, sizeof errmsg, "can't stat %s", fnam);
			lua_pushstring(L, errmsg);
			return -1;
		}
	}

	if (print)
		printf("processing template %s\n", path);

	if ((fd = open(path, O_RDONLY)) == -1) {
		snprintf(errmsg, sizeof errmsg, "can't open %s", fnam);
		lua_pushstring(L, errmsg);
		return -1;
	}

	if ((buf = mmap(0, sb.st_size, PROT_READ,
	    MAP_PRIVATE | MAP_FILE, fd, (off_t)0L)) == MAP_FAILED) {
		snprintf(errmsg, sizeof errmsg, "can't mmap %s", fnam);
		lua_pushstring(L, errmsg);
		close(fd);
		return -1;
	}

	SLIST_INIT(&includes);
#ifdef LT_DEBUG
	s.tline = 0;
	s.lline = 0;
	s.linemax = LINEBUFSIZ;
	s.lines = calloc(s.linemax, sizeof(int));
#endif
	include = malloc(sizeof(struct lt_include));
	include->fnam = strdup(fnam);
	SLIST_INSERT_HEAD(ihead, include, next);

	reader(L, buf, &s, &includes, fnam, print);
	buff = lua_tolstring(L, -1, &len);
	if ((errval = luaL_loadbuffer(L, buff, len, fnam))) {
		switch (errval) {
		case LUA_ERRSYNTAX:
			lua_pushstring(L, "syntax error");
			break;
		case LUA_ERRMEM:
			lua_pushstring(L, "memory error");
			break;
		default:
			lua_pushstring(L, "unknown load error");
			break;
		}
		printf("error loading: %s\n", lua_tostring(L, -1));
	}
	lua_pushvalue(L, -4);
	if (lua_pcall(L, 1, 0, 0))
		printf("an error occured: %s\n", lua_tostring(L, -1));
	lua_pop(L, 1);

	buff = lua_tolstring(L, -1, &len);
	if ((errval = luaL_loadbuffer(L, buff, len, fnam))) {
		switch (errval) {
		case LUA_ERRSYNTAX:
			lua_pushstring(L, "syntax error");
			break;
		case LUA_ERRMEM:
			lua_pushstring(L, "memory error");
			break;
		default:
			lua_pushstring(L, "unknown load error");
			break;
		}
		printf("error loading: %s\n", lua_tostring(L, -1));
	}
	lua_pushvalue(L, -3);
	if (lua_pcall(L, 1, 0, 0))
		printf("an error occured: %s\n", lua_tostring(L, -1));
	lua_pop(L, 1);

	munmap(buf, sb.st_size);
	close(fd);

#ifdef LT_DEBUG
	if (s.lines) {
		lua_getfield(L, -1, "template");
		lua_getfield(L, -1, fnam);
		lua_newtable(L);
		for (n = 0; n < s.linemax && s.lines[n]; n++) {
			lua_pushinteger(L, n + 1);
			lua_pushinteger(L, s.lines[n]);
			lua_settable(L, -3);
		}
		lua_setfield(L, -2, "lines");
		lua_pop(L, 2);
		free(s.lines);
	}
#endif

	if (errval)
		return -1;

	SLIST_FOREACH(include, &includes, next) {
		SLIST_FOREACH(cg, ihead, next)
			if (!strcmp(cg->fnam, include->fnam)) {
				snprintf(errmsg, sizeof errmsg,
				    "recursion detected: %s", cg->fnam);
				lua_pushstring(L, errmsg);
				return -1;
			}

		lua_getfield(L, -1, "template");
		lua_getfield(L, -1, include->fnam);
		errval = lua_isnil(L, -1);
		lua_pop(L, 2);

		if (errval) {
			errval = process_file(L, include->fnam, ihead, print);
			if (errval)
				return errval;
		}
	}
	SLIST_REMOVE_HEAD(ihead, next);
	return 0;
}

const char *
reader(lua_State *L, char *p, struct lt_state *s,
    struct lt_include_head *includes, const char *template, int print)
{
	struct lt_include *include;
	int exists, n, escape, local, state, extends, block, output, cb;
	char fnam[MAXPATHLEN];
	struct buffer body, blocks, *b;

	buf_init(&body);
	buf_init(&blocks);
	b = &body;

	buf_addstring(b, "_ENV = ...\n"
		    "template['");
	buf_addstring(b, template);
	buf_addstring(b, "'] = { blk = {} }\ntemplate['");
	buf_addstring(b, template);
	buf_addstring(b, "'].main = function(_ENV, _t)\n");

	buf_addstring(&blocks, "_ENV = ...\n");

#ifdef LT_DEBUG
	s->tline = 0;
	s->lline = 2;
#endif

	state = s_initial;
	escape = e_none;
	extends = 0;
	block = 0;
	output = 0;
	cb = 0;	/* closing braces */

	while (*p) {
#ifdef LT_DEBUG
		if (*p == '\n') {
			if (s->tline == s->linemax) {
				s->lines = realloc(s->lines,
					s->linemax * 2 * sizeof(int));
				if (s->lines) {
					memset(&s->lines[s->tline], 0,
					    s->linemax * sizeof(int));
					s->linemax *= 2;
				}
			}
			if (s->lines)
				s->lines[s->tline++] = s->lline++;
		}
#endif
		switch (state) {
		case s_initial:
			if (strncmp(p, "<%", 2) && (!extends
			    || (extends && block))) {
				buf_addstring(b, "print([[");
				output = 1;
			}
			state = s_output;
			/* FALLTHROUGH */
		case s_output:
			if (strncmp(p, "<%", 2)) {
				buf_addchar(b, *p++);
				break;
			} else {
				if (output) {
					buf_addstring(b, "]])\n");
					output = 0;
#ifdef LT_DEBUG
					s->lline++;
#endif
				}
				p += 2;
				if (*p == '=') {		/* expression */
					int escape_temp;

					state = s_expression;
					p++;
					escape_temp = escape;
					if (!strncmp(p, "html", 4)) {
						p += 4;
						escape_temp = e_html;
					} else if (!strncmp(p, "xml", 3)) {
						p += 3;
						escape_temp = e_xml;
					} else if (!strncmp(p, "latex", 5)) {
						p += 5;
						escape_temp = e_latex;
					} else if (!strncmp(p, "url", 3)) {
						p += 3;
						escape_temp = e_url;
					} else if (!strncmp(p, "none", 4)) {
						p += 4;
						escape_temp = e_none;
					}
					switch (escape_temp) {
					case e_html:
						buf_addstring(b, "print(escape_html(");
						cb += 1;
						break;
					case e_xml:
						buf_addstring(b, "print(escape_xml(");
						cb += 1;
						break;
					case e_url:
						buf_addstring(b, "print(escape_url(");
						cb += 1;
						break;
					case e_latex:
						buf_addstring(b, "print(escape_latex(");
						cb += 1;
						break;
					default:
						buf_addstring(b, "print(");
					}
					if (*p == '%') {	/* format */
						buf_addstring(b,
						    "string.format([[");
						while (*p && !isspace((int)*p))
							buf_addchar(b, *p++);
						buf_addstring(b, "]], ");
						cb += 1;
					}
				} else if (*p == '!') {	/* instr. */
					state = s_instruction;
					p++;
				} else
					state = s_code;
				while (isspace((int)*p))
					p++;
				if (state == s_expression
				    || state == s_instruction)
					break;
			}
			/* FALLTHROUGH */
		case s_code:
			if (strncmp(p, "%>", 2))
				buf_addchar(b, *p++);
			else {
				buf_addstring(b, "\n");
				state = s_initial;
				p += 2;
#ifdef LT_DEBUG
				s->lline++;
#endif
			}
			break;
		case s_expression:
			if (strncmp(p, "%>", 2))
				buf_addchar(b, *p++);
			else {
				state = s_initial;
				for (; cb > 0; cb--)
					buf_addchar(b, ')');
				buf_addstring(b, ")\n");
				p += 2;
#ifdef LT_DEBUG
				s->lline++;
#endif
			}
			break;
		case s_instruction:
			if (!strncmp(p, "include", 7)) {
				bzero(fnam, sizeof fnam);
				n = 0;
				p += 7;
				while (isspace((int)*p))
					p++;
				if (*p == '"' || *p == '\'') {
					p++;
					while (*p && *p != '"' && *p != '\''
					    && n < sizeof fnam)
						fnam[n++] = *p++;
				} else {
					while (*p && !isspace((int)*p)
					    && !(*p == '%' && *(p + 1) == '>')
					    && n < sizeof fnam)
						fnam[n++] = *p++;
				}
				buf_addstring(b, "render_template(_ENV, '");
				buf_addstring(b, fnam);
				buf_addstring(b, "')\n");
				exists = 0;
				SLIST_FOREACH(include, includes, next)
					if (!strcmp(include->fnam, fnam)) {
						exists = 1;
						break;
					}
				if (!exists) {
					include = malloc(
					    sizeof(struct lt_include));
					include->fnam = strdup(fnam);
					SLIST_INSERT_HEAD(includes, include,
					    next);
				}
#ifdef LT_DEBUG
				s->lline++;
#endif
			} else if (!strncmp(p, "escape", 6)) {
				p += 6;
				while (isspace((int)*p))
					p++;
				if (!strncmp(p, "none", 4)) {
					escape = e_none;
					p += 4;
				} else if (!strncmp(p, "html", 4)) {
					escape = e_html;
					p += 4;
				} else if (!strncmp(p, "latex", 5)) {
					escape = e_latex;
					p += 5;
				} else if (!strncmp(p, "url", 3)) {
					escape = e_url;
					p += 3;
				}
			} else if (!strncmp(p, "block", 5)) {
				bzero(fnam, sizeof fnam);
				n = 0;
				p += 5;
				while (isspace((int)*p))
					p++;
				if (*p == '"' || *p == '\'') {
					p++;
					while (*p && *p != '"' && *p != '\''
					    && n < sizeof fnam)
						fnam[n++] = *p++;
				} else {
					while (*p && !isspace((int)*p)
					    && !(*p == '%' && *(p + 1) == '>')
					    && n < sizeof fnam)
						fnam[n++] = *p++;
				}
				b = &blocks;
				if (!extends) {
					buf_addstring(b, "if template['");
					buf_addstring(b, template);
					buf_addstring(b, "'].blk['");
					buf_addstring(b, fnam);
					buf_addstring(b, "'] == nil then\n");
#ifdef LT_DEBUG
					s->lline++;
#endif
				}
				buf_addstring(b, "template['");
				buf_addstring(b, template);
				buf_addstring(b, "'].blk['");
				buf_addstring(b, fnam);
				buf_addstring(b, "'] = function (_ENV)\n");
				block = 1;
#ifdef LT_DEBUG
				s->lline++;
#endif
			} else if (!strncmp(p, "endblock", 8)) {
				p += 8;
				if (!extends) {
					buf_addstring(b, "end\n");
#ifdef LT_DEBUG
					s->lline++;
				}
				s->lline++;
#else
				}
#endif
				buf_addstring(b, "end\n");
				b = &body;
				if (!extends) {
					buf_addstring(b,
					    "render_block(_ENV, _t, '");
					buf_addstring(b, fnam);
					buf_addstring(b, "')\n");
#ifdef LT_DEBUG
					s->lline++;
#endif
				}
				block = 0;
			} else if (!strncmp(p, "extends", 7)) {
				if (!extends) {
					buf_addstring(b, "end\n");
#ifdef LT_DEBUG
					s->lline++;
#endif
				}
				bzero(fnam, sizeof fnam);
				n = 0;
				p += 7;
				while (isspace((int)*p))
					p++;
				if (*p == '"' || *p == '\'') {
					p++;
					while (*p && *p != '"' && *p != '\''
					    && n < sizeof fnam)
						fnam[n++] = *p++;
				} else {
					while (*p && !isspace((int)*p)
					    && !(*p == '%' && *(p + 1) == '>')
					    && n < sizeof fnam)
						fnam[n++] = *p++;
				}
				buf_addstring(b, "template['");
				buf_addstring(b, template);
				buf_addstring(b, "'].main = nil\n");

				buf_addstring(b, "template['");
				buf_addstring(b, template);
				buf_addstring(b, "'].extends = '");
				buf_addstring(b, fnam);
				buf_addstring(b, "'\n");
				extends = 1;
				exists = 0;
				SLIST_FOREACH(include, includes, next)
					if (!strcmp(include->fnam, fnam)) {
						exists = 1;
						break;
					}
				if (!exists) {
					include = malloc(
					    sizeof(struct lt_include));
					include->fnam = strdup(fnam);
					SLIST_INSERT_HEAD(includes, include,
					    next);
				}
#ifdef LT_DEBUG
				s->lline++;
#endif
			}
			while (*p && strncmp(p, "%>", 2))
				p++;
			if (*p)
				p += 2;
			state = s_initial;
			break;
		}
	}
	if (*p == '\0') {
		if (state == s_output && !extends) {
			buf_addstring(b, "]])\n");
#ifdef LT_DEBUG
			s->lline++;
#endif
		}
		if (state == s_output || state == s_initial)
			state = s_terminate;
		else
			state = s_error;
		if (!extends) {
			buf_addstring(b, "end\n");
#ifdef LT_DEBUG
			s->lline++;
#endif
		}
	}
	buf_push(&blocks, L);
	buf_push(&body, L);

	buf_free(&blocks);
	buf_free(&body);

	if (print) {
		printf("%s", lua_tostring(L, -1));
		printf("%s", lua_tostring(L, -2));
	}
}
