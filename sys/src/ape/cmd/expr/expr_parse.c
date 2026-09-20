/* Hand-written recursive-descent replacement for expr.y (LALR).
 *
 * Original yacc grammar (precedence low -> high):
 *	%left OR
 *	%left AND
 *	%left EQ LT GT GEQ LEQ NEQ
 *	%left ADD SUBT
 *	%left MULT DIV REM
 *	%left MCH		// ":" infix
 *	%left MATCH		// prefix, 2 args
 *	%left SUBSTR		// prefix, 3 args
 *	%left LENGTH INDEX	// prefix, 1 and 2 args
 *	expression: expr NOARG	{ prt(1, $1); exit(...); }
 *	expr: '(' expr ')'		{ $$ = $2; }
 *	| expr OR expr			{ $$ = conj(OR, $1, $3); }
 *	| expr AND expr			{ $$ = conj(AND, $1, $3); }
 *	| expr EQ/GT/GEQ/LT/LEQ/NEQ expr	{ $$ = rel(op, $1, $3); }
 *	| expr ADD/SUBT/MULT/DIV/REM expr	{ $$ = arith(op, $1, $3); }
 *	| expr MCH expr			{ $$ = match($1, $3); }
 *	| MATCH expr expr		{ $$ = match($2, $3); }
 *	| SUBSTR expr expr expr		{ $$ = substr($2, $3, $4); }
 *	| LENGTH expr			{ $$ = length($2); }
 *	| INDEX expr expr		{ $$ = index($2, $3); }
 *	| A_STRING
 *
 * Precedence-map (hand parser, low -> high):
 *	level 1 (lowest, left): OR ("|")
 *	level 2 (left): AND ("&")
 *	level 3 (left): EQ ("=","==") LT ("<") GT (">") GEQ (">=") LEQ ("<=") NEQ ("!=")
 *	level 4 (left): ADD ("+") SUBT ("-")
 *	level 5 (left): MULT ("*") DIV ("/") REM ("%")
 *	level 6 (left): MCH (":" infix match)
 *	level 7: MATCH prefix (2 args, each a full expr)
 *	level 8: SUBSTR prefix (3 args)
 *	level 9 (highest): LENGTH / INDEX prefix (1 / 2 args)
 *	primary: '(' expr ')' | A_STRING
 * Call chain: parse_expr -> parse_or -> parse_and -> parse_rel
 *	-> parse_add -> parse_mul -> parse_mch -> parse_prefix -> parse_primary.
 *	Prefix args are full exprs (parse_or), matching yacc's "expr" args.
 *
 * Error-recovery-map: original had no `error' production; any syntax
 *	error called yyerror() which prints "expr: ..." and exits(2).
 *	Hand parser calls yyerror("syntax error") at the exact failure point
 *	(unexpected token, missing ')', missing arg, trailing garbage),
 *	preserving exits(2) semantics and NOARG terminator protocol.
 */

#define YYSTYPE charp
typedef char *charp;

enum {
	OR = 257,
	AND = 258,
	ADD = 259,
	SUBT = 260,
	MULT = 261,
	DIV = 262,
	REM = 263,
	EQ = 264,
	GT = 265,
	GEQ = 266,
	LT = 267,
	LEQ = 268,
	NEQ = 269,
	A_STRING = 270,
	SUBSTR = 271,
	LENGTH = 272,
	INDEX = 273,
	NOARG = 274,
	MATCH = 275,
	MCH = 276,
};

YYSTYPE yylval;

char *rel(int, char*, char*);
char *arith(int, char*, char*);
char *conj(int, char*, char*);
char *substr(char*, char*, char*);
char *length(char*);
char *index(char*, char*);
char *match(char*, char*);
void prt(int, char*);

static charp parse_expr(void);
static charp parse_or(void);
static charp parse_and(void);
static charp parse_rel(void);
static charp parse_add(void);
static charp parse_mul(void);
static charp parse_mch(void);
static charp parse_prefix(void);
static charp parse_primary(void);
int yylex(void);
void yyerror(char*);
static int yypeek(void);
static int yyget(void);

/* 1-token lookahead buffer over yylex() */
static int yyhave;
static int yysave;
static YYSTYPE yysaveval;

static int
yypeek(void)
{
	if(!yyhave){
		yysave = yylex();
		yysaveval = yylval;
		yyhave = 1;
	}
	return yysave;
}

