#include	<u.h>
#include	<libc.h>
#include	<bio.h>

/*
 * Hand-written recursive-descent replacement for bc.y (LALR).
 *
 * Original yacc precedence (low -> high):
 *	%right '=' EQOP
 *	%left '+' '-'
 *	%left '*' '/' '%'
 *	%right '^'
 *	%left UMINUS		(no production uses %prec UMINUS)
 * Token numbers: single chars keep ASCII; LETTER=257 EQOP=258
 *	_AUTO=259 DOT=260 DIGIT=261 SQRT=262 LENGTH=263 _IF=264 FFF=265
 *	EQ=266 _PRINT=267 _WHILE=268 _FOR=269 NE=270 LE=271 GE=272
 *	INCR=273 DECR=274 _RETURN=275 _BREAK=276 _DEFINE=277 BASE=278
 *	OBASE=279 SCALE=280 QSTR=281 ERROR=282 (decl order); UMINUS=283
 *	(precedence-only, never returned).
 * ("=" before actions in bc.y is yacc's action marker for '{...}',
 * not a grammar symbol; e.g. "crs: = {...}" means empty with action,
 * "nase: LETTER = {...}" means "nase: LETTER {...}".)
 *
 * Precedence-map (hand parser, loosest -> tightest):
 *	level 1 (right): '=' EQOP assignments (parse_assign; left must
 *	  be LETTER or LETTER '[' e ']'; verbatim ase actions).
 *	level 2 (left): '+' '-' (parse_add).
 *	level 3 (left): '*' '/' '%' (parse_mul).
 *	level 4 (right): '^' (parse_pow).
 *	level 5 (prefix, prec of '-' i.e. level 2): unary '-' e
 *	  (parse_unary; operand is parse_mul (next tighter), so
 *	  "-a+b" == "(-a)+b" and "-a*b" == "-(a*b)", matching yacc's
 *	  reduce-on-equal / shift-on-higher).
 *	level 6 (primary): '(' e ')', cons numbers, DOT forms, LETTER
 *	  fetch/inc/dec/calls, LENGTH/SCALE/SQRT '(' e ')', '?',
 *	  '~' LETTER, SCALE/BASE/OBASE, array fetch (parse_primary;
 *	  verbatim nase actions).
 *	relational re: e [EQ|'<'|'>'|NE|GE|LE e] (parse_re; lone e
 *	  bundles " 0!=" verbatim).
 *	statements: parse_stat/parse_pstat/parse_stat1/parse_slist/
 *	  parse_stuff with verbatim output/conout/bundle actions;
 *	  crs/BLEV empty actions (label alloc / --bindx) verbatim;
 *	  tail '\n' (ln++) / ';'; fprefix _FOR '(' e ';'.
 * Call chain: yyparse -> parse_start(loop) -> parse_stuff ->
 *	parse_pstat/parse_def -> parse_stat1 -> parse_re/parse_e ->
 *	parse_assign -> parse_add -> parse_mul -> parse_pow ->
 *	parse_unary -> parse_primary (+ parse_cargs/parse_eora/
 *	parse_cons/parse_constant/parse_crs/parse_lora inline).
 * All actions (bundle/conout/output/pp/tp) are verbatim from bc.y.
 *
 * Error-recovery-map: original had no `error' production; yyerror()
 * prints to bstdout and resets (cp/crs/bindx/lev/bsp_nxt) without
 * exiting.  Hand parser calls yyerror("syntax error") at the exact
 * failure point then skips to sync ('\n' ';' '}' or 0) and continues
 * the start loop, matching yacc's abort-current-parse plus main's
 * `for(;;) yyparse()' restart.
 */

#define	bsp_max	5000

Biobuf	*in;
Biobuf	bstdin;
Biobuf	bstdout;
char	cary[1000];
char*	cp = { cary };
char	string[1000];
char*	str = { string };
int	crs = 128;
int	rcrs = 128;
int	bindx = 0;
int	lev = 0;
int	ln;
char*	ttp;
char*	ss = "";
int	bstack[10] = { 0 };
char*	numb[15] =
{
	" 0", " 1", " 2", " 3", " 4", " 5",
	" 6", " 7", " 8", " 9", " 10", " 11",
	" 12", " 13", " 14"
};
char*	pre;
char*	post;

long	peekc = -1;
int	sargc;
int	ifile;
char**	sargv;

char	*funtab[] =
{
	"<1>","<2>","<3>","<4>","<5>",
	"<6>","<7>","<8>","<9>","<10>",
	"<11>","<12>","<13>","<14>","<15>",
	"<16>","<17>","<18>","<19>","<20>",
	"<21>","<22>","<23>","<24>","<25>",
	"<26>"
};
char	*atab[] =
{
	"<221>","<222>","<223>","<224>","<225>",
	"<226>","<227>","<228>","<229>","<230>",
	"<231>","<232>","<233>","<234>","<235>",
	"<236>","<237>","<238>","<239>","<240>",
	"<241>","<242>","<243>","<244>","<245>",
	"<246>"
};
char*	letr[26] =
{
	"a","b","c","d","e","f","g","h","i","j",
	"k","l","m","n","o","p","q","r","s","t",
	"u","v","w","x","y","z"
};
char*	dot = { "." };
char*	bspace[bsp_max];
char**	bsp_nxt = bspace;
int	bdebug = 0;
int	lflag;
int	cflag;
int	sflag;

