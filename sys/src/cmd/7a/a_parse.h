/* Hand-written replacement for y.tab.h generated from a.y (7a/arm64). */
#ifndef _A_PARSE_H_7A_
#define _A_PARSE_H_7A_

typedef union {
	Sym	*sym;
	vlong	lval;
	double	dval;
	char	sval[NSNAME];
	Gen	gen;
} YYSTYPE;

extern YYSTYPE yylval;

enum {
	LTYPE0 = 257,
	LTYPE1,
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
	LTYPEL,
	LTYPEM,
	LTYPEN,
	LTYPEO,
	LTYPEP,
	LTYPEQ,
	LTYPER,
	LTYPES,
	LTYPET,
	LTYPEU,
	LTYPEV,
	LTYPEW,
	LTYPEX,
	LTYPEY,
	LTYPEZ,
	LMOVK,
	LDMB,
	LSTXR,
	LCONST,
	LSP,
	LSB,
	LFP,
	LPC,
	LR,
	LREG,
	LF,
	LFREG,
	LV,
	LVREG,
	LC,
	LCREG,
	LFCR,
	LFCSEL,
	LCOND,
	LS,
	LAT,
	LEXT,
	LSPR,
	LSPREG,
	LVTYPE,
	LFCONST,
	LSCONST,
	LNAME,
	LLAB,
	LVAR,
};

#endif
