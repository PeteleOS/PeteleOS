#ifndef DBG_PARSE_H
#define DBG_PARSE_H
/*
 * Token numbers and YYSTYPE for acid hand parser (replaces y.tab.h).
 * Single-char tokens use ASCII; multi-char tokens > 255.
 */
typedef struct Node Node;
typedef struct Lsym Lsym;
typedef struct String String;

typedef union {
	Node *node;
	Lsym *sym;
	uvlong ival;
	float fval;
	String *string;
} YYSTYPE;

extern YYSTYPE yylval;
int yyparse(void);
int yylex(void);

enum {
	Tid = 257,
	Tconst,
	Tfmt,
	Tfconst,
	Tstring,
	Tif,
	Tdo,
	Tthen,
	Telse,
	Twhile,
	Tloop,
	Thead,
	Ttail,
	Tappend,
	Tfn,
	Tret,
	Tlocal,
	Tcomplex,
	Twhat,
	Tdelete,
	Teval,
	Tbuiltin,
	Toror,
	Tandand,
	Teq,
	Tneq,
	Tleq,
	Tgeq,
	Tlsh,
	Trsh,
	Tdec,
	Tinc,
	Tindir
};

#endif