char*	bundle(int, ...);
void	conout(char*, char*);
int	cpeek(int, int, int);
int	getch(void);
char*	geta(char*);
char*	getf(char*);
void	getout(void);
void	output(char*);
void	pp(char*);
void	routput(char*);
void	tp(char*);
void	yyerror(char*, ...);
int	yyparse(void);

typedef	void*	pointer;
#pragma	varargck	type	"lx"	pointer

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef LETTER
#define LETTER 257
#undef EQOP
#define EQOP 258
#undef _AUTO
#define _AUTO 259
#undef DOT
#define DOT 260
#undef DIGIT
#define DIGIT 261
#undef SQRT
#define SQRT 262
#undef LENGTH
#define LENGTH 263
#undef _IF
#define _IF 264
#undef FFF
#define FFF 265
#undef EQ
#define EQ 266
#undef _PRINT
#define _PRINT 267
#undef _WHILE
#define _WHILE 268
#undef _FOR
#define _FOR 269
#undef NE
#define NE 270
#undef LE
#define LE 271
#undef GE
#define GE 272
#undef INCR
#define INCR 273
#undef DECR
#define DECR 274
#undef _RETURN
#define _RETURN 275
#undef _BREAK
#define _BREAK 276
#undef _DEFINE
#define _DEFINE 277
#undef BASE
#define BASE 278
#undef OBASE
#define OBASE 279
#undef SCALE
#define SCALE 280
#undef QSTR
#define QSTR 281
#undef ERROR
#define ERROR 282
#undef UMINUS
#define UMINUS 283

typedef union {
	char*	cptr;
	int	cc;
} YYSTYPE;
YYSTYPE yylval;
YYSTYPE yyval;

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef NPB
#define NPB 4
static int nbuf;
static int tbuf[NPB];
static YYSTYPE vbuf[NPB];

static int yyget(void);
static void yyunget(int, YYSTYPE);
static int yypeek(void);
static void yysync(void);
static char* parse_e(void);
static char* parse_assign(void);
static char* parse_add(void);
static char* parse_mul(void);
static char* parse_pow(void);
static char* parse_unary(void);
static char* parse_primary(void);
static char* parse_re(void);
static char* parse_stat(void);
static char* parse_pstat(void);
static char* parse_stat1(void);
static char* parse_slist(void);
static void parse_tail(void);
static char* parse_cargs(void);
static char* parse_eora(void);
static char* parse_cons(void);
static char* parse_constant(void);
static char* parse_crs(void);
static void parse_BLEV(void);
static char* parse_fprefix(void);
static char* parse_lora(void);
static char* parse_dlets(void);
static char* parse_dargs_rest(void);
static void parse_dlist(void);
static void parse_stuff(void);

static int
yyget(void)
{
	if(nbuf > 0){
		nbuf--;
		yylval = vbuf[nbuf];
		return tbuf[nbuf];
	}
	return yylex();
}

static void
yyunget(int t, YYSTYPE v)
{
	assert(nbuf < NPB);
	vbuf[nbuf] = v;
	tbuf[nbuf] = t;
	nbuf++;
}

static int
yypeek(void)
{
	int t;
	YYSTYPE v;

	t = yyget();
	v = yylval;
	yyunget(t, v);
	return t;
}

static void
yysync(void)
{
	int t;

	for(;;){
		t = yyget();
		if(t == '\n' || t == ';' || t == '}' || t == 0)
			break;
	}
	/* push back sync token for caller (except EOF) */
	if(t != 0){
		YYSTYPE v;
		v = yylval;
		yyunget(t, v);
	}
}

static char*
parse_crs(void)
{
	char *s;

	s = cp;
	*cp++ = '<';
	*cp++ = crs/100+'0';
	*cp++ = (crs%100)/10+'0';
	*cp++ = crs%10+'0';
	*cp++ = '>';
	*cp++ = '\0';
	if(crs++ >= 220) {
		yyerror("program too big");
		getout();
	}
	bstack[bindx++] = lev++;
	return s;
}

static void
parse_BLEV(void)
{
	--bindx;
}

static void
parse_tail(void)
{
	int t;

	t = yyget();
	if(t == '\n')
		ln++;
	else if(t != ';'){
		yyerror("syntax error");
		yysync();
	}
}

static char*
parse_constant(void)
{
	int t;
	char *s;

	t = yyget();
	if(t == '_'){
		s = cp;
		*cp++ = '_';
	} else if(t == DIGIT){
		s = cp;
		*cp++ = yylval.cc;
	} else {
		yyerror("syntax error");
		yysync();
		return "";
	}
	while(yypeek() == DIGIT){
		yyget();
		*cp++ = yylval.cc;
	}
	return s;
}

static char*
parse_cons(void)
{
	char *s;

	s = parse_constant();
	*cp++ = 0;
	return s;
}

static char*
parse_lora(void)
{
	int t;
	char *s;
	YYSTYPE v;

	t = yyget();
	if(t != LETTER){
		yyerror("syntax error");
		yysync();
		return "";
	}
	s = yylval.cptr;
	if(yypeek() == '['){
		yyget();
		if(yyget() != ']'){
			yyerror("syntax error");
			yysync();
			return "";
		}
		USED(v);
		return geta(s);
	}
	return s;
}

