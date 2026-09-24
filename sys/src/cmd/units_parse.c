#include <u.h>
#include <libc.h>
#include <bio.h>

/*
 * Hand-written recursive-descent replacement for units.y (LALR).
 *
 * Original yacc grammar had NO precedence declarations; stratification
 * is explicit via nonterminals (low -> high):
 *	prog: ':' VAR expr | ':' VAR '#' | '?' expr | '?'
 *	expr: expr4 | expr '+' expr4 | expr '-' expr4
 *	expr4: expr3 | expr4 '*' expr3 | expr4 '/' expr3
 *	expr3: expr2 | expr3 expr2			(juxtaposition = multiply)
 *	expr2: expr1 | expr2 SUP | expr2 '^' expr1
 *	expr1: expr0 | expr1 '|' expr0			('|' = division)
 *	expr0: VAR | VAL | '(' expr ')'
 *
 * Precedence-map (hand parser, loosest -> tightest):
 *	level 1 (left): '+' '-'
 *	level 2 (left): '*' '/'
 *	level 3 (left, highest tightness via juxtaposition): implicit multiply
 *	  (expr3 expr2).  Abutted factors bind tighter than '*'/'/'.
 *	level 4: postfix SUP (left), infix '^' (left, right operand is expr1)
 *	level 5 (left): '|' (division)
 *	level 6 (primary): VAR, VAL, '(' expr ')'
 * Call chain: parse_expr -> parse_expr4 -> parse_expr3 ->
 *              parse_expr2 -> parse_expr1 -> parse_expr0.
 * Actions are verbatim from units.y (add/sub/mul/div/xpn, retnode1,
 * VAR definition, dimension checks).
 *
 * Error-recovery-map: original had no `error' production; yyerror()
 * prints "lineno: line / msg", bumps nerrors, exits after >5 errors.
 * Hand parser calls yyerror() at the exact failure point (unexpected
 * token, missing ')', trailing garbage) and returns 1, aborting the
 * current line.  Caller (main) discards the line buffer and reads the
 * next line, matching yacc's abort-and-restart (no intra-line resync).
 */

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef Ndim
#define Ndim 15
#undef Nsym
#define Nsym 40
#undef Nvar
#define Nvar 203
#undef Maxe
#define Maxe 695

typedef	struct	Var	Var;
typedef	struct	Node	Node;
typedef	struct	Prefix	Prefix;

struct	Node
{
	double	val;
	schar	dim[Ndim];
};
struct	Var
{
	Rune	name[Nsym];
	Node	node;
	Var*	link;
};
struct	Prefix
{
	double	val;
	Rune*	pname;
};

char	buf[100];
int	digval;
Biobuf*	fi;
Biobuf	linebuf;
Var*	fund[Ndim];
Rune	line[1000];
ulong	lineno;
int	linep;
int	nerrors;
Node	one;
int	peekrune;
Node	retnode1;
Node	retnode2;
Node	retnode;
Rune	sym[Nsym];
Var*	vars[Nvar];
int	vflag;

extern	void	add(Node*, Node*, Node*);
extern	void	div(Node*, Node*, Node*);
extern	int	specialcase(Node*, Node*, Node*);
extern	double	fadd(double, double);
extern	double	fdiv(double, double);
extern	double	fmul(double, double);
extern	int	gdigit(void*);
extern	Var*	lookup(int);
extern	void	main(int, char*[]);
extern	void	mul(Node*, Node*, Node*);
extern	void	ofile(void);
extern	double	pname(void);
extern	void	printdim(char*, int, int);
extern	int	ralpha(int);
extern	int	readline(void);
extern	void	sub(Node*, Node*, Node*);
extern	int	Ufmt(Fmt*);
extern	void	xpn(Node*, Node*, int);
extern	void	yyerror(char*, ...);
extern	int	yylex(void);
extern	int	yyparse(void);

typedef	Node*	indnode;
#pragma	varargck	type	"U"	indnode

/* Token numbers: single-char tokens keep ASCII; named tokens >255
 * in yacc declaration order (VAL, VAR, SUP). */
/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef VAL
#define VAL 257
#undef VAR
#define VAR 258
#undef SUP
#define SUP 259

