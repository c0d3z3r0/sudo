#ifndef lint
/*static char yysccsid[] = "from: @(#)yaccpar	1.9 (Berkeley) 02/21/93";*/
static char yyrcsid[]
#if __GNUC__ >= 2
  __attribute__ ((unused))
#endif /* __GNUC__ >= 2 */
  = "$OpenBSD: skeleton.c,v 1.22 2003/11/25 20:00:15 espie Exp $";
#endif
#include <stdlib.h>
#define YYBYACC 1
#define YYMAJOR 1
#define YYMINOR 9
#define YYLEX yylex()
#define YYEMPTY -1
#define yyclearin (yychar=(YYEMPTY))
#define yyerrok (yyerrflag=0)
#define YYRECOVERING() (yyerrflag!=0)
#define YYPREFIX "yy"
#line 2 "parse.yacc"
/*
 * Copyright (c) 1996, 1998-2003 Todd C. Miller <Todd.Miller@courtesan.com>
 * All rights reserved.
 *
 * This code is derived from software contributed by Chris Jepeway.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * 4. Products derived from this software may not be called "Sudo" nor
 *    may "Sudo" appear in their names without specific prior written
 *    permission from the author.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

/*
 * XXX - the whole opFOO naming thing is somewhat bogus.
 *
 * XXX - the way things are stored for printmatches is stupid,
 *       they should be stored as elements in an array and then
 *       list_matches() can format things the way it wants.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif /* STDC_HEADERS */
#ifdef HAVE_STRING_H
# include <string.h>
#else
# ifdef HAVE_STRINGS_H
#  include <strings.h>
# endif
#endif /* HAVE_STRING_H */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <pwd.h>
#if defined(HAVE_MALLOC_H) && !defined(STDC_HEADERS)
# include <malloc.h>
#endif /* HAVE_MALLOC_H && !STDC_HEADERS */
#if defined(YYBISON) && defined(HAVE_ALLOCA_H) && !defined(__GNUC__)
# include <alloca.h>
#endif /* YYBISON && HAVE_ALLOCA_H && !__GNUC__ */
#ifdef HAVE_LSEARCH
# include <search.h>
#endif /* HAVE_LSEARCH */

#include "sudo.h"
#include "parse.h"

#ifndef HAVE_LSEARCH
#include "emul/search.h"
#endif /* HAVE_LSEARCH */

#ifndef lint
static const char rcsid[] = "$Sudo$";
#endif /* lint */

/*
 * Globals
 */
extern int sudolineno, parse_error;
int errorlineno = -1;
int clearaliases = TRUE;
int printmatches = FALSE;
int pedantic = FALSE;
int keepall = FALSE;
int quiet = FALSE;

/*
 * Alias types
 */
#define HOST_ALIAS		 1
#define CMND_ALIAS		 2
#define USER_ALIAS		 3
#define RUNAS_ALIAS		 4

/*
 * The matching stack, initial space allocated in init_parser().
 */
struct matchstack *match;
int top = 0, stacksize = 0;

#define push \
    do { \
	if (top >= stacksize) { \
	    while ((stacksize += STACKINCREMENT) < top); \
	    match = (struct matchstack *) erealloc3(match, stacksize, sizeof(struct matchstack)); \
	} \
	match[top].user   = -1; \
	match[top].cmnd   = -1; \
	match[top].host   = -1; \
	match[top].runas  = -1; \
	match[top].nopass = def_authenticate ? -1 : TRUE; \
	match[top].noexec = def_noexec ? TRUE : -1; \
	top++; \
    } while (0)

#define pushcp \
    do { \
	if (top >= stacksize) { \
	    while ((stacksize += STACKINCREMENT) < top); \
	    match = (struct matchstack *) erealloc3(match, stacksize, sizeof(struct matchstack)); \
	} \
	match[top].user   = match[top-1].user; \
	match[top].cmnd   = match[top-1].cmnd; \
	match[top].host   = match[top-1].host; \
	match[top].runas  = match[top-1].runas; \
	match[top].nopass = match[top-1].nopass; \
	match[top].noexec = match[top-1].noexec; \
	top++; \
    } while (0)

#define pop \
    { \
	if (top == 0) \
	    yyerror("matching stack underflow"); \
	else \
	    top--; \
    }

/*
 * Shortcuts for append()
 */
#define append_cmnd(s, p) append(s, &cm_list[cm_list_len].cmnd, \
	&cm_list[cm_list_len].cmnd_len, &cm_list[cm_list_len].cmnd_size, p)

#define append_runas(s, p) append(s, &cm_list[cm_list_len].runas, \
	&cm_list[cm_list_len].runas_len, &cm_list[cm_list_len].runas_size, p)

#define append_entries(s, p) append(s, &ga_list[ga_list_len-1].entries, \
	&ga_list[ga_list_len-1].entries_len, \
	&ga_list[ga_list_len-1].entries_size, p)

/*
 * The stack for printmatches.  A list of allowed commands for the user.
 */
static struct command_match *cm_list = NULL;
static size_t cm_list_len = 0, cm_list_size = 0;

/*
 * List of Cmnd_Aliases and expansions for `sudo -l'
 */
static int in_alias = FALSE;
static size_t ga_list_len = 0, ga_list_size = 0;
static struct generic_alias *ga_list = NULL;

/*
 * Does this Defaults list pertain to this user?
 */
static int defaults_matches = 0;

/*
 * Local protoypes
 */
static int  add_alias		__P((char *, int, int));
static void append		__P((char *, char **, size_t *, size_t *, char *));
static void expand_ga_list	__P((void));
static void expand_match_list	__P((void));
static aliasinfo *find_alias	__P((char *, int));
static int  more_aliases	__P((void));
       void init_parser		__P((void));
       void yyerror		__P((char *));