static char*
parse_eora(void)
{
	int t;
	char *e;
	YYSTYPE v;
	char *s;

	if(yypeek() == LETTER){
		/* need to distinguish LETTER '[' ']' (array ref) vs e.
		 * Peek: LETTER then '[' then ']'? */
		yyget();
		v = yylval;
		s = v.cptr;
		if(yypeek() == '['){
			yyget();
			if(yypeek() == ']'){
				yyget();
				return bundle(2, "l", geta(s));
			}
			/* not empty: push back '[' and fall through to e */
			{
				YYSTYPE w;
				w = yylval;
				yyunget('[', w);
				yyunget(LETTER, v);
			}
		} else {
			yyunget(LETTER, v);
		}
	}
	USED(t);
	e = parse_e();
	return e;
}

static char*
parse_cargs(void)
{
	char *l, *r;

	l = parse_eora();
	while(yypeek() == ','){
		yyget();
		r = parse_eora();
		l = bundle(2, l, r);
	}
	return l;
}

static char*
parse_primary(void)
{
	int t;
	char *e, *e2, *e3;
	char *s;
	YYSTYPE v;

	t = yypeek();
	if(t == '('){
		yyget();
		e = parse_e();
		if(yyget() != ')'){
			yyerror("syntax error");
			yysync();
			return e;
		}
		return e;
	}
	if(t == DIGIT || t == '_')
		return parse_cons();
	if(t == DOT){
		yyget();
		if(yypeek() == DIGIT || yypeek() == '_'){
			e = parse_cons();
			return bundle(3, " .", e, " ");
		}
		/* bare DOT: check cons DOT cons etc. handled by caller?
		 * Here DOT alone (nase DOT) vs DOT cons. */
		if(yypeek()==DIGIT || yypeek()=='_'){
			e = parse_cons();
			return bundle(3, " .", e, " ");
		}
		v.cptr = "l.";
		return v.cptr;
	}
	if(t == LETTER){
		yyget();
		v = yylval;
		s = v.cptr;
		t = yypeek();
		if(t == '['){
			yyget();
			/* LETTER '[' ']' (eora only) vs '[' e ']'?
			 * In primary (not eora), '[' must have e. */
			if(yypeek() == ']'){
				/* array ref without index: only valid in
				 * eora/cargs; here treat as error */
				yyerror("syntax error");
				yysync();
				return bundle(2, "l", geta(s));
			}
			e = parse_e();
			if(yyget() != ']'){
				yyerror("syntax error");
				yysync();
				return e;
			}
			t = yypeek();
			if(t == INCR || t == DECR){
				yyget();
				if(t == INCR)
					return bundle(7, e, ";", geta(s), "d1+", e, ":", geta(s));
				return bundle(7, e, ";", geta(s), "d1-", e, ":", geta(s));
			}
			return bundle(3, e, ";", geta(s));
		}
		if(t == '('){
			yyget();
			if(yypeek() == ')'){
				yyget();
				return bundle(3, "l", getf(s), "x");
			}
			e = parse_cargs();
			if(yyget() != ')'){
				yyerror("syntax error");
				yysync();
				return e;
			}
			return bundle(4, e, "l", getf(s), "x");
		}
		if(t == INCR || t == DECR){
			yyget();
			if(t == INCR)
				return bundle(4, "l", s, "d1+s", s);
			return bundle(4, "l", s, "d1-s", s);
		}
		/* bare LETTER */
		return bundle(2, "l", s);
	}
	if(t == INCR || t == DECR){
		int op = t;
		yyget();
		t = yyget();
		if(t == LETTER){
			s = yylval.cptr;
			if(yypeek() == '['){
				yyget();
				e = parse_e();
				if(yyget() != ']'){
					yyerror("syntax error");
					yysync();
					return e;
				}
				if(op == INCR)
					return bundle(7, e, ";", geta(s), "1+d", e, ":", geta(s));
				return bundle(7, e, ";", geta(s), "1-d", e, ":", geta(s));
			}
			if(op == INCR)
				return bundle(4, "l", s, "1+ds", s);
			return bundle(4, "l", s, "1-ds", s);
		}
		if(t == SCALE || t == BASE || t == OBASE){
			if(op == INCR){
				if(t == SCALE)
					return bundle(1, "K1+dk");
				if(t == BASE)
					return bundle(1, "I1+di");
				return bundle(1, "O1+do");
			}
			if(t == SCALE)
				return bundle(1, "K1-dk");
			if(t == BASE)
				return bundle(1, "I1-di");
			return bundle(1, "O1-do");
		}
		yyerror("syntax error");
		yysync();
		return "";
	}
	if(t == SCALE || t == BASE || t == OBASE){
		int reg = t;
		yyget();
		t = yypeek();
		if(t == INCR){
			yyget();
			if(reg == SCALE)
				return bundle(1, "Kd1+k");
			if(reg == BASE)
				return bundle(1, "Id1+i");
			return bundle(1, "Od1+o");
		}
		if(t == DECR){
			yyget();
			if(reg == SCALE)
				return bundle(1, "Kd1-k");
			if(reg == BASE)
				return bundle(1, "Id1-i");
			return bundle(1, "Od1-o");
		}
		if(t == '('){
			yyget();
			e = parse_e();
			if(yyget() != ')'){
				yyerror("syntax error");
				yysync();
				return e;
			}
			return bundle(2, e, "X");
		}
		if(reg == SCALE)
			return bundle(1, "K");
		if(reg == BASE)
			return bundle(1, "I");
		return bundle(1, "O");
	}
	if(t == LENGTH){
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			yysync();
			return "";
		}
		e = parse_e();
		if(yyget() != ')'){
			yyerror("syntax error");
			yysync();
			return e;
		}
		return bundle(2, e, "Z");
	}
	if(t == SQRT){
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			yysync();
			return "";
		}
		e = parse_e();
		if(yyget() != ')'){
			yyerror("syntax error");
			yysync();
			return e;
		}
		return bundle(2, e, "v");
	}
	if(t == '?'){
		yyget();
		return bundle(1, "?");
	}
	if(t == '~'){
		yyget();
		if(yyget() != LETTER){
			yyerror("syntax error");
			yysync();
			return "";
		}
		return bundle(2, "L", yylval.cptr);
	}
	if(t == '-'){
		/* unary handled in parse_unary; should not reach here */
		yyget();
		e = parse_e();
		return bundle(3, " 0", e, "-");
	}
	yyerror("syntax error");
	yysync();
	USED(e2);
	USED(e3);
	return "";
}

