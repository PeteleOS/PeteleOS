#ifndef _SYN_PARSE_H_
#define _SYN_PARSE_H_

/*
 * Token numbers and YYSTYPE for rc (hand-written parser replacement).
 * Replaces yacc-generated x.tab.h (same numbers: FIRSTTOKEN 257
 * through LASTTOKEN 278, single-char tokens keep ASCII codes).
 * Included by rc.h and indirectly by the rest of rc.
 */

typedef union {
	struct tree	*tree;
} YYSTYPE;

extern YYSTYPE yylval;

/*
 * Token numbers (must match Plan 9 yacc output for syn.y %term lines).
 * Order: first %term line, then second, then third.
 */
#define	FOR	257
#define	IN	258
#define	WHILE	259
#define	IF	260
#define	NOT	261
#define	TWIDDLE	262
#define	BANG	263
#define	SUBSHELL 264
#define	SWITCH	265
#define	FN	266
#define	WORD	267
#define	REDIR	268
#define	DUP	269
#define	PIPE	270
#define	SUB	271
#define	SIMPLE	272
#define	ARGLIST	273
#define	WORDS	274
#define	BRACE	275
#define	PAREN	276
#define	PCMD	277
#define	PIPEFD	278

#define	YYMAXDEPTH	500
#define	YYPREFIX
#define	YYSTYPE_IS_DECLARED

#endif