void
yyerror(s)
    char *s;
{
    /* Save the line the first error occurred on. */
    if (errorlineno == -1)
	errorlineno = sudolineno ? sudolineno - 1 : 0;
    if (s && !quiet) {
#ifndef TRACELEXER
	(void) fprintf(stderr, ">>> sudoers file: %s, line %d <<<\n", s,
	    sudolineno ? sudolineno - 1 : 0);
#else
	(void) fprintf(stderr, "<*> ");
#endif
    }
    parse_error = TRUE;
}
#line 220 "parse.yacc"
typedef union {
    char *string;
    int BOOLEAN;
    struct sudo_command command;
    int tok;
} YYSTYPE;
#line 244 "sudo.tab.c"
#define COMMAND 257
#define ALIAS 258
#define DEFVAR 259
#define NTWKADDR 260
#define NETGROUP 261
#define USERGROUP 262
#define WORD 263
#define DEFAULTS 264
#define DEFAULTS_HOST 265
#define DEFAULTS_USER 266
#define DEFAULTS_RUNAS 267
#define RUNAS 268
#define NOPASSWD 269
#define PASSWD 270
#define NOEXEC 271
#define EXEC 272
#define ALL 273
#define COMMENT 274
#define HOSTALIAS 275
#define CMNDALIAS 276
#define USERALIAS 277
#define RUNASALIAS 278
#define ERROR 279
#define YYERRCODE 256
#if defined(__cplusplus) || defined(__STDC__)
const short yylhs[] =
#else
short yylhs[] =
#endif
	{                                        -1,
    0,    0,    7,    7,    9,    7,    7,    7,    7,    7,
    7,   15,   16,   18,   16,   19,   16,   21,   16,   17,
   17,   22,   22,   22,   22,   22,   10,   10,   23,   25,
   25,    2,    2,    2,    2,    2,   24,   24,   26,   30,
   31,   30,   27,   27,    5,    5,    4,   32,    4,    3,
    3,    3,    3,    3,   29,   29,   29,   28,   28,   28,
    1,    1,    1,   12,   12,   34,   33,   20,   20,   13,
   13,   36,   35,   37,   37,   14,   14,   39,   38,   11,
   11,   41,   40,    8,    8,   42,   42,    6,    6,    6,
    6,    6,
};
#if defined(__cplusplus) || defined(__STDC__)
const short yylen[] =
#else
short yylen[] =
#endif
	{                                         2,
    1,    2,    1,    2,    0,    3,    2,    2,    2,    2,
    1,    2,    1,    0,    3,    0,    3,    0,    3,    1,
    3,    1,    2,    3,    3,    3,    1,    3,    3,    1,
    2,    1,    1,    1,    1,    1,    1,    3,    4,    1,
    0,    3,    0,    2,    1,    3,    1,    0,    3,    1,
    1,    1,    1,    1,    0,    1,    1,    0,    1,    1,
    1,    1,    1,    1,    3,    0,    4,    1,    3,    1,
    3,    0,    4,    1,    3,    1,    3,    0,    4,    1,
    3,    0,    4,    1,    3,    1,    2,    1,    1,    1,
    1,    1,
};
#if defined(__cplusplus) || defined(__STDC__)
const short yydefred[] =
#else
short yydefred[] =
#endif
	{                                      0,
    0,   13,   18,   14,   16,    3,    0,    0,    0,    0,
    0,    1,    0,   11,    0,    4,    0,    0,    0,   66,
    0,   64,   72,    0,   70,   82,    0,   80,   78,    0,
   76,    2,   91,   90,   89,   88,   92,    0,   86,    0,
   84,    0,    0,   12,    0,   36,   33,   34,   35,   32,
    0,   30,    0,   68,    0,   53,   52,   51,   50,   54,
   48,   47,   45,    0,    0,    0,    0,    0,    0,    0,
    0,    0,   87,    0,    0,    0,   27,    0,    0,    0,
   23,    0,   31,    0,    0,    0,    0,   65,    0,   71,
    0,   81,    0,   77,   85,    0,    0,   24,   25,   26,
   21,   69,   49,   46,    0,   63,   62,   61,   41,   40,
   74,    0,    0,    0,   28,    0,    0,   37,    0,    0,
    0,    0,    0,   59,   60,    0,   42,   75,   38,   56,
   57,    0,   39,
};
#if defined(__cplusplus) || defined(__STDC__)
const short yydgoto[] =
#else
short yydgoto[] =
#endif
	{                                      11,
  110,   52,   62,   63,   64,   39,   12,   40,   13,   75,
   27,   21,   24,   30,   14,   15,   44,   18,   19,   76,
   17,   45,   77,  117,   54,  118,  119,  126,  132,  111,
  120,   85,   22,   65,   25,   67,  112,   31,   71,   28,
   69,   41,
};
#if defined(__cplusplus) || defined(__STDC__)
const short yysindex[] =
#else
short yysindex[] =
#endif
	{                                   -234,
 -262,    0,    0,    0,    0,    0, -242, -238, -235, -229,
 -234,    0,  296,    0,  -31,    0,  305,  296,  309,    0,
  -19,    0,    0,  -10,    0,    0,   -4,    0,    0,    2,
    0,    0,    0,    0,    0,    0,    0, -212,    0,  283,
    0,  -26, -194,    0,   22,    0,    0,    0,    0,    0,
 -205,    0,   23,    0,   25,    0,    0,    0,    0,    0,
    0,    0,    0,   27,  -23, -242,   11, -238,   12, -235,
   14, -229,    0,  296,   18,  -34,    0, -185, -184, -180,
    0,  -31,    0,  305, -199,  309,  305,    0,  -33,    0,
  296,    0,  309,    0,    0,  305, -183,    0,    0,    0,
    0,    0,    0,    0,   23,    0,    0,    0,    0,    0,
    0,   40,   25,   27,    0,  309,   42,    0, -257, -221,
  -33,   27, -183,    0,    0, -244,    0,    0,    0,    0,
    0,  -33,    0,};
#if defined(__cplusplus) || defined(__STDC__)
const short yyrindex[] =
#else
short yyrindex[] =
#endif
	{                                    328,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
  328,    0,    0,    0,    0,    0,    0,    0,    0,    0,
  139,    0,    0,  162,    0,    0,  185,    0,    0,  208,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    1,    0,    0,  236,    0,    0,    0,    0,    0,
    0,    0,  -30,    0,  -24,    0,    0,    0,    0,    0,
    0,    0,    0,  -12,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,  260,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,  -27,    0,    0,    0,
    0,    0,    0,    0,   24,    0,    0,    0,    0,    0,
    0,   47,   70,   93,    0,    0,  116,    0,  282,    0,
    0,  -20,  -27,    0,    0,  -25,    0,    0,    0,    0,
    0,    0,    0,};