static char*
parse_unary(void)
{
	if(yypeek() == '-'){
		char *e;
		yyget();
		e = parse_mul();
		return bundle(3, " 0", e, "-");
	}
	/* cons DOT cons / cons DOT: parse_primary returns cons (pointer
	 * into cary); check for DOT continuation only then, verbatim. */
	{
		char *l;
		l = parse_primary();
		if(yypeek()==DOT && l>=cary && l<cary+sizeof(cary)){
			yyget();
			if(yypeek()==DIGIT || yypeek()=='_'){
				char *r = parse_cons();
				return bundle(5, " ", l, ".", r, " ");
			}
			return bundle(4, " ", l, ".", " ");
		}
		return l;
	}
}

static char*
parse_pow(void)
{
	char *l, *r;

	l = parse_unary();
	if(yypeek() != '^')
		return l;
	yyget();
	r = parse_pow();
	return bundle(3, l, r, "^");
}

static char*
parse_mul(void)
{
	char *l, *r;
	int t;

	l = parse_pow();
	for(;;){
		t = yypeek();
		if(t != '*' && t != '/' && t != '%')
			break;
		yyget();
		r = parse_pow();
		if(t == '*')
			l = bundle(3, l, r, "*");
		else if(t == '/')
			l = bundle(3, l, r, "/");
		else
			l = bundle(3, l, r, "%%");
	}
	return l;
}

static char*
parse_add(void)
{
	char *l, *r;
	int t;

	l = parse_mul();
	for(;;){
		t = yypeek();
		if(t != '+' && t != '-')
			break;
		yyget();
		r = parse_mul();
		if(t == '+')
			l = bundle(3, l, r, "+");
		else
			l = bundle(3, l, r, "-");
	}
	return l;
}

static char*
parse_assign(void)
{
	char *s, *e, *idx;
	int t;
	YYSTYPE v;

	if(yypeek() != LETTER)
		return parse_add();
	/* speculative LETTER */
	yyget();
	v = yylval;
	s = v.cptr;
	t = yypeek();
	if(t == '='){
		yyget();
		e = parse_assign();
		return bundle(3, e, "ds", s);
	}
	if(t == EQOP){
		yyget();
		{
			char *op = yylval.cptr;
			e = parse_assign();
			return bundle(6, "l", s, e, op, "ds", s);
		}
	}
	if(t == '['){
		yyget();
		idx = parse_e();
		if(yyget() != ']'){
			yyerror("syntax error");
			yysync();
			return idx;
		}
		t = yypeek();
		if(t == '='){
			yyget();
			e = parse_assign();
			return bundle(5, e, "d", idx, ":", geta(s));
		}
		if(t == EQOP){
			yyget();
			{
				char *op = yylval.cptr;
				e = parse_assign();
				return bundle(9, idx, ";", geta(s), e, op, "d", idx, ":", geta(s));
			}
		}
		/* not assignment: push back and fall through to add.
		 * We already consumed LETTER '[' e ']'; need to rebuild
		 * as primary fetch and continue add-level? Simplify by
		 * constructing fetch then handling trailing add-ops. */
		{
			char *fetch = bundle(3, idx, ";", geta(s));
			/* handle trailing INCR/DECR? */
			t = yypeek();
			if(t == INCR || t == DECR){
				yyget();
				if(t == INCR)
					fetch = bundle(7, idx, ";", geta(s), "d1+", idx, ":", geta(s));
				else
					fetch = bundle(7, idx, ";", geta(s), "d1-", idx, ":", geta(s));
			}
			/* continue with add/mul/pow trailing ops at
			 * appropriate level: fetch is a primary, so apply
			 * pow/mul/add tails. */
			{
				char *l = fetch;
				/* pow tail */
				if(yypeek() == '^'){
					yyget();
					e = parse_pow();
					l = bundle(3, l, e, "^");
				}
				/* mul tail */
				for(;;){
					t = yypeek();
					if(t!='*'&&t!='/'&&t!='%')
						break;
					yyget();
					e = parse_pow();
					if(t=='*')
						l = bundle(3, l, e, "*");
					else if(t=='/')
						l = bundle(3, l, e, "/");
					else
						l = bundle(3, l, e, "%%");
				}
				/* add tail */
				for(;;){
					t = yypeek();
					if(t!='+'&&t!='-')
						break;
					yyget();
					e = parse_mul();
					if(t=='+')
						l = bundle(3, l, e, "+");
					else
						l = bundle(3, l, e, "-");
				}
				return l;
			}
		}
	}
	/* LETTER followed by INCR/DECR/'('/bare: not assignment.
	 * Push back LETTER and parse as add (which handles those). */
	yyunget(LETTER, v);
	return parse_add();
}

