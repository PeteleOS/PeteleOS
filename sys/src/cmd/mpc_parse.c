
#include	<u.h>
#include	<libc.h>
#include	<bio.h>
#include 	<mp.h>

/*
 * Hand-written recursive-descent replacement for mpc.y (LALR).
 *
 * Original yacc precedence (low -> high):
 *	%left '{' '}' ';'
 *	%right '=' ','
 *	%right '?' ':'
 *	%left EQ NEQ '<' '>'
 *	%left LSH RSH
 *	%left '+' '-'
 *	%left '/' '%'
 *	%left '*'
 *	%left '^'
 *	%right '('
 * Token numbers (single chars keep ASCII; named in first-appearance
 * order): EQ=257 NEQ=258 LSH=259 RSH=260 MOD=261 IF=262 ELSE=263
 *	WHILE=264 BREAK=265 NAME=266 NUM=267.
 *
 * Precedence-map (hand parser, loosest -> tightest):
 *	level 1 (stmts, left): ';' '{' '}' sequencing via
 *	  parse_stmnts/parse_block (not expression precedence).
 *	level 2 (right): ',' (parse_comma) and '=' (assignment is
 *	  statement-level: stmnt: expr '=' expr, right).
 *	level 3 (right): '?' ':' ternary (parse_ternary; condition is
 *	  parse_bool, branches are full expr).
 *	level 4 (left): EQ NEQ '<' '>' comparisons (parse_cmp; NEQ is
 *	  sugar for '!' EQ, verbatim).
 *	level 5 (left): LSH RSH shifts (parse_shift).
 *	level 6 (left): '+' '-' additive (parse_add).
 *	level 7 (left): '/' '%' (parse_divmod).
 *	level 8 (left): '*' (parse_mul; tighter than '/' '%' per yacc).
 *	level 9 (left): '^' (parse_pow).
 *	level 10 (prefix, prec of '-' token i.e. level 6): unary '-'
 *	  (parse_unary).  Operand is parse_divmod (next tighter level),
 *	  so "-a+b" == "(-a)+b" and "-a*b" == "-(a*b)", matching yacc's
 *	  reduce-on-equal / shift-on-higher behaviour.
 *	level 11 (primary, prec '('): '(' expr ')', NAME, NUM,
 *	  NAME args (call) (parse_primary).
 * Call chain: parse_func -> parse_stmts -> parse_stmnt -> parse_expr
 *	-> parse_comma -> parse_ternary -> parse_bool -> parse_cmp
 *	-> parse_shift -> parse_add -> parse_divmod -> parse_mul
 *	-> parse_pow -> parse_unary -> parse_primary.
 * All actions (new/fcom) are verbatim from mpc.y.
 *
 * Error-recovery-map: original had no `error' production; yyerror()
 * prints "file:line: msg" and exits.  Hand parser calls yyerror()
 * at the exact failure point (unexpected token, missing ')', ';',
 * '}', etc.); recovery is fatal, matching yacc (no resync).
 */

typedef struct Sym Sym;
typedef struct Node Node;

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef FSET
#define FSET 1
#undef FUSE
#define FUSE 2
#undef FARG
#define FARG 4
#undef FLOC
#define FLOC 8

struct Sym
{
	Sym*	l;
	int	f;
	char	n[];
};

struct Node
{
	int	c;
	Node*	l;
	Node*	r;
	Sym*	s;
	mpint*	m;
	int	n;
};

#pragma	varargck type "N" Node*

int	ntmp;
Node	*ftmps, *atmps;
Node	*modulo;

Node*	new(int, Node*, Node*);
Sym*	sym(char*);

Biobuf	bin;
int	goteof;
int	lineno;
int	clevel;
char*	filename;

int	getch(void);
void	ungetc(void);
void	yyerror(char*);
int	yyparse(void);
void	diag(Node*, char*, ...);
void	com(Node*);
void	fcom(Node*,Node*,Node*);

#pragma varargck argpos cprint 1
#pragma varargck argpos diag 2

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef EQ
#define EQ 257
#undef NEQ
#define NEQ 258
#undef LSH
#define LSH 259
#undef RSH
#define RSH 260
#undef MOD
#define MOD 261
#undef IF
#define IF 262
#undef ELSE
#define ELSE 263
#undef WHILE
#define WHILE 264
#undef BREAK
#define BREAK 265
#undef NAME
#define NAME 266
#undef NUM
#define NUM 267

