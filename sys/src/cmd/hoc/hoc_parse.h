#ifndef HOC_PARSE_H
#define HOC_PARSE_H
/* Token defines compatible with old y.tab.h (single-char = ASCII). */
enum {
	NUMBER = 257,
	STRING = 258,
	PRINT = 259,
	VAR = 260,
	BLTIN = 261,
	UNDEF = 262,
	WHILE = 263,
	FOR = 264,
	IF = 265,
	ELSE = 266,
	FUNCTION = 267,
	PROCEDURE = 268,
	RETURN = 269,
	FUNC = 270,
	PROC = 271,
	READ = 272,
	ADDEQ = 273,
	SUBEQ = 274,
	MULEQ = 275,
	DIVEQ = 276,
	MODEQ = 277,
	OR = 278,
	AND = 279,
	GT = 280,
	GE = 281,
	LT = 282,
	LE = 283,
	EQ = 284,
	NE = 285,
	UNARYMINUS = 286,
	NOT = 287,
	INC = 288,
	DEC = 289,
};
typedef union {
	Symbol	*sym;
	Inst	*inst;
	int	narg;
	Formal	*formals;
} YYSTYPE;
extern YYSTYPE yylval;
#endif