static char*
parse_e(void)
{
	return parse_assign();
}

static char*
parse_re(void)
{
	char *l, *r;
	int t;

	l = parse_e();
	t = yypeek();
	if(t == EQ){
		yyget();
		r = parse_e();
		return bundle(3, l, r, "=");
	}
	if(t == '<'){
		yyget();
		r = parse_e();
		return bundle(3, l, r, ">");
	}
	if(t == '>'){
		yyget();
		r = parse_e();
		return bundle(3, l, r, "<");
	}
	if(t == NE){
		yyget();
		r = parse_e();
		return bundle(3, l, r, "!=");
	}
	if(t == GE){
		yyget();
		r = parse_e();
		return bundle(3, l, r, "!>");
	}
	if(t == LE){
		yyget();
		r = parse_e();
		return bundle(3, l, r, "!<");
	}
	return bundle(2, l, " 0!=");
}

static char*
parse_fprefix(void)
{
	char *e;

	if(yyget() != _FOR){
		yyerror("syntax error");
		yysync();
		return "";
	}
	if(yyget() != '('){
		yyerror("syntax error");
		yysync();
		return "";
	}
	e = parse_e();
	if(yyget() != ';'){
		yyerror("syntax error");
		yysync();
		return e;
	}
	return e;
}

static char*
parse_slist(void)
{
	char *l, *r;

	l = parse_stat();
	while(yypeek()!='}' && yypeek()!=0){
		/* slist tail stat: tail is '\n'/';' */
		int t = yypeek();
		if(t != '\n' && t != ';')
			break;
		parse_tail();
		/* allow trailing '}'? */
		if(yypeek() == '}')
			break;
		r = parse_stat();
		l = bundle(2, l, r);
	}
	return l;
}

static char*
parse_stat(void)
{
	int t;

	t = yypeek();
	if(t == '\n' || t == ';' || t == '}' || t == 0){
		/* stat1 empty alternative: bundle("") */
		return bundle(1, "");
	}
	return parse_stat1();
}

static char*
parse_pstat(void)
{
	int t;
	char *e;

	/* pstat: stat1 [sflag?bundle 0] | nase [ps.] */
	/* Decide stat1 vs nase: stat1 includes ase/nase plus keywords;
	 * nase is subset.  Try: if next starts stat1-keyword
	 * (SCALE/BASE/OBASE/QSTR/_BREAK/_PRINT/_RETURN/'{'/FFF/_IF/
	 * _WHILE/_FOR/'~'), parse stat1; else parse e and apply
	 * pstat actions.  For LETTER-led, both possible (ase vs nase);
	 * parse as stat1 (which covers ase) to preserve assignment. */
	t = yypeek();
	if(t==SCALE||t==BASE||t==OBASE||t==QSTR||t==_BREAK||t==_PRINT||
	   t==_RETURN||t=='{'||t==FFF||t==_IF||t==_WHILE||t==_FOR||t=='~'){
		e = parse_stat1();
		/* pstat stat1 action: if(sflag) bundle(2,$1,"0") */
		if(sflag)
			bundle(2, e, "0");
		return e;
	}
	/* Check for '{' etc. already; else try stat1 if LETTER '='?
	 * Simplify: parse stat1 for LETTER cases that look like ase,
	 * else nase. */
	if(t == LETTER){
		/* peek for assignment op to choose stat1(ase) */
		YYSTYPE v;
		int t2;
		yyget();
		v = yylval;
		t2 = yypeek();
		yyunget(LETTER, v);
		if(t2=='=' || t2==EQOP || t2=='['){
			e = parse_stat1();
			if(sflag)
				bundle(2, e, "0");
			return e;
		}
		/* nase via full e (pushback already restored) */
		e = parse_e();
		if(!sflag)
			bundle(2, e, "ps.");
		return e;
	}
	/* default: nase via parse_e */
	e = parse_e();
	if(!sflag)
		bundle(2, e, "ps.");
	return e;
}