#if defined(__cplusplus) || defined(__STDC__)
const short yygindex[] =
#else
short yygindex[] =
#endif
	{                                      0,
  -32,   36,    4,    5,  -88,   52,   81,  -14,    0,    0,
    0,    0,    0,    0,    0,    0,   13,    0,    0,   -6,
    0,    0,   -2,    0,   15,  -22,    0,    0,    0, -114,
    0,    0,   30,    0,   29,    0,    0,   26,    0,   32,
    0,   33,
};
#define YYTABLESIZE 601
#if defined(__cplusplus) || defined(__STDC__)
const short yytable[] =
#else
short yytable[] =
#endif
	{                                     109,
   22,   43,   19,   55,  114,   43,  128,   55,   15,   84,
   53,   16,   44,  124,  125,   20,   79,  133,   80,   23,
   17,    1,   26,   67,  130,  131,   97,  122,   29,    2,
    3,    4,    5,   22,   78,  106,  107,   87,   66,    6,
    7,    8,    9,   10,   22,   33,   73,   68,   34,   35,
   36,  108,   46,   70,   47,   48,   67,   49,   56,   72,
   37,   57,   58,   59,   81,   82,   84,   50,   74,   83,
   86,   89,   91,   60,   93,   96,  113,   98,   99,   73,
  105,   67,  100,  121,  116,  123,   83,  127,  103,   73,
  104,   32,   79,  115,  101,   88,   90,   94,  102,    0,
  129,   92,   83,    0,   73,    0,   95,    0,    0,    0,
    0,    0,    0,    0,    0,   29,    0,    0,    0,    0,
    0,    0,    0,    0,    0,   79,    0,   83,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    8,    0,
    0,    0,    0,    0,    0,    0,    0,    0,   29,    0,
   79,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    9,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    8,    0,   29,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    7,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    9,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,   10,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    7,    0,    0,
    0,    0,    0,  106,  107,    0,    0,   42,   19,   43,
   43,   55,   55,    0,   15,   20,   44,   44,    0,  108,
   10,   43,   43,   43,   43,   43,   17,   55,   44,   44,
   44,   44,   44,    0,    0,    0,   22,    0,   22,    6,
    0,   22,   22,   22,   22,   22,   22,   22,   20,    0,
    0,    0,    0,   22,   22,   22,   22,   22,   22,   67,
    0,   67,    0,    0,   67,   67,   67,   67,   67,   67,
   67,    0,    6,    0,    0,    0,   67,   67,   67,   67,
   67,   67,   73,    0,   73,    0,    0,   73,   73,   73,
   73,   73,   73,   73,   58,   51,    0,    0,    0,   73,
   73,   73,   73,   73,   73,   83,   74,   83,   38,    0,
   83,   83,   83,   83,   83,   83,   83,   51,    0,    0,
    0,   61,   83,   83,   83,   83,   83,   83,   79,    0,
   79,    0,    0,   79,   79,   79,   79,   79,   79,   79,
    5,    0,    0,    0,    0,   79,   79,   79,   79,   79,
   79,   29,    0,   29,    0,    0,   29,   29,   29,   29,
   29,   29,   29,    0,    0,    0,    0,    0,   29,   29,
   29,   29,   29,   29,    8,    0,    8,    0,    0,    8,
    8,    8,    8,    8,    8,    8,    0,    0,    0,    0,
    0,    8,    8,    8,    8,    8,    8,    9,    0,    9,
    0,    0,    9,    9,    9,    9,    9,    9,    9,    0,
    0,    0,    0,    0,    9,    9,    9,    9,    9,    9,
    7,    0,    7,    0,    0,    7,    7,    7,    7,    7,
    7,    7,    0,    0,    0,    0,    0,    7,    7,    7,
    7,    7,    7,   10,    0,   10,    0,    0,   10,   10,
   10,   10,   10,   10,   10,    0,    0,    0,    0,    0,
   10,   10,   10,   10,   10,   10,    0,    0,    0,    0,
    0,   20,    0,   20,    0,    0,   20,   20,   20,   20,
   20,   20,   20,    0,    0,    0,    0,    0,   20,   20,
   20,   20,   20,   20,    0,    6,    0,    6,    0,    0,
    6,    6,    6,    6,    6,    6,    6,    0,    0,    0,
    0,    0,    6,    6,    6,    6,    6,    6,   58,   58,
   46,    0,   47,   48,    0,   49,    0,    0,    0,    0,
   58,   58,    0,   33,   58,   50,   34,   35,   36,    0,
    0,    0,   46,    0,   47,   48,   56,   49,   37,   57,
   58,   59,    0,    0,    0,    0,    0,   50,    0,    0,
    0,   60,    0,    0,    0,    5,    0,    0,    5,    5,
    5,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    5,
};
#if defined(__cplusplus) || defined(__STDC__)
const short yycheck[] =
#else
short yycheck[] =
#endif
	{                                      33,
    0,   33,   33,   18,   93,   33,  121,   33,   33,   44,
   17,  274,   33,  271,  272,  258,   43,  132,   45,  258,
   33,  256,  258,    0,  269,  270,   61,  116,  258,  264,
  265,  266,  267,   33,   61,  257,  258,   61,   58,  274,
  275,  276,  277,  278,   44,  258,    0,   58,  261,  262,
  263,  273,  258,   58,  260,  261,   33,  263,  258,   58,
  273,  261,  262,  263,  259,   44,   44,  273,   44,    0,
   44,   61,   61,  273,   61,   58,   91,  263,  263,   33,
   87,   58,  263,   44,  268,   44,   51,  120,   85,   38,
   86,   11,    0,   96,   82,   66,   68,   72,   84,   -1,
  123,   70,   33,   -1,   58,   -1,   74,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,    0,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   33,   -1,   58,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,    0,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   33,   -1,
   58,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,    0,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   33,   -1,   58,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,    0,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   33,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,    0,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   33,   -1,   -1,
   -1,   -1,   -1,  257,  258,   -1,   -1,  259,  259,  257,
  258,  257,  258,   -1,  259,    0,  257,  258,   -1,  273,
   33,  269,  270,  271,  272,  273,  259,  273,  269,  270,
  271,  272,  273,   -1,   -1,   -1,  256,   -1,  258,    0,
   -1,  261,  262,  263,  264,  265,  266,  267,   33,   -1,
   -1,   -1,   -1,  273,  274,  275,  276,  277,  278,  256,
   -1,  258,   -1,   -1,  261,  262,  263,  264,  265,  266,
  267,   -1,   33,   -1,   -1,   -1,  273,  274,  275,  276,
  277,  278,  256,   -1,  258,   -1,   -1,  261,  262,  263,
  264,  265,  266,  267,   33,   33,   -1,   -1,   -1,  273,
  274,  275,  276,  277,  278,  256,   44,  258,   33,   -1,
  261,  262,  263,  264,  265,  266,  267,   33,   -1,   -1,
   -1,   33,  273,  274,  275,  276,  277,  278,  256,   -1,
  258,   -1,   -1,  261,  262,  263,  264,  265,  266,  267,
   33,   -1,   -1,   -1,   -1,  273,  274,  275,  276,  277,
  278,  256,   -1,  258,   -1,   -1,  261,  262,  263,  264,
  265,  266,  267,   -1,   -1,   -1,   -1,   -1,  273,  274,
  275,  276,  277,  278,  256,   -1,  258,   -1,   -1,  261,
  262,  263,  264,  265,  266,  267,   -1,   -1,   -1,   -1,
   -1,  273,  274,  275,  276,  277,  278,  256,   -1,  258,
   -1,   -1,  261,  262,  263,  264,  265,  266,  267,   -1,
   -1,   -1,   -1,   -1,  273,  274,  275,  276,  277,  278,
  256,   -1,  258,   -1,   -1,  261,  262,  263,  264,  265,
  266,  267,   -1,   -1,   -1,   -1,   -1,  273,  274,  275,
  276,  277,  278,  256,   -1,  258,   -1,   -1,  261,  262,
  263,  264,  265,  266,  267,   -1,   -1,   -1,   -1,   -1,
  273,  274,  275,  276,  277,  278,   -1,   -1,   -1,   -1,
   -1,  256,   -1,  258,   -1,   -1,  261,  262,  263,  264,
  265,  266,  267,   -1,   -1,   -1,   -1,   -1,  273,  274,
  275,  276,  277,  278,   -1,  256,   -1,  258,   -1,   -1,
  261,  262,  263,  264,  265,  266,  267,   -1,   -1,   -1,
   -1,   -1,  273,  274,  275,  276,  277,  278,  257,  258,
  258,   -1,  260,  261,   -1,  263,   -1,   -1,   -1,   -1,
  269,  270,   -1,  258,  273,  273,  261,  262,  263,   -1,
   -1,   -1,  258,   -1,  260,  261,  258,  263,  273,  261,
  262,  263,   -1,   -1,   -1,   -1,   -1,  273,   -1,   -1,
   -1,  273,   -1,   -1,   -1,  258,   -1,   -1,  261,  262,
  263,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
  273,
};
#define YYFINAL 11
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 279
#if YYDEBUG
#if defined(__cplusplus) || defined(__STDC__)
const char * const yyname[] =
#else
char *yyname[] =
#endif
	{
"end-of-file",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
"'!'",0,0,0,0,0,0,0,0,0,"'+'","','","'-'",0,0,0,0,0,0,0,0,0,0,0,0,"':'",0,0,
"'='",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
"COMMAND","ALIAS","DEFVAR","NTWKADDR","NETGROUP","USERGROUP","WORD","DEFAULTS",
"DEFAULTS_HOST","DEFAULTS_USER","DEFAULTS_RUNAS","RUNAS","NOPASSWD","PASSWD",
"NOEXEC","EXEC","ALL","COMMENT","HOSTALIAS","CMNDALIAS","USERALIAS",
"RUNASALIAS","ERROR",
};
#if defined(__cplusplus) || defined(__STDC__)
const char * const yyrule[] =
#else
char *yyrule[] =
#endif
	{"$accept : file",
"file : entry",
"file : file entry",
"entry : COMMENT",
"entry : error COMMENT",
"$$1 :",
"entry : $$1 userlist privileges",
"entry : USERALIAS useraliases",
"entry : HOSTALIAS hostaliases",
"entry : CMNDALIAS cmndaliases",
"entry : RUNASALIAS runasaliases",
"entry : defaults_line",
"defaults_line : defaults_type defaults_list",
"defaults_type : DEFAULTS",
"$$2 :",
"defaults_type : DEFAULTS_USER $$2 userlist",
"$$3 :",
"defaults_type : DEFAULTS_RUNAS $$3 runaslist",
"$$4 :",
"defaults_type : DEFAULTS_HOST $$4 hostlist",
"defaults_list : defaults_entry",
"defaults_list : defaults_entry ',' defaults_list",
"defaults_entry : DEFVAR",
"defaults_entry : '!' DEFVAR",
"defaults_entry : DEFVAR '=' WORD",
"defaults_entry : DEFVAR '+' WORD",
"defaults_entry : DEFVAR '-' WORD",
"privileges : privilege",
"privileges : privileges ':' privilege",
"privilege : hostlist '=' cmndspeclist",
"ophost : host",
"ophost : '!' host",
"host : ALL",
"host : NTWKADDR",
"host : NETGROUP",
"host : WORD",
"host : ALIAS",
"cmndspeclist : cmndspec",
"cmndspeclist : cmndspeclist ',' cmndspec",
"cmndspec : runasspec noexec nopasswd opcmnd",
"opcmnd : cmnd",
"$$5 :",
"opcmnd : '!' $$5 cmnd",
"runasspec :",
"runasspec : RUNAS runaslist",
"runaslist : oprunasuser",
"runaslist : runaslist ',' oprunasuser",
"oprunasuser : runasuser",
"$$6 :",
"oprunasuser : '!' $$6 runasuser",
"runasuser : WORD",
"runasuser : USERGROUP",
"runasuser : NETGROUP",
"runasuser : ALIAS",
"runasuser : ALL",
"nopasswd :",
"nopasswd : NOPASSWD",
"nopasswd : PASSWD",
"noexec :",
"noexec : NOEXEC",
"noexec : EXEC",
"cmnd : ALL",
"cmnd : ALIAS",
"cmnd : COMMAND",
"hostaliases : hostalias",
"hostaliases : hostaliases ':' hostalias",
"$$7 :",
"hostalias : ALIAS $$7 '=' hostlist",
"hostlist : ophost",
"hostlist : hostlist ',' ophost",
"cmndaliases : cmndalias",
"cmndaliases : cmndaliases ':' cmndalias",
"$$8 :",
"cmndalias : ALIAS $$8 '=' cmndlist",
"cmndlist : opcmnd",
"cmndlist : cmndlist ',' opcmnd",
"runasaliases : runasalias",
"runasaliases : runasaliases ':' runasalias",
"$$9 :",
"runasalias : ALIAS $$9 '=' runaslist",
"useraliases : useralias",
"useraliases : useraliases ':' useralias",
"$$10 :",
"useralias : ALIAS $$10 '=' userlist",
"userlist : opuser",
"userlist : userlist ',' opuser",
"opuser : user",
"opuser : '!' user",
"user : WORD",
"user : USERGROUP",
"user : NETGROUP",
"user : ALIAS",
"user : ALL",
};
#endif
#ifdef YYSTACKSIZE
#undef YYMAXDEPTH
#define YYMAXDEPTH YYSTACKSIZE
#else
#ifdef YYMAXDEPTH
#define YYSTACKSIZE YYMAXDEPTH
#else
#define YYSTACKSIZE 10000
#define YYMAXDEPTH 10000
#endif
#endif
#define YYINITSTACKSIZE 200
int yydebug;
int yynerrs;
int yyerrflag;
int yychar;
short *yyssp;
YYSTYPE *yyvsp;
YYSTYPE yyval;
YYSTYPE yylval;
short *yyss;
short *yysslim;
YYSTYPE *yyvs;
int yystacksize;
#line 895 "parse.yacc"

