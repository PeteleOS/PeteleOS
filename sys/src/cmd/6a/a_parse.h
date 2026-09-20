/* Hand-written replacement for y.tab.h generated from a.y (6a/amd64). */
#ifndef _A_PARSE_H_6A_
#define _A_PARSE_H_6A_

typedef union {
	Sym	*sym;
	vlong	lval;
	double	dval;
	char	sval[8];
	Gen	gen;
	Gen2	gen2;
} YYSTYPE;

extern YYSTYPE yylval;

enum {
	LTYPE0 = 257,
	LTYPE1,
	LTYPE2,
	LTYPE3,
	LTYPE4,
	LTYPEC,
	LTYPED,
	LTYPEN,
	LTYPER,
	LTYPET,
	LTYPES,
	LTYPEM,
	LTYPEI,
	LTYPEG,
	LTYPEXC,
	LTYPEX,
	LTYPEY,
	LTYPERT,
	LCONST,
	LFP,
	LPC,
	LSB,
	LBREG,
	LLREG,
	LSREG,
	LFREG,
	LMREG,
	LXREG,
	LYREG,
	LFCONST,
	LSCONST,
	LSP,
	LNAME,
	LLAB,
	LVAR,
};

#endif