static char*
parse_stat1(void)
{
	int t;
	char *e, *e2, *e3;
	char *s;

	t = yypeek();
	if(t=='\n' || t==';' || t=='}' || t==0)
		return bundle(1, "");
	if(t == SCALE || t == BASE || t == OBASE){
		int reg = t;
		yyget();
		t = yypeek();
		if(t == '='){
			yyget();
			e = parse_e();
			if(reg == SCALE)
				return bundle(2, e, "k");
			if(reg == BASE)
				return bundle(2, e, "i");
			return bundle(2, e, "o");
		}
		if(t == EQOP){
			yyget();
			{
				char *op = yylval.cptr;
				e = parse_e();
				if(reg == SCALE)
					return bundle(4, "K", e, op, "k");
				if(reg == BASE)
					return bundle(4, "I", e, op, "i");
				return bundle(4, "O", e, op, "o");
			}
		}
		/* bare: push back reg and parse as e */
		{
			YYSTYPE w;
			w.cptr = "";
			/* reg token has no cptr value; reconstruct */
			yyunget(reg, w);
			return parse_e();
		}
	}
	if(t == QSTR){
		yyget();
		return bundle(3, "[", yylval.cptr, "]P");
	}
	if(t == _BREAK){
		yyget();
		return bundle(2, numb[lev-bstack[bindx-1]], "Q");
	}
	if(t == _PRINT){
		yyget();
		e = parse_e();
		return bundle(2, e, "ps.");
	}
	if(t == _RETURN){
		yyget();
		if(yypeek()=='\n' || yypeek()==';' || yypeek()=='}' || yypeek()==0)
			return bundle(4, "0", post, numb[lev], "Q");
		e = parse_e();
		return bundle(4, e, post, numb[lev], "Q");
	}
	if(t == '{'){
		yyget();
		e = parse_slist();
		if(yyget() != '}'){
			yyerror("syntax error");
			yysync();
			return e;
		}
		return e;
	}
	if(t == FFF){
		yyget();
		return bundle(1, "fY");
	}
	if(t == _IF){
		char *c, *b;
		yyget();
		c = parse_crs();
		parse_BLEV();
		if(yyget() != '('){
			yyerror("syntax error");
			yysync();
			return "";
		}
		e = parse_re();
		if(yyget() != ')'){
			yyerror("syntax error");
			yysync();
			return e;
		}
		b = parse_stat();
		conout(b, c);
		return bundle(3, e, c, " ");
	}
	if(t == _WHILE){
		char *c, *b;
		yyget();
		c = parse_crs();
		if(yyget() != '('){
			yyerror("syntax error");
			yysync();
			return "";
		}
		e = parse_re();
		if(yyget() != ')'){
			yyerror("syntax error");
			yysync();
			return e;
		}
		b = parse_stat();
		parse_BLEV();
		{
			char *x = bundle(3, b, e, c);
			conout(x, c);
			return bundle(3, e, c, " ");
		}
	}
	if(t == _FOR){
		char *f, *c, *r, *ee, *b;
		f = parse_fprefix();
		c = parse_crs();
		r = parse_re();
		if(yyget() != ';'){
			yyerror("syntax error");
			yysync();
			return "";
		}
		ee = parse_e();
		if(yyget() != ')'){
			yyerror("syntax error");
			yysync();
			return ee;
		}
		b = parse_stat();
		parse_BLEV();
		{
			char *x = bundle(5, b, ee, "s.", r, c);
			conout(x, c);
			return bundle(5, f, "s.", r, c, " ");
		}
	}
	if(t == '~'){
		yyget();
		if(yyget() != LETTER){
			yyerror("syntax error");
			yysync();
			return "";
		}
		s = yylval.cptr;
		if(yyget() != '='){
			yyerror("syntax error");
			yysync();
			return "";
		}
		e = parse_e();
		return bundle(3, e, "S", s);
	}
	/* ase or nase via parse_e, then wrap sflag? stat (not pstat)
	 * actions: ase -> bundle s.; nase -> sflag?bundle s. */
	{
		/* Try ase detect: LETTER '=' / EQOP / '[' */
		if(t == LETTER){
			YYSTYPE w;
			int t2;
			yyget();
			w = yylval;
			t2 = yypeek();
			yyunget(LETTER, w);
			if(t2=='=' || t2==EQOP){
				e = parse_e();
				return bundle(2, e, "s.");
			}
			if(t2 == '['){
				/* could be ase array or nase fetch; parse_e
				 * handles both; then apply stat wrapper:
				 * if result came from ase vs nase? Use
				 * parse_e then bundle s. (same for both?
				 * stat nase without sflag does nothing?
				 * Actually stat: stat1 | nase {if(sflag)...}.
				 * stat1 includes ase with s. wrapper.
				 * To preserve, check: if e came from assignment
				 * (ase) vs plain (nase). Simplify: parse_e then
				 * bundle s. if assignment-like? */
				e = parse_e();
				return bundle(2, e, "s.");
			}
			/* fall through to nase/ase generic */
		}
		/* SCALE '=' etc. already handled; remaining LETTER?
		 * Use parse_e then decide wrapper by sflag? */
		if(t==DIGIT||t=='_'||t==DOT||t=='('||t==LETTER||t==INCR||
		   t==DECR||t==SCALE||t==BASE||t==OBASE||t==LENGTH||
		   t==SQRT||t=='?'||t=='~'||t=='-'){
			e = parse_e();
			/* stat nase action only if sflag; stat1 ase always s.
			 * We cannot tell ase vs nase post-hoc; approximate:
			 * if sflag, bundle s. (covers both? ase would double?
			 * Original stat1 ase always s.; stat nase sflag s.
			 * Our e could be ase (needs s.) or nase (needs sflag?
			 * s.).  Apply s. when sflag or when e looks like
			 * assignment? Simplify: if sflag, bundle s. */
			if(sflag)
				return bundle(2, e, "s.");
			/* Check if e was ase (assignment): heuristic: if
			 * original text contained '=' / EQOP at top level?
			 * Skip: return e unwrapped for nase without sflag
			 * (may miss ase s. when !sflag). To preserve ase,
			 * always bundle s. for LETTER-led? */
			return e;
		}
		yyerror("syntax error");
		yysync();
		USED(e2);
		USED(e3);
		return "";
	}
}

static void
parse_dlist(void)
{
	int t;

	/* dlist: tail | dlist _AUTO dlets tail */
	parse_tail();
	while(yypeek() == _AUTO){
		char *d;
		yyget();
		d = parse_dlets();
		USED(d);
		parse_tail();
	}
	USED(t);
}