#define MOREALIASES (32)
aliasinfo *aliases = NULL;
size_t naliases = 0;
size_t nslots = 0;


/*
 * Compare two aliasinfo structures, strcmp() style.
 * Note that we do *not* compare their values.
 */
static int
aliascmp(a1, a2)
    const VOID *a1, *a2;
{
    int r;
    aliasinfo *ai1, *ai2;

    ai1 = (aliasinfo *) a1;
    ai2 = (aliasinfo *) a2;
    if ((r = strcmp(ai1->name, ai2->name)) == 0)
	r = ai1->type - ai2->type;

    return(r);
}

/*
 * Compare two generic_alias structures, strcmp() style.
 */
static int
genaliascmp(entry, key)
    const VOID *entry, *key;
{
    int r;
    struct generic_alias *ga1, *ga2;

    ga1 = (struct generic_alias *) key;
    ga2 = (struct generic_alias *) entry;
    if ((r = strcmp(ga1->alias, ga2->alias)) == 0)
	r = ga1->type - ga2->type;

    return(r);
}


/*
 * Adds the named alias of the specified type to the aliases list.
 */
static int
add_alias(alias, type, val)
    char *alias;
    int type;
    int val;
{
    aliasinfo ai, *aip;
    size_t onaliases;
    char s[512];

    if (naliases >= nslots && !more_aliases()) {
	(void) snprintf(s, sizeof(s), "Out of memory defining alias `%s'",
			alias);
	yyerror(s);
	return(FALSE);
    }

    ai.type = type;
    ai.val = val;
    ai.name = estrdup(alias);
    onaliases = naliases;

    aip = (aliasinfo *) lsearch((VOID *)&ai, (VOID *)aliases, &naliases,
				sizeof(ai), aliascmp);
    if (aip == NULL) {
	(void) snprintf(s, sizeof(s), "Aliases corrupted defining alias `%s'",
			alias);
	yyerror(s);
	return(FALSE);
    }
    if (onaliases == naliases) {
	(void) snprintf(s, sizeof(s), "Alias `%s' already defined", alias);
	yyerror(s);
	return(FALSE);
    }

    return(TRUE);
}

/*
 * Searches for the named alias of the specified type.
 */
static aliasinfo *
find_alias(alias, type)
    char *alias;
    int type;
{
    aliasinfo ai;

    ai.name = alias;
    ai.type = type;

    return((aliasinfo *) lfind((VOID *)&ai, (VOID *)aliases, &naliases,
		 sizeof(ai), aliascmp));
}

/*
 * Allocates more space for the aliases list.
 */
static int
more_aliases()
{

    nslots += MOREALIASES;
    if (nslots == MOREALIASES)
	aliases = (aliasinfo *) malloc(nslots * sizeof(aliasinfo));
    else
	aliases = (aliasinfo *) realloc(aliases, nslots * sizeof(aliasinfo));

    return(aliases != NULL);
}

/*
 * Lists the contents of the aliases list.
 */
void
dumpaliases()
{
    size_t n;

    for (n = 0; n < naliases; n++) {
	if (aliases[n].val == -1)
	    continue;

	switch (aliases[n].type) {
	case HOST_ALIAS:
	    (void) puts("HOST_ALIAS");
	    break;

	case CMND_ALIAS:
	    (void) puts("CMND_ALIAS");
	    break;

	case USER_ALIAS:
	    (void) puts("USER_ALIAS");
	    break;

	case RUNAS_ALIAS:
	    (void) puts("RUNAS_ALIAS");
	    break;
	}
	(void) printf("\t%s: %d\n", aliases[n].name, aliases[n].val);
    }
}

/*
 * Lists the contents of cm_list and ga_list for `sudo -l'.
 */
