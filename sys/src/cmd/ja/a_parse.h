/* Hand-written replacement for y.tab.h generated from a.y (ja/riscv64). */
#ifndef _A_PARSE_H_JA_
#define _A_PARSE_H_JA_

typedef union {
	Sym	*sym;
	vlong	lval;
	double	dval;
	char	sval[8];
	Gen	gen;
} YYSTYPE;

extern YYSTYPE yylval;

enum {
	LADD = 257,
	LMUL,
	LBEQ,
	LBR,
	LBRET,
	LCALL,
	LFLT2,
	LFLT3,
	LMOVB,
	LMOVBU,
	LMOVW,
	LMOVF,
	LLUI,
	LSYS,
	LSYS0,
	LCSR,
	LSWAP,
	LAMO,
	LCONST,
	LSP,
	LSB,
	LFP,
	LPC,
	LREG,
	LFREG,
	LR,
	FR,
	LCTL,
	LDATA,
	LTEXT,
	LWORD,
	LSCONST,
	LFCONST,
	LNAME,
	LLAB,
	LVAR,
};

#endif