typedef union {
	Node	node;
	Var*	var;
	int	numb;
	double	val;
} YYSTYPE;
YYSTYPE yylval;

/* 1-token lookahead buffer over yylex() */
static int yyhave;
static int yysave;
static YYSTYPE yysaveval;

static int yypeek(void);
static int yyget(void);
static Node parse_expr(void);
static Node parse_expr4(void);
static Node parse_expr3(void);
static Node parse_expr2(void);
static Node parse_expr1(void);
static Node parse_expr0(void);

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

static Node
parse_expr0(void)
{
	int t;
	Node n;
	Var *v;

	t = yyget();
	switch(t){
	case VAR:
		v = yylval.var;
		if(v->node.dim[0] == 0) {
			yyerror("undefined %S", v->name);
			n = one;
		} else
			n = v->node;
		return n;
	case VAL:
		n = one;
		n.val = yylval.val;
		return n;
	case '(':
		n = parse_expr();
		if(yyget() != ')')
			yyerror("syntax error");
		return n;
	default:
		yyerror("syntax error");
		return one;	/* not reached; yyerror may return */
	}
}

static Node
parse_expr1(void)
{
	Node l, r, n;

	l = parse_expr0();
	for(;;){
		if(yypeek() != '|')
			break;
		yyget();
		r = parse_expr0();
		div(&n, &l, &r);
		l = n;
	}
	return l;
}

static Node
parse_expr2(void)
{
	Node l, r, n;
	int t, i;

	l = parse_expr1();
	for(;;){
		t = yypeek();
		if(t == SUP){
			yyget();
			xpn(&n, &l, yylval.numb);
			l = n;
		} else if(t == '^'){
			yyget();
			r = parse_expr1();
			for(i=1; i<Ndim; i++)
				if(r.dim[i]) {
					yyerror("exponent has units");
					n = l;
					goto next;
				}
			i = r.val;
			if(i != r.val)
				yyerror("exponent not integral");
			xpn(&n, &l, i);
			l = n;
		next:;
		} else
			break;
	}
	return l;
}

static int
canstart_expr2(int t)
{
	return t == VAR || t == VAL || t == '(';
}

static Node
parse_expr3(void)
{
	Node l, r, n;

	l = parse_expr2();
	for(;;){
		if(!canstart_expr2(yypeek()))
			break;
		r = parse_expr2();
		mul(&n, &l, &r);
		l = n;
	}
	return l;
}

static Node
parse_expr4(void)
{
	Node l, r, n;
	int t;

	l = parse_expr3();
	for(;;){
		t = yypeek();
		if(t != '*' && t != '/')
			break;
		yyget();
		r = parse_expr3();
		if(t == '*')
			mul(&n, &l, &r);
		else
			div(&n, &l, &r);
		l = n;
	}
	return l;
}

static Node
parse_expr(void)
{
	Node l, r, n;
	int t;

	l = parse_expr4();
	for(;;){
		t = yypeek();
		if(t != '+' && t != '-')
			break;
		yyget();
		r = parse_expr4();
		if(t == '+')
			add(&n, &l, &r);
		else
			sub(&n, &l, &r);
		l = n;
	}
	return l;
}

int
yyparse(void)
{
	int t;
	Var *v;
	Node n;
	int f, i;

	yyhave = 0;
	t = yyget();
	if(t == ':'){
		if(yyget() != VAR)
			yyerror("syntax error");
		v = yylval.var;
		t = yypeek();
		if(t == '#'){
			yyget();
			if(yypeek() != 0)
				yyerror("syntax error");
			for(i=1; i<Ndim; i++)
				if(fund[i] == 0)
					break;
			if(i >= Ndim) {
				yyerror("too many dimensions");
				i = Ndim-1;
			}
			fund[i] = v;
			f = v->node.dim[0];
			v->node = one;
			v->node.dim[0] = 1;
			v->node.dim[i] = 1;
			if(f)
				yyerror("redefinition of %S", v->name);
			else
			if(vflag)
				print("%S\t#\n", v->name);
			return 0;
		}
		n = parse_expr();
		if(yypeek() != 0)
			yyerror("syntax error");
		f = v->node.dim[0];
		v->node = n;
		v->node.dim[0] = 1;
		if(f)
			yyerror("redefinition of %S", v->name);
		else
		if(vflag)
			print("%S\t%U\n", v->name, &v->node);
		return 0;
	}
	if(t == '?'){
		if(yypeek() == 0){
			retnode1 = one;
			return 0;
		}
		n = parse_expr();
		if(yypeek() != 0)
			yyerror("syntax error");
		retnode1 = n;
		return 0;
	}
	yyerror("syntax error");
	return 1;
}