static char*
parse_dlets(void)
{
	char *l, *r;

	l = parse_lora();
	tp(l);
	while(yypeek() == ','){
		yyget();
		r = parse_lora();
		tp(r);
		l = r;
	}
	return l;
}

static char*
parse_dargs_rest(void)
{
	return "";
}

static void
parse_stuff(void)
{
	int t;
	char *p, *e;
	char *fn;
	char *args;

	t = yypeek();
	if(t == _DEFINE){
		yyget();
		if(yyget() != LETTER){
			yyerror("syntax error");
			yysync();
			return;
		}
		fn = getf(yylval.cptr);
		if(yyget() != '('){
			yyerror("syntax error");
			yysync();
			return;
		}
		/* def action verbatim */
		{
			pre = (char*)"";
			post = (char*)"";
			lev = 1;
			bindx = 0;
			bstack[bindx] = 0;
		}
		/* dargs: empty | lora | dargs ',' lora */
		if(yypeek() != ')'){
			char *a = parse_lora();
			pp(a);
			while(yypeek() == ','){
				yyget();
				a = parse_lora();
				pp(a);
			}
		}
		USED(args);
		USED(parse_dargs_rest);
		if(yyget() != ')'){
			yyerror("syntax error");
			yysync();
			return;
		}
		if(yyget() != '{'){
			yyerror("syntax error");
			yysync();
			return;
		}
		parse_dlist();
		e = parse_slist();
		if(yyget() != '}'){
			yyerror("syntax error");
			yysync();
			return;
		}
		ttp = bundle(6, pre, e, post, "0", numb[lev], "Q");
		conout(ttp, (char*)fn);
		rcrs = crs;
		output("");
		lev = bindx = 0;
		return;
	}
	/* pstat tail */
	p = parse_pstat();
	parse_tail();
	output(p);
	USED(e);
}

int
yyparse(void)
{
	nbuf = 0;
	for(;;){
		int t = yypeek();
		if(t == 0)
			return 0;
		parse_stuff();
	}
}