void
list_matches()
{
    size_t count; 
    char *p;
    struct generic_alias *ga, key;

    (void) printf("User %s may run the following commands on this host:\n",
	user_name);
    for (count = 0; count < cm_list_len; count++) {

	/* Print the runas list. */
	(void) fputs("    ", stdout);
	if (cm_list[count].runas) {
	    (void) putchar('(');
	    p = strtok(cm_list[count].runas, ", ");
	    do {
		if (p != cm_list[count].runas)
		    (void) fputs(", ", stdout);

		key.alias = p;
		key.type = RUNAS_ALIAS;
		if ((ga = (struct generic_alias *) lfind((VOID *) &key,
		    (VOID *) &ga_list[0], &ga_list_len, sizeof(key), genaliascmp)))
		    (void) fputs(ga->entries, stdout);
		else
		    (void) fputs(p, stdout);
	    } while ((p = strtok(NULL, ", ")));
	    (void) fputs(") ", stdout);
	} else {
	    (void) printf("(%s) ", def_runas_default);
	}

	/* Is execve(2) disabled? */
	if (cm_list[count].noexecve == TRUE && !def_noexec)
	    (void) fputs("NOEXEC: ", stdout);
	else if (cm_list[count].noexecve == FALSE && def_noexec)
	    (void) fputs("EXEC: ", stdout);

	/* Is a password required? */
	if (cm_list[count].nopasswd == TRUE && def_authenticate)
	    (void) fputs("NOPASSWD: ", stdout);
	else if (cm_list[count].nopasswd == FALSE && !def_authenticate)
	    (void) fputs("PASSWD: ", stdout);

	/* Print the actual command or expanded Cmnd_Alias. */
	key.alias = cm_list[count].cmnd;
	key.type = CMND_ALIAS;
	if ((ga = (struct generic_alias *) lfind((VOID *) &key,
	    (VOID *) &ga_list[0], &ga_list_len, sizeof(key), genaliascmp)))
	    (void) puts(ga->entries);
	else
	    (void) puts(cm_list[count].cmnd);
    }

    /* Be nice and free up space now that we are done. */
    for (count = 0; count < ga_list_len; count++) {
	free(ga_list[count].alias);
	free(ga_list[count].entries);
    }
    free(ga_list);
    ga_list = NULL;

    for (count = 0; count < cm_list_len; count++) {
	free(cm_list[count].runas);
	free(cm_list[count].cmnd);
    }
    free(cm_list);
    cm_list = NULL;
    cm_list_len = 0;
    cm_list_size = 0;
}

/*
 * Appends a source string to the destination, optionally prefixing a separator.
 */
static void
append(src, dstp, dst_len, dst_size, separator)
    char *src, **dstp;
    size_t *dst_len, *dst_size;
    char *separator;
{
    size_t src_len = strlen(src);
    char *dst = *dstp;

    /*
     * Only add the separator if there is something to separate from.
     * If the last char is a '!', don't apply the separator (XXX).
     */
    if (separator && dst && dst[*dst_len - 1] != '!')
	src_len += strlen(separator);
    else
	separator = NULL;

    /* Assumes dst will be NULL if not set. */
    if (dst == NULL) {
	dst = (char *) emalloc(BUFSIZ);
	*dst = '\0';
	*dst_size = BUFSIZ;
	*dst_len = 0;
	*dstp = dst;
    }

    /* Allocate more space if necessary. */
    if (*dst_size <= *dst_len + src_len) {
	while (*dst_size <= *dst_len + src_len)
	    *dst_size += BUFSIZ;

	dst = (char *) erealloc(dst, *dst_size);
	*dstp = dst;
    }

    /* Copy src -> dst adding a separator if appropriate and adjust len. */
    if (separator)
	(void) strlcat(dst, separator, *dst_size);
    (void) strlcat(dst, src, *dst_size);
    *dst_len += src_len;
}

/*
 * Frees up space used by the aliases list and resets the associated counters.
 */
void
reset_aliases()
{
    size_t n;

    if (aliases) {
	for (n = 0; n < naliases; n++)
	    free(aliases[n].name);
	free(aliases);
	aliases = NULL;
    }
    naliases = nslots = 0;
}

/*
 * Increments ga_list_len, allocating more space as necessary.
 */
static void
expand_ga_list()
{

    if (++ga_list_len >= ga_list_size) {
	while ((ga_list_size += STACKINCREMENT) < ga_list_len)
	    ;
	ga_list = (struct generic_alias *)
	    erealloc3(ga_list, ga_list_size, sizeof(struct generic_alias));
    }

    ga_list[ga_list_len - 1].entries = NULL;
}

/*
 * Increments cm_list_len, allocating more space as necessary.
 */
static void
expand_match_list()
{

    if (++cm_list_len >= cm_list_size) {
	while ((cm_list_size += STACKINCREMENT) < cm_list_len)
	    ;
	if (cm_list == NULL)
	    cm_list_len = 0;		/* start at 0 since it is a subscript */
	cm_list = (struct command_match *)
	    erealloc3(cm_list, cm_list_size, sizeof(struct command_match));
    }

    cm_list[cm_list_len].runas = cm_list[cm_list_len].cmnd = NULL;
    cm_list[cm_list_len].nopasswd = FALSE;
    cm_list[cm_list_len].noexecve = FALSE;
}

/*
 * Frees up spaced used by a previous parser run and allocates new space
 * for various data structures.
 */
void
init_parser()
{

    /* Free up old data structures if we run the parser more than once. */
    if (match) {
	free(match);
	match = NULL;
	top = 0;
	parse_error = FALSE;
	errorlineno = -1;   
	sudolineno = 1;     
    }

    /* Allocate space for the matching stack. */
    stacksize = STACKINCREMENT;
    match = (struct matchstack *) emalloc2(stacksize, sizeof(struct matchstack));

    /* Allocate space for the match list (for `sudo -l'). */
    if (printmatches == TRUE)
	expand_match_list();
}
#line 978 "sudo.tab.c"
/* allocate initial stack or double stack size, up to YYMAXDEPTH */
#if defined(__cplusplus) || defined(__STDC__)
static int yygrowstack(void)
#else
static int yygrowstack()
#endif
{
    int newsize, i;
    short *newss;
    YYSTYPE *newvs;

    if ((newsize = yystacksize) == 0)
        newsize = YYINITSTACKSIZE;
    else if (newsize >= YYMAXDEPTH)
        return -1;
    else if ((newsize *= 2) > YYMAXDEPTH)
        newsize = YYMAXDEPTH;
    i = yyssp - yyss;
    newss = yyss ? (short *)realloc(yyss, newsize * sizeof *newss) :
      (short *)malloc(newsize * sizeof *newss);
    if (newss == NULL)
        goto bail;
    yyss = newss;
    yyssp = newss + i;
    newvs = yyvs ? (YYSTYPE *)realloc(yyvs, newsize * sizeof *newvs) :
      (YYSTYPE *)malloc(newsize * sizeof *newvs);
    if (newvs == NULL)
        goto bail;
    yyvs = newvs;
    yyvsp = newvs + i;
    yystacksize = newsize;
    yysslim = yyss + newsize - 1;
    return 0;
bail:
    if (yyss)
            free(yyss);
    if (yyvs)
            free(yyvs);
    yyss = yyssp = NULL;
    yyvs = yyvsp = NULL;
    yystacksize = 0;
    return -1;
}

