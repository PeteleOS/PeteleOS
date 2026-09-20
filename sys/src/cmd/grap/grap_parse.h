/*
 * Token numbers for grap (hand-parser replacement).
 * Replaces yacc-generated y.tab.h (same order: FRAME 257 through
 * ST 320 in %token order, then OR 321 through UMINUS 330 for tokens
 * first declared in %left/%right lines); single-char tokens keep
 * ASCII codes. YYSTYPE and yylval/yyval live in grap.h as before.
 * Included directly by grap_parse.c and indirectly by the rest of
 * grap via the static y.tab.h shim.
 */
#ifndef GRAP_PARSE_H
#define GRAP_PARSE_H

extern int yyparse(void);

enum {
	FRAME = 257,
	TICKS = 258,
	GRID = 259,
	LABEL = 260,
	COORD = 261,
	LINE = 262,
	ARROW = 263,
	CIRCLE = 264,
	DRAW = 265,
	NEW = 266,
	PLOT = 267,
	NEXT = 268,
	PIC = 269,
	COPY = 270,
	THRU = 271,
	UNTIL = 272,
	FOR = 273,
	FROM = 274,
	TO = 275,
	BY = 276,
	AT = 277,
	WITH = 278,
	IF = 279,
	GRAPH = 280,
	THEN = 281,
	ELSE = 282,
	DOSTR = 283,
	DOT = 284,
	DASH = 285,
	INVIS = 286,
	SOLID = 287,
	TEXT = 288,
	JUST = 289,
	SIZE = 290,
	LOG = 291,
	EXP = 292,
	SIN = 293,
	COS = 294,
	ATAN2 = 295,
	SQRT = 296,
	RAND = 297,
	MAX = 298,
	MIN = 299,
	INT = 300,
	PRINT = 301,
	SPRINTF = 302,
	X = 303,
	Y = 304,
	SIDE = 305,
	IN = 306,
	OUT = 307,
	OFF = 308,
	UP = 309,
	DOWN = 310,
	ACROSS = 311,
	HEIGHT = 312,
	WIDTH = 313,
	RADIUS = 314,
	NUMBER = 315,
	NAME = 316,
	VARNAME = 317,
	DEFNAME = 318,
	STRING = 319,
	ST = 320,
	OR = 321,
	AND = 322,
	GT = 323,
	LT = 324,
	LE = 325,
	GE = 326,
	EQ = 327,
	NE = 328,
	NOT = 329,
	UMINUS = 330,
};

#endif