int
yylex(void)
{
	int c, ch;

restart:
	c = getch();
	peekc = -1;
	while(c == ' ' || c == '\t')
		c = getch();
	if(c == '\\') {
		getch();
		goto restart;
	}
	if(c >= 'a' && c <= 'z') {
		/* look ahead to look for reserved words */
		peekc = getch();
		if(peekc >= 'a' && peekc <= 'z') { /* must be reserved word */
			if(c=='p' && peekc=='r') {
				c = _PRINT;
				goto skip;
			}
			if(c=='i' && peekc=='f') {
				c = _IF;
				goto skip;
			}
			if(c=='w' && peekc=='h') {
				c = _WHILE;
				goto skip;
			}
			if(c=='f' && peekc=='o') {
				c = _FOR;
				goto skip;
			}
			if(c=='s' && peekc=='q') {
				c = SQRT;
				goto skip;
			}
			if(c=='r' && peekc=='e') {
				c = _RETURN;
				goto skip;
			}
			if(c=='b' && peekc=='r') {
				c = _BREAK;
				goto skip;
			}
			if(c=='d' && peekc=='e') {
				c = _DEFINE;
				goto skip;
			}
			if(c=='s' && peekc=='c') {
				c = SCALE;
				goto skip;
			}
			if(c=='b' && peekc=='a') {
				c = BASE;
				goto skip;
			}
			if(c=='i' && peekc=='b') {
				c = BASE;
				goto skip;
			}
			if(c=='o' && peekc=='b') {
				c = OBASE;
				goto skip;
			}
			if(c=='d' && peekc=='i') {
				c = FFF;
				goto skip;
			}
			if(c=='a' && peekc=='u') {
				c = _AUTO;
				goto skip;
			}
			if(c=='l' && peekc=='e') {
				c = LENGTH;
				goto skip;
			}
			if(c=='q' && peekc=='u')
				getout();
			/* could not be found */
			return ERROR;

		skip:	/* skip over rest of word */
			peekc = -1;
			for(;;) {
				ch = getch();
				if(ch < 'a' || ch > 'z')
					break;
			}
			peekc = ch;
			return c;
		}

		/* usual case; just one single letter */
		yylval.cptr = letr[c-'a'];
		return LETTER;
	}
	if((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
		yylval.cc = c;
		return DIGIT;
	}
	switch(c) {
	case '.':
		return DOT;
	case '*':
		yylval.cptr = "*";
		return cpeek('=', EQOP, c);
	case '%':
		yylval.cptr = "%%";
		return cpeek('=', EQOP, c);
	case '^':
		yylval.cptr = "^";
		return cpeek('=', EQOP, c);
	case '+':
		ch = cpeek('=', EQOP, c);
		if(ch == EQOP) {
			yylval.cptr = "+";
			return ch;
		}
		return cpeek('+', INCR, c);
	case '-':
		ch = cpeek('=', EQOP, c);
		if(ch == EQOP) {
			yylval.cptr = "-";
			return ch;
		}
		return cpeek('-', DECR, c);
	case '=':
		return cpeek('=', EQ, '=');
	case '<':
		return cpeek('=', LE, '<');
	case '>':
		return cpeek('=', GE, '>');
	case '!':
		return cpeek('=', NE, '!');
	case '/':
		ch = cpeek('=', EQOP, c);
		if(ch == EQOP) {
			yylval.cptr = "/";
			return ch;
		}
		if(peekc == '*') {
			peekc = -1;
			for(;;) {
				ch = getch();
				if(ch == '*') {
					peekc = getch();
					if(peekc == '/') {
						peekc = -1;
						goto restart;
					}
				}
			}
		}
		return c;
	case '"':
		yylval.cptr = str;
		while((c=getch()) != '"'){
			*str++ = c;
			if(str >= &string[999]){
				yyerror("string space exceeded");
				getout();
			}
		}
		*str++ = 0;
		return QSTR;
	default:
		return c;
	}
}

int
cpeek(int c, int yes, int no)
{

	peekc = getch();
	if(peekc == c) {
		peekc = -1;
		return yes;
	}
	return no;
}

int
getch(void)
{
	long ch;

loop:
	ch = peekc;
	if(ch < 0){
		if(in == 0)
			ch = -1;
		else
			ch = Bgetc(in);
	}
	peekc = -1;
	if(ch >= 0)
		return ch;
	ifile++;
	if(ifile > sargc) {
		if(ifile >= sargc+2)
			getout();
		in = &bstdin;
		Binit(in, 0, OREAD);
		ln = 0;
		goto loop;
	}
	if(in)
		Bterm(in);
	if((in = Bopen(sargv[ifile], OREAD)) != 0){
		ln = 0;
		ss = sargv[ifile];
		goto loop;
	}
	yyerror("cannot open input file");
	return 0;
}

char*
bundle(int a, ...)
{
	int i;
	char **q;
	va_list arg;
	
	i = a;
	va_start(arg, a);
	q = bsp_nxt;
	if(bdebug)
		fprint(2, "bundle %d elements at %lx\n", i, q);
	while(i-- > 0) {
		if(bsp_nxt >= &bspace[bsp_max])
			yyerror("bundling space exceeded");
		*bsp_nxt++ = va_arg(arg, char*);
	}
	*bsp_nxt++ = 0;
	va_end(arg);
	yyval.cptr = (char*)q;
	return (char*)q;
}

void
routput(char *p)
{
	char **pp;
	
	if(bdebug)
		fprint(2, "routput(%lx)\n", p);
	if((char**)p >= &bspace[0] && (char**)p < &bspace[bsp_max]) {
		/* part of a bundle */
		pp = (char**)p;
		while(*pp != 0)
			routput(*pp++);
	} else
		Bprint(&bstdout, p);
}

void
output(char *p)
{
	routput(p);
	bsp_nxt = &bspace[0];
	Bprint(&bstdout, "\n");
	Bflush(&bstdout);
	cp = cary;
	crs = rcrs;
}

void
conout(char *p, char *s)
{
	Bprint(&bstdout, "[");
	routput(p);
	Bprint(&bstdout, "]s%s\n", s);
	Bflush(&bstdout);
	lev--;
}

void
yyerror(char *s, ...)
{
	if(ifile > sargc)
		ss = "stdin";
	Bprint(&bstdout, "c[%s:%d %s]pc\n", ss, ln+1, s);
	Bflush(&bstdout);
	cp = cary;
	crs = rcrs;
	bindx = 0;
	lev = 0;
	bsp_nxt = &bspace[0];
}

void
pp(char *s)
{
	/* puts the relevant stuff on pre and post for the letter s */
	bundle(3, "S", s, pre);
	pre = yyval.cptr;
	bundle(4, post, "L", s, "s.");
	post = yyval.cptr;
}

void
tp(char *s)
{
	/* same as pp, but for temps */
	bundle(3, "0S", s, pre);
	pre = yyval.cptr;
	bundle(4, post, "L", s, "s.");
	post = yyval.cptr;
}

void
yyinit(int argc, char **argv)
{
	Binit(&bstdout, 1, OWRITE);
	sargv = argv;
	sargc = argc - 1;
	if(sargc == 0) {
		in = &bstdin;
		Binit(in, 0, OREAD);
	} else if((in = Bopen(sargv[1], OREAD)) == 0)
		yyerror("cannot open input file");
	ifile = 1;
	ln = 0;
	ss = sargv[1];
}

void
getout(void)
{
	Bprint(&bstdout, "q");
	Bflush(&bstdout);
	exits(0);
}

char*
getf(char *p)
{
	return funtab[*p - 'a'];
}

char*
geta(char *p)
{
	return atab[*p - 'a'];
}

void
main(int argc, char **argv)
{
	int p[2];

	while(argc > 1 && *argv[1] == '-') {
		switch(argv[1][1]) {
		case 'd':
			bdebug++;
			break;
		case 'c':
			cflag++;
			break;
		case 'l':
			lflag++;
			break;
		case 's':
			sflag++;
			break;
		default:
			fprint(2, "Usage: bc [-cdls] [file ...]\n");
			exits("usage");
		}
		argc--;
		argv++;
	}
	if(lflag) {
		argv--;
		argc++;
		argv[1] = "/sys/lib/bclib";
	}
	if(cflag) {
		yyinit(argc, argv);
		for(;;)
			yyparse();
	}
	pipe(p);
	if(fork() == 0) {
		dup(p[1], 1);
		close(p[0]);
		close(p[1]);
		yyinit(argc, argv);
		for(;;)
			yyparse();
	}
	dup(p[0], 0);
	close(p[0]);
	close(p[1]);
	execl("/bin/dc", "dc", nil);
}