static int
yyget(void)
{
	if(yyhave){
		yyhave = 0;
		yylval = yysaveval;
		return yysave;
	}
	return yylex();
}

static charp
parse_primary(void)
{
	int t;
	charp e;

	t = yyget();
	switch(t){
	case '(':
		e = parse_expr();
		if(yyget() != ')')
			yyerror("syntax error");
		return e;
	case A_STRING:
		return yylval;
	default:
		yyerror("syntax error");
		return 0;	/* not reached */
	}
}

static charp
parse_prefix(void)
{
	int t;
	charp a, b, c;

	t = yypeek();
	if(t == MATCH){
		yyget();
		a = parse_or();
		b = parse_or();
		return match(a, b);
	}
	if(t == SUBSTR){
		yyget();
		a = parse_or();
		b = parse_or();
		c = parse_or();
		return substr(a, b, c);
	}
	if(t == LENGTH){
		yyget();
		a = parse_or();
		return length(a);
	}
	if(t == INDEX){
		yyget();
		a = parse_or();
		b = parse_or();
		return index(a, b);
	}
	return parse_primary();
}

static charp
parse_mch(void)
{
	int t;
	charp e, r;

	e = parse_prefix();
	for(;;){
		t = yypeek();
		if(t != MCH)
			break;
		yyget();
		r = parse_prefix();
		e = match(e, r);
	}
	return e;
}

static charp
parse_mul(void)
{
	int t;
	charp e, r;

	e = parse_mch();
	for(;;){
		t = yypeek();
		if(t != MULT && t != DIV && t != REM)
			break;
		yyget();
		r = parse_mch();
		e = arith(t, e, r);
	}
	return e;
}

static charp
parse_add(void)
{
	int t;
	charp e, r;

	e = parse_mul();
	for(;;){
		t = yypeek();
		if(t != ADD && t != SUBT)
			break;
		yyget();
		r = parse_mul();
		e = arith(t, e, r);
	}
	return e;
}

static charp
parse_rel(void)
{
	int t;
	charp e, r;

	e = parse_add();
	for(;;){
		t = yypeek();
		if(t != EQ && t != LT && t != GT && t != GEQ && t != LEQ && t != NEQ)
			break;
		yyget();
		r = parse_add();
		e = rel(t, e, r);
	}
	return e;
}

static charp
parse_and(void)
{
	charp e, r;

	e = parse_rel();
	while(yypeek() == AND){
		yyget();
		r = parse_rel();
		e = conj(AND, e, r);
	}
	return e;
}

static charp
parse_or(void)
{
	charp e, r;

	e = parse_and();
	while(yypeek() == OR){
		yyget();
		r = parse_and();
		e = conj(OR, e, r);
	}
	return e;
}

static charp
parse_expr(void)
{
	return parse_or();
}

int
yyparse(void)
{
	charp e;

	yyhave = 0;
	e = parse_expr();
	if(yyget() != NOARG)
		yyerror("syntax error");
	prt(1, e);
	exit((!strcmp(e,"0")||!strcmp(e,"\0"))? 1: 0);
	return 0;	/* not reached */
}
/*	expression command */
#include <stdio.h>
/* get rid of yacc debug printf's */
#define printf
#define ESIZE	512
#define error(c)	errxx(c)
#define EQL(x,y) !strcmp(x,y)
long atol();
char *ltoa();
char	**Av;
int	Ac;
int	Argi;

char Mstring[1][128];
char *malloc();
extern int nbra;
int yyparse(void);

main(argc, argv) char **argv; {
	Ac = argc;
	Argi = 1;
	Av = argv;
	yyparse();
}

char *operator[] = { "|", "&", "+", "-", "*", "/", "%", ":",
	"=", "==", "<", "<=", ">", ">=", "!=",
	"match", "substr", "length", "index", "\0" };
int op[] = { OR, AND, ADD,  SUBT, MULT, DIV, REM, MCH,
	EQ, EQ, LT, LEQ, GT, GEQ, NEQ,
	MATCH, SUBSTR, LENGTH, INDEX };
yylex() {
	register char *p;
	register i;

	if(Argi >= Ac) return NOARG;

	p = Av[Argi++];

	if(*p == '(' || *p == ')')
		return (int)*p;
	for(i = 0; *operator[i]; ++i)
		if(EQL(operator[i], p))
			return op[i];

	yylval = p;
	return A_STRING;
}

