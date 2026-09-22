#include <u.h>
#include <libc.h>
#include <ctype.h>
#include "disk.h"
#include "edit.h"

/*
 * Hand-written recursive-descent replacement for calc.y (LALR).
 *
 * Original yacc grammar (precedence low → high):
 *	%left '+' '-'
 *	%left '*' '/'
 *	%left UNARYMINUS '%'
 *	top: expr			{ yyexp = $1; return 0; }
 *	expr: NUMBER
 *	| '.'				{ $$ = mkOP(DOT, nil, nil); }
 *	| '$'				{ $$ = mkOP(DOLLAR, nil, nil); }
 *	| '(' expr ')'			{ $$ = $2; }
 *	| expr '+' expr			{ $$ = mkOP(ADD, $1, $3); }
 *	| expr '-' expr			{ $$ = mkOP(SUB, $1, $3); }
 *	| expr '*' expr			{ $$ = mkOP(MUL, $1, $3); }
 *	| expr '/' expr			{ $$ = mkOP(DIV, $1, $3); }
 *	| expr '%'			{ $$ = mkOP(FRAC, $1, nil); }
 *	| '-' expr %prec UNARYMINUS	{ $$ = mkOP(NEG, $2, nil); }
 *
 * Precedence-map (hand parser):
 *	level 1 (lowest, left): '+' '-'
 *	level 2 (left): '*' '/'
 *	level 3 (highest): prefix '-' (right), postfix '%' (left)
 * Note: '-' operand excludes trailing '%' so "-a%" == "(-a)%",
 * matching yacc's reduce-on-equal-precedence (left) behaviour.
 * Call chain: parse_expr -> parse_term -> parse_postfix ->
 *              parse_unary -> parse_primary.
 *
 * Error-recovery-map: original had no `error' production; any
 * syntax error called yyerror() which longjmp()s out with *errp
 * set.  Hand parser calls yyerror("syntax error") at the exact
 * failure point (unexpected token, missing ')', trailing garbage)
 * preserving longjmp semantics and parseexpr() return protocol.
 */

typedef struct Exp Exp;
/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef NUM
#define NUM 0
#undef DOT
#define DOT 1
#undef DOLLAR
#define DOLLAR 2
#undef ADD
#define ADD 3
#undef SUB
#define SUB 4
#undef MUL
#define MUL 5
#undef DIV
#define DIV 6
#undef FRAC
#define FRAC 7
#undef NEG
#define NEG 8

struct Exp {
	int ty;
	long long n;
	Exp *e1;
	Exp *e2;
};

typedef Exp* YYSTYPE;
YYSTYPE yylval;
Exp *yyexp;

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef NUMBER
#define NUMBER 257

static Exp* mkNUM(vlong x);
static Exp* mkOP(int ty, Exp *e1, Exp *e2);
static int yylex(void);
static void yyerror(char *s);
static Exp* parse_expr(void);
static Exp* parse_term(void);
static Exp* parse_postfix(void);
static Exp* parse_unary(void);
static Exp* parse_primary(void);
static int yypeek(void);
static int yyget(void);

static Exp*
mkNUM(vlong x)
{
	Exp *n;

	n = emalloc(sizeof *n);

	n->ty = NUM;
	n->n = x;
	return n;
}

static Exp*
mkOP(int ty, Exp *e1, Exp *e2)
{
	Exp *n;

	n = emalloc(sizeof *n);
	n->ty = ty;
	n->e1 = e1;
	n->e2 = e2;

	return n;
}

static char *inp;
static jmp_buf jmp;
static vlong dot, size, dollar;
static char** errp;

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