typedef union {
	Sym*	sval;
	Node*	node;
} YYSTYPE;
YYSTYPE yylval;

/* up to 4-token pushback for LL(2..3) decisions */
/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef NPB
#define NPB 4
static int nbuf;
static int tbuf[NPB];
static YYSTYPE vbuf[NPB];

static int yyget(void);
static void yyunget(int, YYSTYPE);
static int yypeek(void);
static Node* parse_name(void);
static Node* parse_num(void);
static Node* parse_args(void);
static Node* parse_elif(Node*, Node*);
static void parse_sem(void);
static Node* parse_stmnt(void);
static Node* parse_block(void);
static Node* parse_stmnts(void);
static Node* parse_expr(void);
static Node* parse_comma(void);
static Node* parse_ternary(void);
static Node* parse_bool(void);
static Node* parse_cmp(void);
static Node* parse_shift(void);
static Node* parse_add(void);
static Node* parse_divmod(void);
static Node* parse_mul(void);
static Node* parse_pow(void);
static Node* parse_unary(void);
static Node* parse_primary(void);

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

static Node*
parse_name(void)
{
	Node *n;

	if(yyget() != NAME)
		yyerror("syntax error");
	n = new(NAME, nil, nil);
	n->s = yylval.sval;
	return n;
}

static Node*
parse_num(void)
{
	Node *n;

	if(yyget() != NUM)
		yyerror("syntax error");
	n = new(NUM, nil, nil);
	n->s = yylval.sval;
	return n;
}

static Node*
parse_args(void)
{
	Node *e;
	int t;

	if(yyget() != '(')
		yyerror("syntax error");
	t = yypeek();
	if(t == ')'){
		yyget();
		return nil;
	}
	e = parse_expr();
	if(yyget() != ')')
		yyerror("syntax error");
	return e;
}

static Node*
parse_elif(void)
{
	/* called after IF '(' bool ')' stmnt when peek is ELSE;
	 * consumes ELSE ... and returns its node (verbatim actions). */
	Node *b, *c2, *t2;
	int t;

	if(yyget() != ELSE)
		yyerror("syntax error");
	t = yypeek();
	if(t == IF){
		yyget();
		if(yyget() != '(')
			yyerror("syntax error");
		c2 = parse_bool();
		if(yyget() != ')')
			yyerror("syntax error");
		t2 = parse_stmnt();
		if(yypeek() == ELSE){
			b = parse_elif();
			return new('?', c2, new(':', t2, b));
		}
		return new('?', c2, new(':', t2, nil));
	}
	b = parse_stmnt();
	return b;
}

static void
parse_sem(void)
{
	if(yyget() != ';')
		yyerror("syntax error");
	while(yypeek() == ';')
		yyget();
}

static Node*
parse_block(void)
{
	Node *s;

	if(yyget() != '{')
		yyerror("syntax error");
	s = parse_stmnts();
	if(yyget() != '}')
		yyerror("syntax error");
	return s;
}

static Node*
parse_stmnts(void)
{
	Node *l, *r;

	l = parse_stmnt();
	for(;;){
		int t;
		t = yypeek();
		if(t == '}' || t == -1 || t == 0)
			break;
		r = parse_stmnt();
		l = new('\n', l, r);
	}
	return l;
}

static Node*
parse_stmnt(void)
{
	int t;
	Node *e1, *e2, *b, *c;

	t = yypeek();
	if(t == MOD){
		yyget();
		e1 = parse_args();
		e2 = parse_stmnt();
		return new('m', e1, e2);
	}
	if(t == IF){
		yyget();
		if(yyget() != '(')
			yyerror("syntax error");
		c = parse_bool();
		if(yyget() != ')')
			yyerror("syntax error");
		b = parse_stmnt();
		if(yypeek() == ELSE){
			e2 = parse_elif();
			return new('?', c, new(':', b, e2));
		}
		return new('?', c, new(':', b, nil));
	}
	if(t == WHILE){
		yyget();
		if(yyget() != '(')
			yyerror("syntax error");
		c = parse_bool();
		if(yyget() != ')')
			yyerror("syntax error");
		b = parse_stmnt();
		return new('@', new('?', c, new(':', b, new('b', nil, nil))), nil);
	}
	if(t == BREAK){
		yyget();
		parse_sem();
		return new('b', nil, nil);
	}
	if(t == '{')
		return parse_block();
	/* expr '=' expr sem | expr sem */
	e1 = parse_expr();
	if(yypeek() == '='){
		yyget();
		e2 = parse_expr();
		parse_sem();
		return new('=', e1, e2);
	}
	parse_sem();
	if(e1->c == NAME)
		return new('e', e1, nil);
	return e1;
}