int
yylex(void)
{
	int c, i;

	c = peekrune;
	peekrune = ' ';

loop:
	if((c >= '0' && c <= '9') || c == '.')
		goto numb;
	if(ralpha(c))
		goto alpha;
	switch(c) {
	case ' ':
	case '\t':
		c = line[linep++];
		goto loop;
	case L'×':
		return '*';
	case L'÷':
		return '/';
	case L'¹':
	case L'ⁱ':
		yylval.numb = 1;
		return SUP;
	case L'²':
	case L'⁲':
		yylval.numb = 2;
		return SUP;
	case L'³':
	case L'⁳':
		yylval.numb = 3;
		return SUP;
	}
	return c;

alpha:
	memset(sym, 0, sizeof(sym));
	for(i=0;; i++) {
		if(i < nelem(sym))
			sym[i] = c;
		c = line[linep++];
		if(!ralpha(c))
			break;
	}
	sym[nelem(sym)-1] = 0;
	peekrune = c;
	yylval.var = lookup(0);
	return VAR;

numb:
	digval = c;
	yylval.val = charstod(gdigit, 0);
	return VAL;
}

void
main(int argc, char *argv[])
{
	char *file;

	ARGBEGIN {
	default:
		print("usage: units [-v] [file]\n");
		exits("usage");
	case 'v':
		vflag = 1;
		break;
	} ARGEND

	file = "/lib/units";
	if(argc > 0)
		file = argv[0];
	fi = Bopen(file, OREAD);
	if(fi == 0) {
		print("cant open: %s\n", file);
		exits("open");
	}
	fmtinstall('U', Ufmt);
	one.val = 1;

	/*
	 * read the 'units' file to
	 * develope a database
	 */
	lineno = 0;
	for(;;) {
		lineno++;
		if(readline())
			break;
		if(line[0] == 0 || line[0] == '/')
			continue;
		peekrune = ':';
		yyparse();
	}

	/*
	 * read the console to
	 * print ratio of pairs
	 */
	Bterm(fi);
	fi = &linebuf;
	Binit(fi, 0, OREAD);
	lineno = 0;
	for(;;) {
		if(lineno & 1)
			print("you want: ");
		else
			print("you have: ");
		if(readline())
			break;
		peekrune = '?';
		nerrors = 0;
		yyparse();
		if(nerrors)
			continue;
		if(lineno & 1) {
			if(specialcase(&retnode, &retnode2, &retnode1))
				print("\tis %U\n", &retnode);
			else {
				div(&retnode, &retnode2, &retnode1);
				print("\t* %U\n", &retnode);
				div(&retnode, &retnode1, &retnode2);
				print("\t/ %U\n", &retnode);
			}
		} else
			retnode2 = retnode1;
		lineno++;
	}
	print("\n");
	exits(0);
}

/*
 * all characters that have some
 * meaning. rest are usable as names
 */
int
ralpha(int c)
{
	switch(c) {
	case 0:
	case '+':
	case '-':
	case '*':
	case '/':
	case '[':
	case ']':
	case '(':
	case ')':
	case '^':
	case ':':
	case '?':
	case ' ':
	case '\t':
	case '.':
	case '|':
	case '#':
	case L'¹':
	case L'ⁱ':
	case L'²':
	case L'⁲':
	case L'³':
	case L'⁳':
	case L'×':
	case L'÷':
		return 0;
	}
	return 1;
}

int
gdigit(void*)
{
	int c;

	c = digval;
	if(c) {
		digval = 0;
		return c;
	}
	c = line[linep++];
	peekrune = c;
	return c;
}

