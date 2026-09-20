/*
 * Token numbers for eqn (hand-parser replacement).
 * Replaces yacc-generated y.tab.h: same numbers as prevy.tab.h
 * (CONTIG 57346 through UTILDE 57402); single-char tokens '{' '}'
 * keep ASCII codes. Included directly by eqn_parse.c and indirectly
 * by the rest of eqn via the static y.tab.h shim.
 */
#ifndef EQN_PARSE_H
#define EQN_PARSE_H

#ifndef YYSTYPE
#define YYSTYPE int
#endif
extern YYSTYPE yylval;
extern YYSTYPE yyval;
extern int yyparse(void);

enum {
	CONTIG = 57346,
	QTEXT = 57347,
	SPACE = 57348,
	THIN = 57349,
	TAB = 57350,
	MATRIX = 57351,
	LCOL = 57352,
	CCOL = 57353,
	RCOL = 57354,
	COL = 57355,
	ABOVE = 57356,
	MARK = 57357,
	LINEUP = 57358,
	SUM = 57359,
	INT = 57360,
	PROD = 57361,
	UNION = 57362,
	INTER = 57363,
	DEFINE = 57364,
	TDEFINE = 57365,
	NDEFINE = 57366,
	DELIM = 57367,
	GSIZE = 57368,
	GFONT = 57369,
	INCLUDE = 57370,
	IFDEF = 57371,
	DOTEQ = 57372,
	DOTEN = 57373,
	FROM = 57374,
	TO = 57375,
	OVER = 57376,
	SQRT = 57377,
	SUP = 57378,
	SUB = 57379,
	SIZE = 57380,
	FONT = 57381,
	ROMAN = 57382,
	ITALIC = 57383,
	BOLD = 57384,
	FAT = 57385,
	UP = 57386,
	DOWN = 57387,
	BACK = 57388,
	FWD = 57389,
	LEFT = 57390,
	RIGHT = 57391,
	DOT = 57392,
	DOTDOT = 57393,
	HAT = 57394,
	TILDE = 57395,
	BAR = 57396,
	LOWBAR = 57397,
	HIGHBAR = 57398,
	UNDER = 57399,
	VEC = 57400,
	DYAD = 57401,
	UTILDE = 57402,
};

#endif