static Node*
parse_primary(void)
{
	int t;
	Node *n, *a;
	Sym *s;

	t = yyget();
	if(t == '('){
		n = parse_expr();
		if(yyget() != ')')
			yyerror("syntax error");
		return n;
	}
	if(t == NAME){
		s = yylval.sval;
		if(yypeek() == '('){
			n = new(NAME, nil, nil);
			n->s = s;
			a = parse_args();
			return new('e', n, a);
		}
		n = new(NAME, nil, nil);
		n->s = s;
		return n;
	}
	if(t == NUM){
		n = new(NUM, nil, nil);
		n->s = yylval.sval;
		return n;
	}
	yyerror("syntax error");
	return nil;
}

static Node*
parse_unary(void)
{
	Node *e, *z;

	if(yypeek() == '-'){
		yyget();
		e = parse_divmod();
		z = new(NUM, nil, nil);
		z->s = sym("0");
		z->s->f = 0;
		return new('-', z, e);
	}
	return parse_primary();
}

static Node*
parse_pow(void)
{
	Node *l, *r;

	l = parse_unary();
	while(yypeek() == '^'){
		yyget();
		r = parse_unary();
		l = new('^', l, r);
	}
	return l;
}

static Node*
parse_mul(void)
{
	Node *l, *r;

	l = parse_pow();
	while(yypeek() == '*'){
		yyget();
		r = parse_pow();
		l = new('*', l, r);
	}
	return l;
}

static Node*
parse_divmod(void)
{
	Node *l, *r;
	int t;

	l = parse_mul();
	for(;;){
		t = yypeek();
		if(t != '/' && t != '%')
			break;
		yyget();
		r = parse_mul();
		l = new(t, l, r);
	}
	return l;
}

static Node*
parse_add(void)
{
	Node *l, *r;
	int t;

	l = parse_divmod();
	for(;;){
		t = yypeek();
		if(t != '+' && t != '-')
			break;
		yyget();
		r = parse_divmod();
		l = new(t, l, r);
	}
	return l;
}

static Node*
parse_shift(void)
{
	Node *l, *r;
	int t;

	l = parse_add();
	for(;;){
		t = yypeek();
		if(t != LSH && t != RSH)
			break;
		yyget();
		r = parse_add();
		l = new(t, l, r);
	}
	return l;
}

static Node*
parse_cmp(void)
{
	Node *l, *r;
	int t;

	l = parse_shift();
	for(;;){
		t = yypeek();
		if(t != EQ && t != NEQ && t != '>' && t != '<')
			break;
		yyget();
		r = parse_shift();
		if(t == EQ)
			l = new(EQ, l, r);
		else if(t == NEQ)
			l = new('!', new(EQ, l, r), nil);
		else
			l = new(t, l, r);
	}
	return l;
}

static Node*
parse_bool(void)
{
	int t;
	Node *n, *b;

	t = yypeek();
	if(t == '!'){
		yyget();
		b = parse_bool();
		return new('!', b, nil);
	}
	if(t == '('){
		/* Could be '(' bool ')' or '(' expr ')'; both are
		 * '(' parse_bool ')' since parse_bool accepts plain
		 * expr when no comparison follows. */
		yyget();
		b = parse_bool();
		/* If inner was plain expr and next is not ')', it may
		 * be a comma/ternary continuation? No: bool position
		 * only; outer handles. */
		if(yyget() != ')')
			yyerror("syntax error");
		/* Also allow comparison after parenthesized bool?
		 * e.g. "(a) == b": our parse_cmp loop above already
		 * handles trailing EQ after primary? No: '(' case
		 * returned early. Handle trailing comparison here. */
		t = yypeek();
		if(t == EQ || t == NEQ || t == '>' || t == '<'){
			yyget();
			n = parse_shift();
			if(t == EQ)
				return new(EQ, b, n);
			if(t == NEQ)
				return new('!', new(EQ, b, n), nil);
			return new(t, b, n);
		}
		return b;
	}
	return parse_cmp();
}