void
yyerror(char *fmt, ...)
{
	va_list arg;

	/*
	 * hack to intercept message from yaccpar
	 */
	if(strcmp(fmt, "syntax error") == 0) {
		yyerror("syntax error, last name: %S", sym);
		return;
	}
	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	print("%lud: %S\n\t%s\n", lineno, line, buf);
	nerrors++;
	if(nerrors > 5) {
		print("too many errors\n");
		exits("errors");
	}
}

void
add(Node *c, Node *a, Node *b)
{
	int i, d;

	for(i=0; i<Ndim; i++) {
		d = a->dim[i];
		c->dim[i] = d;
		if(d != b->dim[i])
			yyerror("add must be like units");
	}
	c->val = fadd(a->val, b->val);
}

void
sub(Node *c, Node *a, Node *b)
{
	int i, d;

	for(i=0; i<Ndim; i++) {
		d = a->dim[i];
		c->dim[i] = d;
		if(d != b->dim[i])
			yyerror("sub must be like units");
	}
	c->val = fadd(a->val, -b->val);
}

void
mul(Node *c, Node *a, Node *b)
{
	int i;

	for(i=0; i<Ndim; i++)
		c->dim[i] = a->dim[i] + b->dim[i];
	c->val = fmul(a->val, b->val);
}

void
div(Node *c, Node *a, Node *b)
{
	int i;

	for(i=0; i<Ndim; i++)
		c->dim[i] = a->dim[i] - b->dim[i];
	c->val = fdiv(a->val, b->val);
}

void
xpn(Node *c, Node *a, int b)
{
	int i;

	*c = one;
	if(b < 0) {
		b = -b;
		for(i=0; i<b; i++)
			div(c, c, a);
	} else
	for(i=0; i<b; i++)
		mul(c, c, a);
}

int
specialcase(Node *c, Node *a, Node *b)
{
	int i, d, d1, d2;

	d1 = 0;
	d2 = 0;
	for(i=1; i<Ndim; i++) {
		d = a->dim[i];
		if(d) {
			if(d != 1 || d1)
				return 0;
			d1 = i;
		}
		d = b->dim[i];
		if(d) {
			if(d != 1 || d2)
				return 0;
			d2 = i;
		}
	}
	if(d1 == 0 || d2 == 0)
		return 0;

	if(memcmp(fund[d1]->name, L"°C", 3*sizeof(Rune)) == 0 &&
	   memcmp(fund[d2]->name, L"°F", 3*sizeof(Rune)) == 0 &&
	   b->val == 1) {
		memcpy(c->dim, b->dim, sizeof(c->dim));
		c->val = a->val * 9. / 5. + 32.;
		return 1;
	}

	if(memcmp(fund[d1]->name, L"°F", 3*sizeof(Rune)) == 0 &&
	   memcmp(fund[d2]->name, L"°C", 3*sizeof(Rune)) == 0 &&
	   b->val == 1) {
		memcpy(c->dim, b->dim, sizeof(c->dim));
		c->val = (a->val - 32.) * 5. / 9.;
		return 1;
	}
	return 0;
}

void
printdim(char *str, int d, int n)
{
	Var *v;

	if(n) {
		v = fund[d];
		if(v)
			sprint(strchr(str, 0), " %S", v->name);
		else
			sprint(strchr(str, 0), " [%d]", d);
		switch(n) {
		case 1:
			break;
		case 2:
			strcat(str, "²");
			break;
		case 3:
			strcat(str, "³");
			break;
		default:
			sprint(strchr(str, 0), "^%d", n);
		}
	}
}

int
Ufmt(Fmt *fp)
{
	char str[200];
	Node *n;
	int f, i, d;

	n = va_arg(fp->args, Node*);
	sprint(str, "%g", n->val);

	f = 0;
	for(i=1; i<Ndim; i++) {
		d = n->dim[i];
		if(d > 0)
			printdim(str, i, d);
		else
		if(d < 0)
			f = 1;
	}

	if(f) {
		strcat(str, " /");
		for(i=1; i<Ndim; i++) {
			d = n->dim[i];
			if(d < 0)
				printdim(str, i, -d);
		}
	}

	return fmtstrcpy(fp, str);
}

