/*
 * Token numbers and YYSTYPE for awk (hand-parser replacement).
 * Replaces yacc-generated y.tab.h (same numbers: FIRSTTOKEN 257
 * through LASTTOKEN 349, single-char tokens keep ASCII codes).
 * Included directly by awkgram_parse.c and indirectly by the rest
 * of awk via the static y.tab.h shim.
 */
#ifndef AWKGRAM_PARSE_H
#define AWKGRAM_PARSE_H

typedef union {
	struct Node	*p;
	struct Cell	*cp;
	int	i;
	char	*s;
} YYSTYPE;
extern YYSTYPE yylval;
extern int yyparse(void);

enum {
	FIRSTTOKEN = 257,
	PROGRAM = 258,
	PASTAT = 259,
	PASTAT2 = 260,
	XBEGIN = 261,
	XEND = 262,
	NL = 263,
	ARRAY = 264,
	MATCH = 265,
	NOTMATCH = 266,
	MATCHOP = 267,
	FINAL = 268,
	DOT = 269,
	ALL = 270,
	CCL = 271,
	NCCL = 272,
	CHAR = 273,
	OR = 274,
	STAR = 275,
	QUEST = 276,
	PLUS = 277,
	EMPTYRE = 278,
	AND = 279,
	BOR = 280,
	APPEND = 281,
	EQ = 282,
	GE = 283,
	GT = 284,
	LE = 285,
	LT = 286,
	NE = 287,
	IN = 288,
	ARG = 289,
	BLTIN = 290,
	BREAK = 291,
	CLOSE = 292,
	CONTINUE = 293,
	DELETE = 294,
	DO = 295,
	EXIT = 296,
	FOR = 297,
	FUNC = 298,
	SUB = 299,
	GSUB = 300,
	IF = 301,
	INDEX = 302,
	LSUBSTR = 303,
	MATCHFCN = 304,
	NEXT = 305,
	NEXTFILE = 306,
	ADD = 307,
	MINUS = 308,
	MULT = 309,
	DIVIDE = 310,
	MOD = 311,
	ASSIGN = 312,
	ASGNOP = 313,
	ADDEQ = 314,
	SUBEQ = 315,
	MULTEQ = 316,
	DIVEQ = 317,
	MODEQ = 318,
	POWEQ = 319,
	PRINT = 320,
	PRINTF = 321,
	SPRINTF = 322,
	ELSE = 323,
	INTEST = 324,
	CONDEXPR = 325,
	POSTINCR = 326,
	PREINCR = 327,
	POSTDECR = 328,
	PREDECR = 329,
	VAR = 330,
	IVAR = 331,
	VARNF = 332,
	CALL = 333,
	NUMBER = 334,
	STRING = 335,
	REGEXPR = 336,
	GETLINE = 337,
	RETURN = 338,
	SPLIT = 339,
	SUBSTR = 340,
	WHILE = 341,
	CAT = 342,
	NOT = 343,
	UMINUS = 344,
	POWER = 345,
	DECR = 346,
	INCR = 347,
	INDIRECT = 348,
	LASTTOKEN = 349,
};

#endif