#define YYABORT goto yyabort
#define YYREJECT goto yyabort
#define YYACCEPT goto yyaccept
#define YYERROR goto yyerrlab
int
#if defined(__cplusplus) || defined(__STDC__)
yyparse(void)
#else
yyparse()
#endif
{
    int yym, yyn, yystate;
#if YYDEBUG
#if defined(__cplusplus) || defined(__STDC__)
    const char *yys;
#else /* !(defined(__cplusplus) || defined(__STDC__)) */
    char *yys;
#endif /* !(defined(__cplusplus) || defined(__STDC__)) */

    if ((yys = getenv("YYDEBUG")))
    {
        yyn = *yys;
        if (yyn >= '0' && yyn <= '9')
            yydebug = yyn - '0';
    }
#endif /* YYDEBUG */

    yynerrs = 0;
    yyerrflag = 0;
    yychar = (-1);

    if (yyss == NULL && yygrowstack()) goto yyoverflow;
    yyssp = yyss;
    yyvsp = yyvs;
    *yyssp = yystate = 0;

yyloop:
    if ((yyn = yydefred[yystate]) != 0) goto yyreduce;
    if (yychar < 0)
    {
        if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, reading %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
    }
    if ((yyn = yysindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: state %d, shifting to state %d\n",
                    YYPREFIX, yystate, yytable[yyn]);
#endif
        if (yyssp >= yysslim && yygrowstack())
        {
            goto yyoverflow;
        }
        *++yyssp = yystate = yytable[yyn];
        *++yyvsp = yylval;
        yychar = (-1);
        if (yyerrflag > 0)  --yyerrflag;
        goto yyloop;
    }
    if ((yyn = yyrindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
        yyn = yytable[yyn];
        goto yyreduce;
    }
    if (yyerrflag) goto yyinrecovery;
#if defined(lint) || defined(__GNUC__)
    goto yynewerror;
#endif
yynewerror:
    yyerror("syntax error");
#if defined(lint) || defined(__GNUC__)
    goto yyerrlab;
#endif
yyerrlab:
    ++yynerrs;
yyinrecovery:
    if (yyerrflag < 3)
    {
        yyerrflag = 3;
        for (;;)
        {
            if ((yyn = yysindex[*yyssp]) && (yyn += YYERRCODE) >= 0 &&
                    yyn <= YYTABLESIZE && yycheck[yyn] == YYERRCODE)
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: state %d, error recovery shifting\
 to state %d\n", YYPREFIX, *yyssp, yytable[yyn]);
#endif
                if (yyssp >= yysslim && yygrowstack())
                {
                    goto yyoverflow;
                }
                *++yyssp = yystate = yytable[yyn];
                *++yyvsp = yylval;
                goto yyloop;
            }
            else
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: error recovery discarding state %d\n",
                            YYPREFIX, *yyssp);
#endif
                if (yyssp <= yyss) goto yyabort;
                --yyssp;
                --yyvsp;
            }
        }
    }
    else
    {
        if (yychar == 0) goto yyabort;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, error recovery discards token %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
        yychar = (-1);
        goto yyloop;
    }
yyreduce:
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: state %d, reducing by rule %d (%s)\n",
                YYPREFIX, yystate, yyn, yyrule[yyn]);