static Node*
parse_ternary(void)
{
	Node *c, *a, *b;

	c = parse_bool();
	if(yypeek() != '?')
		return c;
	yyget();
	a = parse_expr();
	if(yyget() != ':')
		yyerror("syntax error");
	b = parse_expr();
	return new('?', c, new(':', a, b));
}

static Node*
parse_comma(void)
{
	Node *l, *r;

	l = parse_ternary();
	if(yypeek() != ',')
		return l;
	yyget();
	r = parse_comma();
	return new(',', l, r);
}

static Node*
parse_expr(void)
{
	return parse_comma();
}

int
yyparse(void)
{
	Node *n, *a, *b;

	if(yypeek() == 0 || yypeek() == -1)
		return 0;
	n = parse_name();
	a = parse_args();
	b = parse_stmnt();
	fcom(n, a, b);
	return 0;
}

int
yylex(void)
{
	static char buf[200];
	char *p;
	int c;

Loop:
	c = getch();
	switch(c){
	case -1:
		return -1;
	case ' ':
	case '\t':
	case '\n':
		goto Loop;
	case '#':
		while((c = getch()) > 0)
			if(c == '\n')
				break;
		goto Loop;
	}

	switch(c){
	case '?': case ':':
	case '+': case '-':
	case '*': case '^':
	case '/': case '%':
	case '{': case '}':
	case '(': case ')':
	case ',': case ';':
		return c;
	case '<':
		if(getch() == '<') return LSH;
		ungetc();
		return '<';
	case '>':
		if(getch() == '>') return RSH;
		ungetc();
		return '>';
	case '=':
		if(getch() == '=') return EQ;
		ungetc();
		return '=';
	case '!':
		if(getch() == '=') return NEQ;
		ungetc();
		return '!';
	}

	ungetc();
	p = buf;
	for(;;){
		c = getch();
		if((c >= Runeself)
		|| (c == '_')
		|| (c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| (c >= '0' && c <= '9')){
			*p++ = c;
			continue;
		}
		ungetc();
		break;
	}
	*p = '\0';

	if(strcmp(buf, "mod") == 0)
		return MOD;
	if(strcmp(buf, "if") == 0)
		return IF;
	if(strcmp(buf, "else") == 0)
		return ELSE;
	if(strcmp(buf, "while") == 0)
		return WHILE;
	if(strcmp(buf, "break") == 0)
		return BREAK;

	yylval.sval = sym(buf);
	yylval.sval->f = 0;
	return (buf[0] >= '0' && buf[0] <= '9') ? NUM : NAME;
}


int
getch(void)
{
	int c;

	c = Bgetc(&bin);
	if(c == Beof){
		goteof = 1;
		return -1;
	}
	if(c == '\n')
		lineno++;
	return c;
}

void
ungetc(void)
{
	Bungetc(&bin);
}

Node*
new(int c, Node *l, Node *r)
{
	Node *n;

	n = malloc(sizeof(Node));
	n->c = c;
	n->l = l;
	n->r = r;
	n->s = nil;
	n->m = nil;
	n->n = lineno;
	return n;
}

Sym*
sym(char *n)
{
	static Sym *tab[128];
	Sym *s;
	ulong h, t;
	int i;

	h = 0;
	for(i=0; n[i] != '\0'; i++){
		t = h & 0xf8000000;
		h <<= 5;
		h ^= t>>27;
		h ^= (ulong)n[i];
	}
	h %= nelem(tab);
	for(s = tab[h]; s != nil; s = s->l)
		if(strcmp(s->n, n) == 0)
			return s;
	s = malloc(sizeof(Sym)+i+1);
	memmove(s->n, n, i+1);
	s->f = 0;
	s->l = tab[h];
	tab[h] = s;
	return s;
}

void
yyerror(char *s)
{
	fprint(2, "%s:%d: %s\n", filename, lineno, s);
	exits(s);
}
void
cprint(char *fmt, ...)
{
	static char buf[1024], tabs[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	char *p, *x;
	va_list a;

	va_start(a, fmt);
	vsnprint(buf, sizeof(buf), fmt, a);
	va_end(a);

	p = buf;
	while((x = strchr(p, '\n')) != nil){
		x++;
		write(1, p, x-p);
		p = &tabs[sizeof(tabs)-1 - clevel];
		if(*p != '\0')
			write(1, p, strlen(p));
		p = x;
	}
	if(*p != '\0')
		write(1, p, strlen(p));
}

Node*
alloctmp(void)
{
	Node *t;

	t = ftmps;
	if(t != nil)
		ftmps = t->l;
	else {
		char n[16];

		snprint(n, sizeof(n), "tmp%d", ++ntmp);
		t = new(NAME, nil, nil);
		t->s = sym(n);

		cprint("mpint *");
	}
	cprint("%N = mpnew(0);\n", t);
	t->s->f &= ~(FSET|FUSE);
	t->l = atmps;
	atmps = t;
	return t;
}

int
isconst(Node *n)
{
	if(n->c == NUM)
		return 1;
	if(n->c == NAME){
		return 	n->s == sym("mpzero") ||
			n->s == sym("mpone") ||
			n->s == sym("mptwo");
	}
	return 0;
}

int
istmp(Node *n)
{
	Node *l;

	if(n->c == NAME){
		for(l = atmps; l != nil; l = l->l){
			if(l->s == n->s)
				return 1;
		}
	}
	return 0;
}


void
freetmp(Node *t)
{
	Node **ll, *l;

	if(t == nil)
		return;
	if(t->c == ','){
		freetmp(t->l);
		freetmp(t->r);
		return;
	}
	if(t->c != NAME)
		return;

	ll = &atmps;
	for(l = atmps; l != nil; l = l->l){
		if(l == t){
			cprint("mpfree(%N);\n", t);
			*ll = t->l;
			t->l = ftmps;
			ftmps = t;
			return;
		}
		ll = &l->l;
	}
}

int
symref(Node *n, Sym *s)
{
	if(n == nil)
		return 0;
	if(n->c == NAME && n->s == s)
		return 1;
	return symref(n->l, s) || symref(n->r, s);
}

void
nodeset(Node *n)
{
	if(n == nil)
		return;
	if(n->c == NAME){
		n->s->f |= FSET;
		return;
	}
	if(n->c == ','){
		nodeset(n->l);
		nodeset(n->r);
	}
}

int
complex(Node *n)
{
	if(n->c == NAME)
		return 0;
	if(n->c == NUM && n->m->sign > 0 && mpcmp(n->m, mptwo) <= 0)
		return 0;
	return 1;
}

void
bcom(Node *n, Node *t);

Node*
ccom(Node *f)
{
	Node *l, *r;

	if(f == nil)
		return nil;

	if(f->m != nil)
		return f;
	f->m = (void*)~0;

	switch(f->c){
	case NUM:
		f->m = strtomp(f->s->n, nil, 0, nil);
		if(f->m == nil)
			diag(f, "bad constant");
		goto out;

	case LSH:
	case RSH:
		break;

	case '+':
	case '-':
	case '*':
	case '/':
	case '%':
	case '^':
		if(modulo == nil || modulo->c == NUM)
			break;

		/* wet floor */
	default:
		return f;
	}

	f->l = l = ccom(f->l);
	f->r = r = ccom(f->r);
	if(l == nil || r == nil || l->c != NUM || r->c != NUM)
		return f;

	f->m = mpnew(0);
	switch(f->c){
	case LSH:
	case RSH:
		if(mpsignif(r->m) > 32)
			diag(f, "bad shift");
		if(f->c == LSH)
			mpleft(l->m, mptoi(r->m), f->m);
		else
			mpright(l->m, mptoi(r->m), f->m);
		goto out;

	case '+':
		mpadd(l->m, r->m, f->m);
		break;
	case '-':
		mpsub(l->m, r->m, f->m);
		break;
	case '*':
		mpmul(l->m, r->m, f->m);
		break;
	case '/':
		if(modulo != nil){
			mpinvert(r->m, modulo->m, f->m);
			mpmul(f->m, l->m, f->m);
		} else {
			mpdiv(l->m, r->m, f->m, nil);
		}
		break;
	case '%':
		mpmod(l->m, r->m, f->m);
		break;
	case '^':
		mpexp(l->m, r->m, modulo != nil ? modulo->m : nil, f->m);
		goto out;
	}
	if(modulo != nil)
		mpmod(f->m, modulo->m, f->m);

out:
	f->l = nil;
	f->r = nil;
	f->s = nil;
	f->c = NUM;
	return f;
}

Node*
ecom(Node *f, Node *t)
{
	Node *l, *r, *t2;

	if(f == nil)
		return nil;

	f = ccom(f);
	if(f->c == NUM){
		if(f->m->sign < 0){
			f->m->sign = 1;
			t = ecom(f, t);
			f->m->sign = -1;
			if(isconst(t))
				t = ecom(t, alloctmp());
			cprint("%N->sign = -1;\n", t);
			return t;
		}
		if(mpcmp(f->m, mpzero) == 0){
			f->c = NAME;
			f->s = sym("mpzero");
			f->s->f = FSET;
			return ecom(f, t);
		}
		if(mpcmp(f->m, mpone) == 0){
			f->c = NAME;
			f->s = sym("mpone");
			f->s->f = FSET;
			return ecom(f, t);
		}
		if(mpcmp(f->m, mptwo) == 0){
			f->c = NAME;
			f->s = sym("mptwo");
			f->s->f = FSET;
			return ecom(f, t);
		}
	}

	if(f->c == ','){
		if(t != nil)
			diag(f, "cannot assign list to %N", t);
		f->l = ecom(f->l, nil);
		f->r = ecom(f->r, nil);
		return f;
	}

	l = r = nil;
	if(f->c == NAME){
		if((f->s->f & FSET) == 0)
			diag(f, "name used but not set");
		f->s->f |= FUSE;
		if(t == nil)
			return f;
		if(f->s != t->s)
			cprint("mpassign(%N, %N);\n", f, t);
		goto out;
	}

	if(t == nil)
		t = alloctmp();

	if(f->c == '?'){
		bcom(f, t);
		goto out;
	}

	if(f->c == 'e'){
		r = ecom(f->r, nil);
		if(r == nil)
			cprint("%N(%N);\n", f->l, t);
		else
			cprint("%N(%N, %N);\n", f->l, r, t);
		goto out;
	}

	if(t->c != NAME)
		diag(f, "destination %N not a name", t);

	switch(f->c){
	case NUM:
		if(mpsignif(f->m) <= 32)
			cprint("uitomp(%uUL, %N);\n", mptoui(f->m), t);
		else if(mpsignif(f->m) <= 64)
			cprint("uvtomp(%lluULL, %N);\n", mptouv(f->m), t);
		else
			cprint("strtomp(\"%.16B\", nil, 16, %N);\n", f->m, t);
		goto out;
	case LSH:
	case RSH:
		r = ccom(f->r);
		if(r == nil || r->c != NUM || mpsignif(r->m) > 32)
			diag(f, "bad shift");
		l = f->l->c == NAME ? f->l : ecom(f->l, t);
		if(f->c == LSH)
			cprint("mpleft(%N, %d, %N);\n", l, mptoi(r->m), t);
		else
			cprint("mpright(%N, %d, %N);\n", l, mptoi(r->m), t);
		goto out;
	case '*':
	case '/':
		l = ecom(f->l, nil);
		r = ecom(f->r, nil);
		break;
	default:
		l = ccom(f->l);
		r = ccom(f->r);
		l = ecom(l, complex(l) && !symref(r, t->s) ? t : nil);
		r = ecom(r, complex(r) && l->s != t->s ? t : nil);
		break;
	}


	if(modulo != nil){
		switch(f->c){
		case '+':
			cprint("mpmodadd(%N, %N, %N, %N);\n", l, r, modulo, t);
			goto out;
		case '-':
			cprint("mpmodsub(%N, %N, %N, %N);\n", l, r, modulo, t);
			goto out;
		case '*':
		Modmul:
			if(l->s == sym("mptwo") || r->s == sym("mptwo"))
				cprint("mpmodadd(%N, %N, %N, %N); // 2*%N\n",
					r->s == sym("mptwo") ? l : r,
					r->s == sym("mptwo") ? l : r,
					modulo, t,
					r);
			else
				cprint("mpmodmul(%N, %N, %N, %N);\n", l, r, modulo, t);
			goto out;
		case '/':
			if(l->s == sym("mpone")){
				cprint("mpinvert(%N, %N, %N);\n", r, modulo, t);
				goto out;
			}
			t2 = alloctmp();
			cprint("mpinvert(%N, %N, %N);\n", r, modulo, t2);
			cprint("mpmodmul(%N, %N, %N, %N);\n", l, t2, modulo, t);
			freetmp(t2);
			goto out;
		case '^':
			if(r->s == sym("mptwo")){
				r = l;
				goto Modmul;
			}
			cprint("mpexp(%N, %N, %N, %N);\n", l, r, modulo, t);
			goto out;
		}
	}

	switch(f->c){
	case '+':
		cprint("mpadd(%N, %N, %N);\n", l, r, t);
		goto out;
	case '-':
		if(l->s == sym("mpzero")){
			r = ecom(r, t);
			cprint("%N->sign = -%N->sign;\n", t, t);
		} else
			cprint("mpsub(%N, %N, %N);\n", l, r, t);
		goto out;
	case '*':
	Mul:
		if(l->s == sym("mptwo") || r->s == sym("mptwo"))
			cprint("mpleft(%N, 1, %N);\n", r->s == sym("mptwo") ? l : r, t);
		else
			cprint("mpmul(%N, %N, %N);\n", l, r, t);
		goto out;
	case '/':
		cprint("mpdiv(%N, %N, %N, %N);\n", l, r, t, nil);
		goto out;
	case '%':
		cprint("mpmod(%N, %N, %N);\n", l, r, t);
		goto out;
	case '^':
		if(r->s == sym("mptwo")){
			r = l;
			goto Mul;
		}
		cprint("mpexp(%N, %N, nil, %N);\n", l, r, t);
		goto out;
	default:
		diag(f, "unknown operation");
	}

out:
	if(l != t)
		freetmp(l);
	if(r != t)
		freetmp(r);
	nodeset(t);
	return t;
}

void
bcom(Node *n, Node *t)
{
	Node *f, *l, *r;
	int neg = 0;

	l = r = nil;
	f = n->l;
Loop:
	switch(f->c){
	case '!':
		neg = !neg;
		f = f->l;
		goto Loop;
	case '>':
	case '<':
	case EQ:
		l = ecom(f->l, nil);
		r = ecom(f->r, nil);
		if(t != nil) {
			Node *b1, *b2;

			b1 = ecom(n->r->l, nil);
			b2 = ecom(n->r->r, nil);
			cprint("mpsel(");

			if(l->s == r->s)
				cprint("0");
			else {
				if(f->c == '>')
					cprint("-");
				cprint("mpcmp(%N, %N)", l, r);
			}
			if(f->c == EQ)
				neg = !neg;
			else
				cprint(" >> (sizeof(int)*8-1)");

			cprint(", %N, %N, %N);\n", neg ? b2 : b1, neg ? b1 : b2, t);
			freetmp(b1);
			freetmp(b2);
		} else {
			cprint("if(");

			if(l->s == r->s)
				cprint("0");
			else
				cprint("mpcmp(%N, %N)", l, r);
			if(f->c == EQ)
				cprint(neg ? " != 0" : " == 0");
			else if(f->c == '>')
				cprint(neg ? " <= 0" : " > 0");
			else
				cprint(neg ? " >= 0" : " < 0");

			cprint(")");
			com(n->r);
		}
		break;
	default:
		diag(n, "saw %N in boolean expression", f);
	}
	freetmp(l);
	freetmp(r);
}

void
com(Node *n)
{
	Node *l, *r;

Loop:
	if(n != nil)
	switch(n->c){
	case '\n':
		com(n->l);
		n = n->r;
		goto Loop;
	case '?':
		bcom(n, nil);
		break;
	case 'b':
		for(l = atmps; l != nil; l = l->l)
			cprint("mpfree(%N);\n", l);
		cprint("break;\n");
		break;
	case '@':
		cprint("for(;;)");
	case ':':
		clevel++;
		cprint("{\n");
		l = ftmps;
		r = atmps;
		if(n->c == '@')
			atmps = nil;
		ftmps = nil;
		com(n->l);
		if(n->r != nil){
			cprint("}else{\n");
			ftmps = nil;
			com(n->r);
		}
		ftmps = l;
		atmps = r;
		clevel--;
		cprint("}\n");
		break;
	case 'm':
		l = modulo;
		modulo = ecom(n->l, nil);
		com(n->r);
		freetmp(modulo);
		modulo = l;
		break;
	case 'e':
		if(n->r == nil)
			cprint("%N();\n", n->l);
		else {
			r = ecom(n->r, nil);
			cprint("%N(%N);\n", n->l, r);
			freetmp(r);
		}
		break;
	case '=':
		ecom(n->r, n->l);
		break;
	}
}

Node*
flocs(Node *n, Node *r)
{
Loop:
	if(n != nil)
	switch(n->c){
	default:
		r = flocs(n->l, r);
		r = flocs(n->r, r);
		n = n->r;
		goto Loop;
	case '=':
		n = n->l;
		if(n == nil)
			diag(n, "lhs is nil");
		while(n->c == ','){
			n->c = '=';
			r = flocs(n, r);
			n->c = ',';
			n = n->r;
			if(n == nil)
				return r;
		}
		if(n->c == NAME && (n->s->f & (FARG|FLOC)) == 0){
			n->s->f = FLOC;
			return new(',', n, r);
		}
		break;
	}
	return r;
}

void
fcom(Node *f, Node *a, Node *b)
{
	Node *a0, *l0, *l;

	ntmp = 0;
	ftmps = atmps = modulo = nil;
	clevel = 1;
	cprint("void %N(", f);
	a0 = a;
	while(a != nil){
		if(a != a0)
			cprint(", ");
		l = a->c == NAME ? a : a->l;
		l->s->f = FARG|FSET;
		cprint("mpint *%N", l);
		a = a->r;
	}
	cprint("){\n");
	l0 = flocs(b, nil);
	for(a = l0; a != nil; a = a->r)
		cprint("mpint *%N = mpnew(0);\n", a->l);
	com(b);
	for(a = l0; a != nil; a = a->r)
		cprint("mpfree(%N);\n", a->l);
	clevel = 0;
	cprint("}\n");
}

void
diag(Node *n, char *fmt, ...)
{
	static char buf[1024];
	va_list a;

	va_start(a, fmt);
	vsnprint(buf, sizeof(buf), fmt, a);
	va_end(a);

	fprint(2, "%s:%d: for %N; %s\n", filename, n->n, n, buf);
	exits("error");
}

int
Nfmt(Fmt *f)
{
	Node *n = va_arg(f->args, Node*);

	if(n == nil)
		return fmtprint(f, "nil");

	if(n->c == ',')
		return fmtprint(f, "%N, %N", n->l, n->r);

	switch(n->c){
	case NUM:
		if(n->m != nil)
			return fmtprint(f, "%B", n->m);
		/* wet floor */
	case NAME:
		return fmtprint(f, "%s", n->s->n);
	case EQ:
		return fmtprint(f, "==");
	case IF:
		return fmtprint(f, "if");
	case ELSE:
		return fmtprint(f, "else");
	case MOD:
		return fmtprint(f, "mod");
	default:
		return fmtprint(f, "%c", (char)n->c);
	}
}

void
parse(int fd, char *file)
{
	Binit(&bin, fd, OREAD);
	filename = file;
	clevel = 0;
	lineno = 1;
	goteof = 0;
	while(!goteof)
		yyparse();
	Bterm(&bin);
}

void
usage(void)
{
	fprint(2, "%s [file ...]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	fmtinstall('N', Nfmt);
	fmtinstall('B', mpfmt);

	ARGBEGIN {
	default:
		usage();
	} ARGEND;

	if(argc == 0){
		parse(0, "<stdin>");
		exits(nil);
	}
	while(*argv != nil){
		int fd;

		if((fd = open(*argv, OREAD)) < 0){
			fprint(2, "%s: %r\n", *argv);
			exits("error");
		}
		parse(fd, *argv);
		close(fd);
		argv++;
	}
	exits(nil);
}
