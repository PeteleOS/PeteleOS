#ifndef CC_PARSE_H
#define CC_PARSE_H
/*
 * Token numbers and YYSTYPE for cc hand parser (replaces y.tab.h).
 * Single-char tokens use ASCII; multi-char tokens > 255.
 */
#include "cc.h"

typedef union {
	Node *node;
	Sym *sym;
	Type *type;
	struct {
		Type *t;
		uchar c;
	} tycl;
	struct {
		Type *t1;
		Type *t2;
		Type *t3;
		uchar c;
	} tyty;
	struct {
		char *s;
		long l;
	} sval;
	long lval;
	double dval;
	vlong vval;
} YYSTYPE;

extern YYSTYPE yylval;
int yyparse(void);

enum {
	LPE = 257,
	LME,
	LMLE,
	LDVE,
	LMDE,
	LRSHE,
	LLSHE,
	LANDE,
	LXORE,
	LORE,
	LOROR,
	LANDAND,
	LEQ,
	LNE,
	LLE,
	LGE,
	LLSH,
	LRSH,
	LMM,
	LPP,
	LMG,
	LNAME,
	LTYPE,
	LFCONST,
	LDCONST,
	LCONST,
	LLCONST,
	LUCONST,
	LULCONST,
	LVLCONST,
	LUVLCONST,
	LSTRING,
	LLSTRING,
	LAUTO,
	LBREAK,
	LCASE,
	LCHAR,
	LCONTINUE,
	LDEFAULT,
	LDO,
	LDOUBLE,
	LELSE,
	LEXTERN,
	LFLOAT,
	LFOR,
	LGOTO,
	LIF,
	LINT,
	LLONG,
	LREGISTER,
	LRETURN,
	LSHORT,
	LSIZEOF,
	LUSED,
	LSTATIC,
	LSTRUCT,
	LSWITCH,
	LTYPEDEF,
	LTYPESTR,
	LUNION,
	LUNSIGNED,
	LWHILE,
	LVOID,
	LENUM,
	LSIGNED,
	LCONSTNT,
	LVOLATILE,
	LSET,
	LSIGNOF,
	LRESTRICT,
	LINLINE
};

#endif