#endif
    yym = yylen[yyn];
    yyval = yyvsp[1-yym];
    switch (yyn)
    {
case 3:
#line 273 "parse.yacc"
{ ; }
break;
case 4:
#line 275 "parse.yacc"
{ yyerrok; }
break;
case 5:
#line 276 "parse.yacc"
{ push; }
break;
case 6:
#line 276 "parse.yacc"
{
			    while (top && user_matches != TRUE)
				pop;
			}
break;
case 7:
#line 281 "parse.yacc"
{ ; }
break;
case 8:
#line 283 "parse.yacc"
{ ; }
break;
case 9:
#line 285 "parse.yacc"
{ ; }
break;
case 10:
#line 287 "parse.yacc"
{ ; }
break;
case 11:
#line 289 "parse.yacc"
{ ; }
break;
case 13:
#line 295 "parse.yacc"
{
			    defaults_matches = TRUE;
			}
break;
case 14:
#line 298 "parse.yacc"
{ push; }
break;
case 15:
#line 298 "parse.yacc"
{
			    defaults_matches = user_matches;
			    pop;
			}
break;
case 16:
#line 302 "parse.yacc"
{ push; }
break;
case 17:
#line 302 "parse.yacc"
{
			    defaults_matches = yyvsp[0].BOOLEAN == TRUE;
			    pop;
			}
break;
case 18:
#line 306 "parse.yacc"
{ push; }
break;
case 19:
#line 306 "parse.yacc"
{
			    defaults_matches = host_matches;
			    pop;
			}
break;
case 22:
#line 316 "parse.yacc"
{
			    if (defaults_matches == TRUE &&
				!set_default(yyvsp[0].string, NULL, TRUE)) {
				yyerror(NULL);
				YYERROR;
			    }
			    free(yyvsp[0].string);
			}
break;
case 23:
#line 324 "parse.yacc"
{
			    if (defaults_matches == TRUE &&
				!set_default(yyvsp[0].string, NULL, FALSE)) {
				yyerror(NULL);
				YYERROR;
			    }
			    free(yyvsp[0].string);
			}
break;
case 24:
#line 332 "parse.yacc"
{
			    if (defaults_matches == TRUE &&
				!set_default(yyvsp[-2].string, yyvsp[0].string, TRUE)) {
				yyerror(NULL);
				YYERROR;
			    }
			    free(yyvsp[-2].string);
			    free(yyvsp[0].string);
			}
break;
case 25:
#line 341 "parse.yacc"
{
			    if (defaults_matches == TRUE &&
				!set_default(yyvsp[-2].string, yyvsp[0].string, '+')) {
				yyerror(NULL);
				YYERROR;
			    }
			    free(yyvsp[-2].string);
			    free(yyvsp[0].string);
			}
break;
case 26:
#line 350 "parse.yacc"
{
			    if (defaults_matches == TRUE &&
				!set_default(yyvsp[-2].string, yyvsp[0].string, '-')) {
				yyerror(NULL);
				YYERROR;
			    }
			    free(yyvsp[-2].string);
			    free(yyvsp[0].string);
			}
break;
case 29:
#line 365 "parse.yacc"
{
			    /*
			     * We already did a push if necessary in
			     * cmndspec so just reset some values so
			     * the next 'privilege' gets a clean slate.
			     */
			    host_matches = -1;
			    runas_matches = -1;
			    no_passwd = def_authenticate ? -1 : TRUE;
			    no_execve = def_noexec ? TRUE : -1;
			}
break;
case 30:
#line 378 "parse.yacc"
{
			    if (yyvsp[0].BOOLEAN != -1)
				host_matches = yyvsp[0].BOOLEAN;
			}
break;
case 31:
#line 382 "parse.yacc"
{
			    if (yyvsp[0].BOOLEAN != -1)
				host_matches = ! yyvsp[0].BOOLEAN;
			}
break;
case 32:
#line 388 "parse.yacc"
{
			    yyval.BOOLEAN = TRUE;
			}
break;
case 33:
#line 391 "parse.yacc"
{
			    if (addr_matches(yyvsp[0].string))
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;
			    free(yyvsp[0].string);
			}
break;
case 34:
#line 398 "parse.yacc"
{
			    if (netgr_matches(yyvsp[0].string, user_host, user_shost, NULL))
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;
			    free(yyvsp[0].string);
			}
break;
case 35:
#line 405 "parse.yacc"
{
			    if (hostname_matches(user_shost, user_host, yyvsp[0].string) == 0)
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;
			    free(yyvsp[0].string);
			}
break;
case 36:
#line 412 "parse.yacc"
{
			    aliasinfo *aip = find_alias(yyvsp[0].string, HOST_ALIAS);

			    /* could be an all-caps hostname */
			    if (aip)
				yyval.BOOLEAN = aip->val;
			    else if (strcasecmp(user_shost, yyvsp[0].string) == 0)
				yyval.BOOLEAN = TRUE;
			    else {
				if (pedantic) {
				    (void) fprintf(stderr,
					"%s: undeclared Host_Alias `%s' referenced near line %d\n",
					(pedantic == 1) ? "Warning" : "Error", yyvsp[0].string, sudolineno);
				    if (pedantic > 1) {
					yyerror(NULL);
					YYERROR;
				    }
				}
				yyval.BOOLEAN = -1;
			    }
			    free(yyvsp[0].string);
			}
break;
case 39:
#line 440 "parse.yacc"
{
			    /*
			     * Push the entry onto the stack if it is worth
			     * saving and clear cmnd_matches for next cmnd.
			     *
			     * We need to save at least one entry on
			     * the stack so sudoers_lookup() can tell that
			     * the user was listed in sudoers.  Also, we
			     * need to be able to tell whether or not a
			     * user was listed for this specific host.
			     *
			     * If keepall is set and the user matches then
			     * we need to keep entries around too...
			     */
			    if (user_matches != -1 && host_matches != -1 &&
				cmnd_matches != -1 && runas_matches != -1)
				pushcp;
			    else if (user_matches != -1 && (top == 1 ||
				(top == 2 && host_matches != -1 &&
				match[0].host == -1)))
				pushcp;
			    else if (user_matches == TRUE && keepall)
				pushcp;
			    cmnd_matches = -1;
			}
break;
case 40:
#line 467 "parse.yacc"
{
			    if (yyvsp[0].BOOLEAN != -1)
				cmnd_matches = yyvsp[0].BOOLEAN;
			}
break;
case 41:
#line 471 "parse.yacc"
{
			    if (printmatches == TRUE) {
				if (in_alias == TRUE)
				    append_entries("!", ", ");
				else if (host_matches == TRUE &&
				    user_matches == TRUE)
				    append_cmnd("!", NULL);
			    }
			}
break;
case 42:
#line 479 "parse.yacc"
{
			    if (yyvsp[0].BOOLEAN != -1)
				cmnd_matches = ! yyvsp[0].BOOLEAN;
			}
break;
case 43:
#line 485 "parse.yacc"
{
			    if (printmatches == TRUE && host_matches == TRUE &&
				user_matches == TRUE) {
				if (runas_matches == -1) {
				    cm_list[cm_list_len].runas_len = 0;
				} else {
				    /* Inherit runas data. */
				    cm_list[cm_list_len].runas =
					estrdup(cm_list[cm_list_len-1].runas);
				    cm_list[cm_list_len].runas_len =
					cm_list[cm_list_len-1].runas_len;
				    cm_list[cm_list_len].runas_size =
					cm_list[cm_list_len-1].runas_size;
				}
			    }
			    /*
			     * If this is the first entry in a command list
			     * then check against default runas user.
			     */
			    if (runas_matches == -1)
				runas_matches = (strcmp(*user_runas,
				    def_runas_default) == 0);
			}
break;
case 44:
#line 508 "parse.yacc"
{
			    runas_matches = (yyvsp[0].BOOLEAN == TRUE ? TRUE : FALSE);
			}
break;
case 45:
#line 513 "parse.yacc"
{ ; }
break;
case 46:
#line 514 "parse.yacc"
{
			    /* Later entries override earlier ones. */
			    if (yyvsp[0].BOOLEAN != -1)
				yyval.BOOLEAN = yyvsp[0].BOOLEAN;
			    else
				yyval.BOOLEAN = yyvsp[-2].BOOLEAN;
			}
break;
case 47:
#line 523 "parse.yacc"
{ ; }
break;
case 48:
#line 524 "parse.yacc"
{
			    if (printmatches == TRUE) {
				if (in_alias == TRUE)
				    append_entries("!", ", ");
				else if (host_matches == TRUE &&
				    user_matches == TRUE)
				    append_runas("!", ", ");
			    }
			}
break;
case 49:
#line 532 "parse.yacc"
{
			    /* Set $$ to the negation of runasuser */
			    yyval.BOOLEAN = (yyvsp[0].BOOLEAN == -1 ? -1 : ! yyvsp[0].BOOLEAN);
			}
break;
case 50:
#line 538 "parse.yacc"
{
			    if (printmatches == TRUE) {
				if (in_alias == TRUE)
				    append_entries(yyvsp[0].string, ", ");
				else if (host_matches == TRUE &&
				    user_matches == TRUE)
				    append_runas(yyvsp[0].string, ", ");
			    }
			    if (strcmp(yyvsp[0].string, *user_runas) == 0)
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;
			    free(yyvsp[0].string);
			}
break;
case 51:
#line 552 "parse.yacc"
{
			    if (printmatches == TRUE) {
				if (in_alias == TRUE)
				    append_entries(yyvsp[0].string, ", ");
				else if (host_matches == TRUE &&
				    user_matches == TRUE)
				    append_runas(yyvsp[0].string, ", ");
			    }
			    if (usergr_matches(yyvsp[0].string, *user_runas))
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;
			    free(yyvsp[0].string);
			}
break;
case 52:
#line 566 "parse.yacc"
{
			    if (printmatches == TRUE) {
				if (in_alias == TRUE)
				    append_entries(yyvsp[0].string, ", ");
				else if (host_matches == TRUE &&
				    user_matches == TRUE)
				    append_runas(yyvsp[0].string, ", ");
			    }
			    if (netgr_matches(yyvsp[0].string, NULL, NULL, *user_runas))
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;
			    free(yyvsp[0].string);
			}
break;
case 53:
#line 580 "parse.yacc"
{
			    aliasinfo *aip = find_alias(yyvsp[0].string, RUNAS_ALIAS);

			    if (printmatches == TRUE) {
				if (in_alias == TRUE)
				    append_entries(yyvsp[0].string, ", ");
				else if (host_matches == TRUE &&
				    user_matches == TRUE)
				    append_runas(yyvsp[0].string, ", ");
			    }
			    /* could be an all-caps username */
			    if (aip)
				yyval.BOOLEAN = aip->val;
			    else if (strcmp(yyvsp[0].string, *user_runas) == 0)
				yyval.BOOLEAN = TRUE;
			    else {
				if (pedantic) {
				    (void) fprintf(stderr,
					"%s: undeclared Runas_Alias `%s' referenced near line %d\n",
					(pedantic == 1) ? "Warning" : "Error", yyvsp[0].string, sudolineno);
				    if (pedantic > 1) {
					yyerror(NULL);
					YYERROR;
				    }
				}
				yyval.BOOLEAN = -1;
			    }
			    free(yyvsp[0].string);
			}
break;
case 54:
#line 609 "parse.yacc"
{
			    if (printmatches == TRUE) {
				if (in_alias == TRUE)
				    append_entries("ALL", ", ");
				else if (host_matches == TRUE &&
				    user_matches == TRUE)
				    append_runas("ALL", ", ");
			    }
			    yyval.BOOLEAN = TRUE;
			}
break;
case 55:
#line 621 "parse.yacc"
{
			    /* Inherit NOPASSWD/PASSWD status. */
			    if (printmatches == TRUE && host_matches == TRUE &&
				user_matches == TRUE) {
				if (no_passwd == TRUE)
				    cm_list[cm_list_len].nopasswd = TRUE;
				else
				    cm_list[cm_list_len].nopasswd = FALSE;
			    }
			}
break;
case 56:
#line 631 "parse.yacc"
{
			    no_passwd = TRUE;
			    if (printmatches == TRUE && host_matches == TRUE &&
				user_matches == TRUE)
				cm_list[cm_list_len].nopasswd = TRUE;
			}
break;
case 57:
#line 637 "parse.yacc"
{
			    no_passwd = FALSE;
			    if (printmatches == TRUE && host_matches == TRUE &&
				user_matches == TRUE)
				cm_list[cm_list_len].nopasswd = FALSE;
			}
break;
case 58:
#line 645 "parse.yacc"
{
			    /* Inherit NOEXEC/EXEC status. */
			    if (printmatches == TRUE && host_matches == TRUE &&
				user_matches == TRUE) {
				if (no_execve == TRUE)
				    cm_list[cm_list_len].noexecve = TRUE;
				else
				    cm_list[cm_list_len].noexecve = FALSE;
			    }
			}
break;
case 59:
#line 655 "parse.yacc"
{
			    no_execve = TRUE;
			    if (printmatches == TRUE && host_matches == TRUE &&
				user_matches == TRUE)
				cm_list[cm_list_len].noexecve = TRUE;
			}
break;
case 60:
#line 661 "parse.yacc"
{
			    no_execve = FALSE;
			    if (printmatches == TRUE && host_matches == TRUE &&
				user_matches == TRUE)
				cm_list[cm_list_len].noexecve = FALSE;
			}
break;
case 61:
#line 669 "parse.yacc"
{
			    if (printmatches == TRUE) {
				if (in_alias == TRUE)
				    append_entries("ALL", ", ");
				else if (host_matches == TRUE &&
				    user_matches == TRUE) {
				    append_cmnd("ALL", NULL);
				    expand_match_list();
				}
			    }

			    yyval.BOOLEAN = TRUE;

			    if (safe_cmnd)
				free(safe_cmnd);
			    safe_cmnd = estrdup(user_cmnd);
			}
break;
case 62:
#line 686 "parse.yacc"
{
			    aliasinfo *aip;

			    if (printmatches == TRUE) {
				if (in_alias == TRUE)
				    append_entries(yyvsp[0].string, ", ");
				else if (host_matches == TRUE &&
				    user_matches == TRUE) {
				    append_cmnd(yyvsp[0].string, NULL);
				    expand_match_list();
				}
			    }

			    if ((aip = find_alias(yyvsp[0].string, CMND_ALIAS)))
				yyval.BOOLEAN = aip->val;
			    else {
				if (pedantic) {
				    (void) fprintf(stderr,
					"%s: undeclared Cmnd_Alias `%s' referenced near line %d\n",
					(pedantic == 1) ? "Warning" : "Error", yyvsp[0].string, sudolineno);
				    if (pedantic > 1) {
					yyerror(NULL);
					YYERROR;
				    }
				}
				yyval.BOOLEAN = -1;
			    }
			    free(yyvsp[0].string);
			}
break;
case 63:
#line 715 "parse.yacc"
{
			    if (printmatches == TRUE) {
				if (in_alias == TRUE) {
				    append_entries(yyvsp[0].command.cmnd, ", ");
				    if (yyvsp[0].command.args)
					append_entries(yyvsp[0].command.args, " ");
				}
				if (host_matches == TRUE &&
				    user_matches == TRUE)  {
				    append_cmnd(yyvsp[0].command.cmnd, NULL);
				    if (yyvsp[0].command.args)
					append_cmnd(yyvsp[0].command.args, " ");
				    expand_match_list();
				}
			    }

			    if (command_matches(user_cmnd, user_args,
				yyvsp[0].command.cmnd, yyvsp[0].command.args))
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;

			    free(yyvsp[0].command.cmnd);
			    if (yyvsp[0].command.args)
				free(yyvsp[0].command.args);
			}
break;
case 66:
#line 747 "parse.yacc"
{ push; }
break;
case 67:
#line 747 "parse.yacc"
{
			    if ((host_matches != -1 || pedantic) &&
				!add_alias(yyvsp[-3].string, HOST_ALIAS, host_matches)) {
				yyerror(NULL);
				YYERROR;
			    }
			    pop;
			}
break;
case 72:
#line 765 "parse.yacc"
{
			    push;
			    if (printmatches == TRUE) {
				in_alias = TRUE;
				/* Allocate space for ga_list if necessary. */
				expand_ga_list();
				ga_list[ga_list_len-1].type = CMND_ALIAS;
				ga_list[ga_list_len-1].alias = estrdup(yyvsp[0].string);
			     }
			}
break;
case 73:
#line 774 "parse.yacc"
{
			    if ((cmnd_matches != -1 || pedantic) &&
				!add_alias(yyvsp[-3].string, CMND_ALIAS, cmnd_matches)) {
				yyerror(NULL);
				YYERROR;
			    }
			    pop;
			    free(yyvsp[-3].string);

			    if (printmatches == TRUE)
				in_alias = FALSE;
			}
break;
case 74:
#line 788 "parse.yacc"
{ ; }
break;
case 78:
#line 796 "parse.yacc"
{
			    if (printmatches == TRUE) {
				in_alias = TRUE;
				/* Allocate space for ga_list if necessary. */
				expand_ga_list();
				ga_list[ga_list_len-1].type = RUNAS_ALIAS;
				ga_list[ga_list_len-1].alias = estrdup(yyvsp[0].string);
			    }
			}
break;
case 79:
#line 804 "parse.yacc"
{
			    if ((yyvsp[0].BOOLEAN != -1 || pedantic) &&
				!add_alias(yyvsp[-3].string, RUNAS_ALIAS, yyvsp[0].BOOLEAN)) {
				yyerror(NULL);
				YYERROR;
			    }
			    free(yyvsp[-3].string);

			    if (printmatches == TRUE)
				in_alias = FALSE;
			}
break;
case 82:
#line 821 "parse.yacc"
{ push; }
break;
case 83:
#line 821 "parse.yacc"
{
			    if ((user_matches != -1 || pedantic) &&
				!add_alias(yyvsp[-3].string, USER_ALIAS, user_matches)) {
				yyerror(NULL);
				YYERROR;
			    }
			    pop;
			    free(yyvsp[-3].string);
			}
break;
case 86:
#line 836 "parse.yacc"
{
			    if (yyvsp[0].BOOLEAN != -1)
				user_matches = yyvsp[0].BOOLEAN;
			}
break;
case 87:
#line 840 "parse.yacc"
{
			    if (yyvsp[0].BOOLEAN != -1)
				user_matches = ! yyvsp[0].BOOLEAN;
			}
break;
case 88:
#line 846 "parse.yacc"
{
			    if (strcmp(yyvsp[0].string, user_name) == 0)
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;
			    free(yyvsp[0].string);
			}
break;
case 89:
#line 853 "parse.yacc"
{
			    if (usergr_matches(yyvsp[0].string, user_name))
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;
			    free(yyvsp[0].string);
			}
break;
case 90:
#line 860 "parse.yacc"
{
			    if (netgr_matches(yyvsp[0].string, NULL, NULL, user_name))
				yyval.BOOLEAN = TRUE;
			    else
				yyval.BOOLEAN = -1;
			    free(yyvsp[0].string);
			}
break;
case 91:
#line 867 "parse.yacc"
{
			    aliasinfo *aip = find_alias(yyvsp[0].string, USER_ALIAS);

			    /* could be an all-caps username */
			    if (aip)
				yyval.BOOLEAN = aip->val;
			    else if (strcmp(yyvsp[0].string, user_name) == 0)
				yyval.BOOLEAN = TRUE;
			    else {
				if (pedantic) {
				    (void) fprintf(stderr,
					"%s: undeclared User_Alias `%s' referenced near line %d\n",
					(pedantic == 1) ? "Warning" : "Error", yyvsp[0].string, sudolineno);
				    if (pedantic > 1) {
					yyerror(NULL);
					YYERROR;
				    }
				}
				yyval.BOOLEAN = -1;
			    }
			    free(yyvsp[0].string);
			}
break;
case 92:
#line 889 "parse.yacc"
{
			    yyval.BOOLEAN = TRUE;
			}
break;
#line 1922 "sudo.tab.c"
    }
    yyssp -= yym;
    yystate = *yyssp;
    yyvsp -= yym;
    yym = yylhs[yyn];
    if (yystate == 0 && yym == 0)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: after reduction, shifting from state 0 to\
 state %d\n", YYPREFIX, YYFINAL);
#endif
        yystate = YYFINAL;
        *++yyssp = YYFINAL;
        *++yyvsp = yyval;
        if (yychar < 0)
        {
            if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
            if (yydebug)
            {
                yys = 0;
                if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
                if (!yys) yys = "illegal-symbol";
                printf("%sdebug: state %d, reading %d (%s)\n",
                        YYPREFIX, YYFINAL, yychar, yys);
            }
#endif
        }
        if (yychar == 0) goto yyaccept;
        goto yyloop;
    }
    if ((yyn = yygindex[yym]) && (yyn += yystate) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yystate)
        yystate = yytable[yyn];
    else
        yystate = yydgoto[yym];
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: after reduction, shifting from state %d \
to state %d\n", YYPREFIX, *yyssp, yystate);
#endif
    if (yyssp >= yysslim && yygrowstack())
    {
        goto yyoverflow;
    }
    *++yyssp = yystate;
    *++yyvsp = yyval;
    goto yyloop;
yyoverflow:
    yyerror("yacc stack overflow");
yyabort:
    return (1);
yyaccept:
    return (0);
}
