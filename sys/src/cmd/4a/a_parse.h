/* Hand-written replacement for y.tab.h generated from a.y (4a/arm). */
#ifndef _A_PARSE_H_4A_
#define _A_PARSE_H_4A_

typedef union {
	Sym	*sym;
	vlong	lval;
	double	dval;
	char	sval[8];
	Gen	gen;
} YYSTYPE;

extern YYSTYPE yylval;

enum {
	LTYPE1 = 257,
	LTYPE2,
	LTYPE3,
	LTYPE4,
	LTYPE5,
	LTYPE6,
	LTYPE7,
	LTYPE8,
	LTYPE9,
	LTYPEA,
	LTYPEB,
	LTYPEC,
	LTYPED,
	LTYPEE,
	LTYPEF,
	LTYPEG,
	LTYPEH,
	LTYPEI,
	LTYPEJ,
	LTYPEK,
	LCONST,
	LSP,
	LSB,
	LFP,
	LPC,
	LHI,
	LLO,
	LMREG,
	LTYPEX,
	LREG,
	LFREG,
	LFCREG,
	LR,
	LM,
	LF,
	LFCR,
	LSCHED,
	LFCONST,
	LSCONST,
	LVCONST,
	LNAME,
	LLAB,
	LVAR,
};

#endif