int
readline(void)
{
	int i, c;

	linep = 0;
	for(i=0;; i++) {
		c = Bgetrune(fi);
		if(c < 0)
			return 1;
		if(c == '\n')
			break;
		if(i < nelem(line))
			line[i] = c;
	}
	if(i >= nelem(line))
		i = nelem(line)-1;
	line[i] = 0;
	return 0;
}

Var*
lookup(int f)
{
	int i;
	Var *v, *w;
	double p;
	ulong h;

	h = 0;
	for(i=0; sym[i]; i++)
		h = h*13 + sym[i];
	h %= nelem(vars);

	for(v=vars[h]; v; v=v->link)
		if(memcmp(sym, v->name, sizeof(sym)) == 0)
			return v;
	if(f)
		return 0;
	v = malloc(sizeof(*v));
	if(v == nil) {
		fprint(2, "out of memory\n");
		exits("mem");
	}
	memset(v, 0, sizeof(*v));
	memcpy(v->name, sym, sizeof(sym));
	v->link = vars[h];
	vars[h] = v;

	p = 1;
	for(;;) {
		p = fmul(p, pname());
		if(p == 0)
			break;
		w = lookup(1);
		if(w) {
			v->node = w->node;
			v->node.val = fmul(v->node.val, p);
			break;
		}
	}
	return v;
}

Prefix	prefix[] =
{
	1e-24,	L"yocto",
	1e-21,	L"zepto",
	1e-18,	L"atto",
	1e-15,	L"femto",
	1e-12,	L"pico",
	1e-9,	L"nano",
	1e-6,	L"micro",
	1e-6,	L"μ",
	1e-3,	L"milli",
	1e-2,	L"centi",
	1e-1,	L"deci",
	1e1,	L"deka",
	1e2,	L"hecta",
	1e2,	L"hecto",
	1e3,	L"kilo",
	1e6,	L"mega",
	1e6,	L"meg",
	1e9,	L"giga",
	1e12,	L"tera",
	1e15,	L"peta",
	1e18,	L"exa",
	1e21,	L"zetta",
	1e24,	L"yotta",
	0,	0
};

double
pname(void)
{
	Rune *p;
	int i, j, c;

	/*
	 * rip off normal prefixs
	 */
	for(i=0; p=prefix[i].pname; i++) {
		for(j=0; c=p[j]; j++)
			if(c != sym[j])
				goto no;
		memmove(sym, sym+j, (Nsym-j)*sizeof(*sym));
		memset(sym+(Nsym-j), 0, j*sizeof(*sym));
		return prefix[i].val;
	no:;
	}

	/*
	 * rip off 's' suffixes
	 */
	for(j=0; sym[j]; j++)
		;
	j--;
	/* j>1 is special hack to disallow ms finding m */
	if(j > 1 && sym[j] == 's') {
		sym[j] = 0;
		return 1;
	}
	return 0;
}

/*
 * careful floating point
 */
double
fmul(double a, double b)
{
	double l;

	if(a <= 0) {
		if(a == 0)
			return 0;
		l = log(-a);
	} else
		l = log(a);

	if(b <= 0) {
		if(b == 0)
			return 0;
		l += log(-b);
	} else
		l += log(b);

	if(l > Maxe) {
		yyerror("overflow in multiply");
		return 1;
	}
	if(l < -Maxe) {
		yyerror("underflow in multiply");
		return 0;
	}
	return a*b;
}

double
fdiv(double a, double b)
{
	double l;

	if(a <= 0) {
		if(a == 0)
			return 0;
		l = log(-a);
	} else
		l = log(a);

	if(b <= 0) {
		if(b == 0) {
			yyerror("division by zero");
			return 1;
		}
		l -= log(-b);
	} else
		l -= log(b);

	if(l > Maxe) {
		yyerror("overflow in divide");
		return 1;
	}
	if(l < -Maxe) {
		yyerror("underflow in divide");
		return 0;
	}
	return a/b;
}

double
fadd(double a, double b)
{
	return a + b;
}