char *rel(op, r1, r2) register char *r1, *r2; {
	register i;

	if(ematch(r1, "-\\{0,1\\}[0-9]*$") && ematch(r2, "-\\{0,1\\}[0-9]*$"))
		i = atol(r1) - atol(r2);
	else
		i = strcmp(r1, r2);
	switch(op) {
	case EQ: i = i==0; break;
	case GT: i = i>0; break;
	case GEQ: i = i>=0; break;
	case LT: i = i<0; break;
	case LEQ: i = i<=0; break;
	case NEQ: i = i!=0; break;
	}
	return i? "1": "0";
}

char *arith(op, r1, r2) char *r1, *r2; {
	long i1, i2;
	register char *rv;

	if(!(ematch(r1, "-\\{0,1\\}[0-9]*$") && ematch(r2, "-\\{0,1\\}[0-9]*$")))
		yyerror("non-numeric argument");
	i1 = atol(r1);
	i2 = atol(r2);

	switch(op) {
	case ADD: i1 = i1 + i2; break;
	case SUBT: i1 = i1 - i2; break;
	case MULT: i1 = i1 * i2; break;
	case DIV: i1 = i1 / i2; break;
	case REM: i1 = i1 % i2; break;
	}
	rv = malloc(16);
	strcpy(rv, ltoa(i1));
	return rv;
}
char *conj(op, r1, r2) char *r1, *r2; {
	register char *rv;

	switch(op) {

	case OR:
		if(EQL(r1, "0")
		|| EQL(r1, ""))
			if(EQL(r2, "0")
			|| EQL(r2, ""))
				rv = "0";
			else
				rv = r2;
		else
			rv = r1;
		break;
	case AND:
		if(EQL(r1, "0")
		|| EQL(r1, ""))
			rv = "0";
		else if(EQL(r2, "0")
		|| EQL(r2, ""))
			rv = "0";
		else
			rv = r1;
		break;
	}
	return rv;
}

char *substr(v, s, w) char *v, *s, *w; {
register si, wi;
register char *res;

	si = atol(s);
	wi = atol(w);
	while(--si) if(*v) ++v;

	res = v;

	while(wi--) if(*v) ++v;

	*v = '\0';
	return res;
}

char *length(s) register char *s; {
	register i = 0;
	register char *rv;

	while(*s++) ++i;

	rv = malloc(8);
	strcpy(rv, ltoa((long)i));
	return rv;
}

char *index(s, t) char *s, *t; {
	register i, j;
	register char *rv;

	for(i = 0; s[i] ; ++i)
		for(j = 0; t[j] ; ++j)
			if(s[i]==t[j]) {
				strcpy(rv=malloc(8), ltoa((long)++i));
				return rv;
			}
	return "0";
}

char *match(s, p)
{
	register char *rv;

	strcpy(rv=malloc(8), ltoa((long)ematch(s, p)));
	if(nbra) {
		rv = malloc(strlen(Mstring[0])+1);
		strcpy(rv, Mstring[0]);
	}
	return rv;
}

#define INIT	register char *sp = instring;
#define GETC()		(*sp++)
#define PEEKC()		(*sp)
#define UNGETC(c)	(--sp)
#define RETURN(c)	return
#define ERROR(c)	errxx(c)


ematch(s, p)
char *s;
register char *p;
{
	static char expbuf[ESIZE];
	char *compile();
	register num;
	extern char *braslist[], *braelist[], *loc2;

	compile(p, expbuf, &expbuf[ESIZE], 0);
	if(nbra > 1)
		yyerror("Too many '\\('s");
	if(advance(s, expbuf)) {
		if(nbra == 1) {
			p = braslist[0];
			num = braelist[0] - p;
			strncpy(Mstring[0], p, num);
			Mstring[0][num] = '\0';
		}
		return(loc2-s);
	}
	return(0);
}

errxx(c)
{
	yyerror("RE error");
}

#include  "regexp.h"
yyerror(s)

{
	write(2, "expr: ", 6);
	prt(2, s);
	exit(2);
}
prt(fd, s)
char *s;
{
	write(fd, s, strlen(s));
	write(fd, "\n", 1);
}
char *ltoa(l)
long l;
{
	static char str[20];
	register char *sp = &str[18];
	register i;
	register neg = 0;

	if(l < 0)
		++neg, l *= -1;
	str[19] = '\0';
	do {
		i = l % 10;
		*sp-- = '0' + i;
		l /= 10;
	} while(l);
	if(neg)
		*sp-- = '-';
	return ++sp;
}
