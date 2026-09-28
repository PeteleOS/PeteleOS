/* Hand-written replacement for y.tab.h generated from a.y (9a/ppc64). */
#ifndef _A_PARSE_H_9A_
#define _A_PARSE_H_9A_

typedef union {
	Sym	*sym;
	vlong	lval;
	double	dval;
	char	sval[8];
	Gen	gen;
} YYSTYPE;

extern YYSTYPE yylval;

enum {
	LMOVW = 257,
	LMOVB,
	LABS,
	LLOGW,
	LSHW,
	LADDW,
	LCMP,
	LCROP,
	LBRA,
	LFMOV,
	LFCONV,
	LFCMP,
	LFADD,
	LFMA,
	LTRAP,
	LXORW,
	LNOP,
	LEND,
	LRETT,
	LWORD,
	LTEXT,
	LDATA,
	LRETRN,
	LCONST,
	LSP,
	LSB,
	LFP,
	LPC,
	LCREG,
	LFLUSH,
	LREG,
	LFREG,
	LR,
	LCR,
	LF,
	LFPSCR,
	LLR,
	LCTR,
	LSPR,
	LSPREG,
	LSEG,
	LMSR,
	LSCHED,
	LXLD,
	LXST,
	LXOP,
	LXMV,
	LRLWM,
	LMOVMW,
	LMOVEM,
	LMOVFL,
	LMTFSB,
	LMA,
	LFCONST,
	LSCONST,
	LNAME,
	LLAB,
	LVAR,
};

#endif