static int
yylex(void)
{
	int c;
	uvlong n;

	while(isspace(*inp))
		inp++;

	if(*inp == 0)
		return 0;

	if(isdigit(*inp)) {
		n = strtoull(inp, &inp, 0);	/* default unit is sectors */
		c = *inp++;
		if(isascii(c) && isupper(c))
			c = tolower(c);
		switch(c) {
		case 't':
			n *= 1024;
			/* fall through */
		case 'g':
			n *= 1024;
			/* fall through */
		case 'm':
			n *= 1024;
			/* fall through */
		case 'k':
			n *= 2;
			break;
		default:
			--inp;
			break;
		}
		yylval = mkNUM(n);
		return NUMBER;
	}
	return *inp++;
}

static void
yyerror(char *s)
{
	*errp = s;
	longjmp(jmp, 1);
}

static Exp*
parse_primary(void)
{
	int t;
	Exp *e;

	t = yyget();
	switch(t){
	case NUMBER:
		return yylval;
	case '.':
		return mkOP(DOT, nil, nil);
	case '$':
		return mkOP(DOLLAR, nil, nil);
	case '(':
		e = parse_expr();
		if(yyget() != ')')
			yyerror("syntax error");
		return e;
	default:
		yyerror("syntax error");
		return nil;	/* not reached */
	}
}

static Exp*
parse_unary(void)
{
	Exp *e;

	if(yypeek() == '-'){
		yyget();
		e = parse_unary();
		return mkOP(NEG, e, nil);
	}
	return parse_primary();
}

static Exp*
parse_postfix(void)
{
	Exp *e;

	e = parse_unary();
	while(yypeek() == '%'){
		yyget();
		e = mkOP(FRAC, e, nil);
	}
	return e;
}

static Exp*
parse_term(void)
{
	int t;
	Exp *e, *r;

	e = parse_postfix();
	for(;;){
		t = yypeek();
		if(t != '*' && t != '/')
			break;
		yyget();
		r = parse_postfix();
		if(t == '*')
			e = mkOP(MUL, e, r);
		else
			e = mkOP(DIV, e, r);
	}
	return e;
}

static Exp*
parse_expr(void)
{
	int t;
	Exp *e, *r;

	e = parse_term();
	for(;;){
		t = yypeek();
		if(t != '+' && t != '-')
			break;
		yyget();
		r = parse_term();
		if(t == '+')
			e = mkOP(ADD, e, r);
		else
			e = mkOP(SUB, e, r);
	}
	return e;
}

int
yyparse(void)
{
	Exp *e;

	yyhave = 0;
	e = parse_expr();
	if(yypeek() != 0)
		yyerror("syntax error");
	yyexp = e;
	return 0;
}

static vlong
eval(Exp *e)
{
	vlong i;

	switch(e->ty) {
	case NUM:
		return e->n;
	case DOT:
		return dot;
	case DOLLAR:
		return dollar;
	case ADD:
		return eval(e->e1)+eval(e->e2);
	case SUB:
		return eval(e->e1)-eval(e->e2);
	case MUL:
		return eval(e->e1)*eval(e->e2);
	case DIV:
		i = eval(e->e2);
		if(i == 0)
			yyerror("division by zero");
		return eval(e->e1)/i;
	case FRAC:
		return (size*eval(e->e1))/100;
	case NEG:
		return -eval(e->e1);
	}
	assert(0);
	return 0;
}

char*
parseexpr(char *s, vlong xdot, vlong xdollar, vlong xsize, vlong *result)
{
	char *err;

	errp = &err;
	if(setjmp(jmp))
		return err;

	inp = s;
	dot = xdot;
	size = xsize;
	dollar = xdollar;
	yyexp = nil;
	yyparse();
	if(yyexp == nil)
		return "nil yylval?";
	*result = eval(yyexp);
	return nil;
}

#ifdef TEST
void
main(int argc, char **argv)
{
	int i;
	vlong r;
	char *e;

	for(i=1; i<argc; i++)
		if(e = parseexpr(argv[i], 1000, 1000000, 1000000, &r))
			print("%s\n", e);
		else
			print("%lld\n", r);
}
#endif
