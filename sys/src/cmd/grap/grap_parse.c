#include <u.h>
#include <libc.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "grap.h"
#include "grap_parse.h"

/*
 * Hand-written recursive-descent replacement for grap.y (LALR).
 *
 * Original yacc grammar (precedence low -> high):
 *	%right '='
 *	%left OR
 *	%left AND
 *	%nonassoc GT LT LE GE EQ NE
 *	%left '+' '-'
 *	%left '*' '/' '%'
 *	%right UMINUS NOT
 *	%right '^'
 *	top: graphseq | (empty) | error
 *	graphseq: statlist | graph statlist | graphseq graph statlist
 *	stat: FRAME framelist | ticks | grid | label | coord | plot
 *	| line | circle | draw | next | PIC | for | if | copy
 *	| numlist | assign | PRINT expr | PRINT string | (empty)
 *	expr: NUMBER | assign | '(' string_expr ')' | VARNAME | binops...
 *	(see grap.y for the full 396-line grammar; all actions below
 *	are copied verbatim.)
 *
 * Precedence-map (hand parser, low -> high):
 *	level 1 (right): '=' (assign, right-assoc)
 *	level 2 (left): OR
 *	level 3 (left): AND
 *	level 4 (nonassoc): GT LT LE GE EQ NE
 *	level 5 (left): '+' '-'
 *	level 6 (left): '*' '/' '%'
 *	level 7 (right, prefix): UMINUS('-'/'+') NOT
 *	level 8 (highest, right): '^'
 *	if_expr/string_expr sit above expr: parse_ifexpr tries a STRING
 *	EQ/NE STRING comparison first (3-token lookahead), else expr,
 *	then left-assoc AND/OR with string_expr operands (grap.y).
 * Call chain: yyparse -> parse_stat loop -> parse_stat ->
 *	parse_* keyword builders -> parse_expr -> parse_or -> parse_and
 *	-> parse_rel -> parse_add -> parse_mul -> parse_unary ->
 *	parse_pow -> parse_eprimary.
 *
 * Error-recovery-map: original had one `error' production at `top':
 * { codegen = 0; ERROR "syntax error" WARNING; }. Hand parser calls
 * yyerror("syntax error") (input.c) at the failure point, sets
 * codegen = 0 (further output suppressed until a codegen=1 stat),
 * skips to the next ST (statement terminator) or EOF, and resumes
 * with the next statement -- the yyerrok equivalent (yyhave cleared
 * while skipping). Trailing `if (codegen && !synerr) graph(0)'
 * from the top action is preserved in yyparse.
 */

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef FRAME
#define FRAME 257
#undef TICKS
#define TICKS 258
#undef GRID
#define GRID 259
#undef LABEL
#define LABEL 260
#undef COORD
#define COORD 261
#undef LINE
#define LINE 262
#undef ARROW
#define ARROW 263
#undef CIRCLE
#define CIRCLE 264
#undef DRAW
#define DRAW 265
#undef NEW
#define NEW 266
#undef PLOT
#define PLOT 267
#undef NEXT
#define NEXT 268
#undef PIC
#define PIC 269
#undef COPY
#define COPY 270
#undef THRU
#define THRU 271
#undef UNTIL
#define UNTIL 272
#undef FOR
#define FOR 273
#undef FROM
#define FROM 274
#undef TO
#define TO 275
#undef BY
#define BY 276
#undef AT
#define AT 277
#undef WITH
#define WITH 278
#undef IF
#define IF 279
#undef GRAPH
#define GRAPH 280
#undef THEN
#define THEN 281
#undef ELSE
#define ELSE 282
#undef DOSTR
#define DOSTR 283
#undef DOT
#define DOT 284
#undef DASH
#define DASH 285
#undef INVIS
#define INVIS 286
#undef SOLID
#define SOLID 287
#undef TEXT
#define TEXT 288
#undef JUST
#define JUST 289
#undef SIZE
#define SIZE 290
#undef LOG
#define LOG 291
#undef EXP
#define EXP 292
#undef SIN
#define SIN 293
#undef COS
#define COS 294
#undef ATAN2
#define ATAN2 295
#undef SQRT
#define SQRT 296
#undef RAND
#define RAND 297
#undef MAX
#define MAX 298
#undef MIN
#define MIN 299
#undef INT
#define INT 300
#undef PRINT
#define PRINT 301
#undef SPRINTF
#define SPRINTF 302
#undef X
#define X 303
#undef Y
#define Y 304
#undef SIDE
#define SIDE 305
#undef IN
#define IN 306
#undef OUT
#define OUT 307
#undef OFF
#define OFF 308
#undef UP
#define UP 309
#undef DOWN
#define DOWN 310
#undef ACROSS
#define ACROSS 311
#undef HEIGHT
#define HEIGHT 312
#undef WIDTH
#define WIDTH 313
#undef RADIUS
#define RADIUS 314
#undef NUMBER
#define NUMBER 315
#undef NAME
#define NAME 316
#undef VARNAME
#define VARNAME 317
#undef DEFNAME
#define DEFNAME 318
#undef STRING
#define STRING 319
#undef ST
#define ST 320
#undef OR
#define OR 321
#undef AND
#define AND 322
#undef GT
#define GT 323
#undef LT
#define LT 324
#undef LE
#define LE 325
#undef GE
#define GE 326
#undef EQ
#define EQ 327
#undef NE
#define NE 328
#undef NOT
#define NOT 329
#undef UMINUS
#define UMINUS 330

YYSTYPE yylval, yyval;

extern int yylex(void);

#define NLA 5
static int yyhave;
static YYSTYPE yyb[NLA];
static int yyt[NLA];

static void
yyfill(int n)
{
	while(yyhave <= n){
		yyt[yyhave] = yylex();
		yyb[yyhave] = yylval;
		yyhave++;
	}
}

static int
yypeek(void)
{
	yyfill(0);
	return yyt[0];
}

static int
yypeek2(void)
{
	yyfill(1);
	return yyt[1];
}

static int
yyget(void)
{
	int t, i;

	yyfill(0);
	t = yyt[0];
	yylval = yyb[0];
	for(i = 1; i < yyhave; i++){
		yyt[i-1] = yyt[i];
		yyb[i-1] = yyb[i];
	}
	yyhave--;
	return t;
}

static void
yysync(void)
{
	while(yypeek() != ST && yypeek() != 0 && yypeek() != EOF)
		yyget();
	if(yypeek() == ST)
		yyget();
}

static int parse_stat(void);
static int parse_numlist(void);
static double parse_number(void);
static int parse_label(void);
static int parse_lablist(void);
static int parse_labattr(void);
static int parse_framelist(void);
static int parse_frameitem(void);
static int parse_side(void);
static int parse_optside(void);
static Attr* parse_linedesc(void);
static int parse_linetype(void);
static Attr* parse_optdesc(void);
static int parse_ticks(void);
static int parse_tickdesc(void);
static int parse_tickattr(void);
static int parse_ticklist(void);
static int parse_tickpoint(void);
static Obj* parse_iterator(void);
static int parse_optop(void);
static char* parse_optstring(void);
static int parse_grid(void);
static int parse_griddesc(void);
static int parse_gridattr(void);
static int parse_line(void);
static int parse_circle(void);
static Attr* parse_stringlist(void);
static Attr* parse_string(void);
static Attr* parse_exprlist(void);
static Attr* parse_sattrlist(void);
static int parse_stringattr(void);
static int parse_coord(void);
static int parse_coordlist(void);
static int parse_coorditem(void);
static int parse_coordlog(void);
static int parse_plot(void);
static int parse_draw(void);
static int parse_drawtype(void);
static int parse_next(void);
static int parse_copy(void);
static int parse_copylist(void);
static int parse_copyattr(void);
static int parse_for(void);
static char* parse_if(void);
static double parse_ifexpr(void);
static double parse_stringexpr(void);
static Point parse_point(void);
static int parse_comma(void);
static Obj* parse_optname(void);
static double parse_expr(void);
static double parse_or(void);
static double parse_and(void);
static double parse_rel(void);
static double parse_add(void);
static double parse_mul(void);
static double parse_unary(void);
static double parse_pow(void);
static double parse_eprimary(void);
static double parse_assign(void);
static Obj* parse_name(void);
static double parse_optexpr(void);

static int
exprstart(int t)
{
	return t == NUMBER || t == VARNAME || t == NAME || t == '('
	    || t == '-' || t == '+' || t == LOG || t == EXP
	    || t == SIN || t == COS || t == ATAN2 || t == SQRT
	    || t == RAND || t == MAX || t == MIN || t == INT
	    || t == NOT;
}

int
yyparse(void)
{
	int t;

	yyhave = 0;
	if(yypeek() == 0 || yypeek() == EOF){
		codegen = 0;
		return 0;
	}
	while((t = yypeek()) != 0 && t != EOF){
		if(t == GRAPH){
			char *s;
			yyget();
			s = yylval.p;
			graph(s);
			endstat();
			continue;
		}
		if(t == ST){
			yyget();
			continue;
		}
		if(parse_stat() < 0){
			codegen = 0;
			yyerror("syntax error");
			yysync();
			continue;
		}
		if(yypeek() == ST){
			yyget();
			endstat();
		}else if(yypeek() != 0 && yypeek() != EOF
		    && yypeek() != GRAPH){
			codegen = 0;
			yyerror("syntax error");
			yysync();
		}
	}
	if(codegen && !synerr)
		graph((char*)0);
	return 0;
}

static int
parse_stat(void)
{
	int t = yypeek();

	switch(t){
	case FRAME:
		yyget();
		parse_framelist();
		codegen = 1;
		return 0;
	case TICKS:
		parse_ticks();
		codegen = 1;
		return 0;
	case GRID:
		parse_grid();
		codegen = 1;
		return 0;
	case LABEL:
		parse_label();
		codegen = 1;
		return 0;
	case COORD:
		parse_coord();
		return 0;
	case PLOT:
	case STRING:
	case SPRINTF:
		parse_plot();
		codegen = 1;
		return 0;
	case LINE:
		parse_line();
		codegen = 1;
		return 0;
	case CIRCLE:
		parse_circle();
		codegen = 1;
		return 0;
	case DRAW:
	case NEW:
		parse_draw();
		return 0;
	case NEXT:
		parse_next();
		codegen = 1;
		return 0;
	case PIC:
		{
			char *s;
			yyget();
			s = yylval.p;
			codegen = 1;
			pic(s);
			return 0;
		}
	case FOR:
		parse_for();
		return 0;
	case IF:
		parse_if();
		return 0;
	case COPY:
		parse_copy();
		return 0;
	case PRINT:
		yyget();
		if(yypeek() == STRING || yypeek() == SPRINTF){
		Attr *a = parse_string();
		fprintf(stderr, "\t%s\n", a->sval);
		freeattr(a);
			freeattr(a);
		}else{
			double f = parse_expr();
			fprintf(stderr, "\t%g\n", f);
		}
		return 0;
	default:
		if(t == NUMBER || (t == '-' && yypeek2() == NUMBER)
		    || (t == '+' && yypeek2() == NUMBER)){
			parse_numlist();
			codegen = 1;
			numlist();
			return 0;
		}
		if((t == NAME || t == VARNAME) && yypeek2() == '='){
			parse_assign();
			return 0;
		}
		if(t == ST || t == 0 || t == EOF || t == GRAPH)
			return 0;
		yyerror("syntax error");
		return -1;
	}
}

static int
parse_numlist(void)
{
	double f;
	int n;

	f = parse_number();
	savenum(0, f);
	n = 1;
	while(yypeek() == NUMBER || yypeek() == ','
	    || ((yypeek() == '-' || yypeek() == '+')
		&& yypeek2() == NUMBER)){
		if(yypeek() == ',')
			yyget();
		f = parse_number();
		savenum(n, f);
		n++;
	}
	return n;
}

static double
parse_number(void)
{
	int t = yypeek();
	double f;

	if(t == '-'){
		yyget();
		if(yyget() != NUMBER){
			yyerror("syntax error");
			return 0;
		}
		f = yylval.f;
		return -f;
	}
	if(t == '+'){
		yyget();
		if(yyget() != NUMBER){
			yyerror("syntax error");
			return 0;
		}
		f = yylval.f;
		return f;
	}
	yyget();
	return yylval.f;
}

static int
parse_label(void)
{
	int s;
	Attr *a;

	yyget();	/* LABEL */
	s = parse_optside();
	a = parse_stringlist();
	parse_lablist();
	label(s, a);
	return 0;
}

static int
parse_lablist(void)
{
	while(yypeek() == UP || yypeek() == DOWN
	    || yypeek() == SIDE || yypeek() == WIDTH)
		parse_labattr();
	return 0;
}

static int
parse_labattr(void)
{
	int t = yypeek();
	double f;

	if(t == UP || t == DOWN || t == SIDE){
		int v;
		yyget();
		v = yylval.i;
		f = parse_expr();
		labelmove(v, f);
		return 0;
	}
	yyget();	/* WIDTH */
	f = parse_expr();
	labelwid(f);
	return 0;
}

static int
parse_framelist(void)
{
	while(yypeek() == HEIGHT || yypeek() == WIDTH
	    || yypeek() == SIDE || yypeek() == DOT
	    || yypeek() == DASH || yypeek() == SOLID
	    || yypeek() == INVIS)
		parse_frameitem();
	return 0;
}

static int
parse_frameitem(void)
{
	int t = yypeek();

	if(t == HEIGHT){
		double f;
		yyget();
		f = parse_expr();
		frameht(f);
		return 0;
	}
	if(t == WIDTH){
		double f;
		yyget();
		f = parse_expr();
		framewid(f);
		return 0;
	}
	if(t == SIDE && (yypeek2() == DOT || yypeek2() == DASH
	    || yypeek2() == SOLID || yypeek2() == INVIS)){
		int s = parse_side();
		Attr *a = parse_linedesc();
		frameside(s, a);
		return 0;
	}
	{
		Attr *a = parse_linedesc();
		frameside(0, a);
		return 0;
	}
}

static int
parse_side(void)
{
	int v;
	yyget();	/* SIDE */
	v = yylval.i;
	return v;
}

static int
parse_optside(void)
{
	if(yypeek() == SIDE)
		return parse_side();
	return 0;
}

static Attr*
parse_linedesc(void)
{
	int t = parse_linetype();
	double f = parse_optexpr();
	return makeattr(t, f, (char*)0, 0, 0);
}

static int
parse_linetype(void)
{
	int t = yypeek();

	if(t == DOT || t == DASH || t == SOLID || t == INVIS){
		int v;
		yyget();
		v = yylval.i;
		return v;
	}
	yyerror("syntax error");
	return 0;
}

static Attr*
parse_optdesc(void)
{
	if(yypeek() == DOT || yypeek() == DASH
	    || yypeek() == SOLID || yypeek() == INVIS)
		return parse_linedesc();
	return makeattr(0, 0.0, (char*)0, 0, 0);
}

static int
parse_ticks(void)
{
	yyget();	/* TICKS */
	parse_tickdesc();
	ticks();
	return 0;
}

static int
parse_tickdesc(void)
{
	parse_tickattr();
	while(yypeek() == SIDE || yypeek() == IN || yypeek() == OUT
	    || yypeek() == AT || yypeek() == FROM || yypeek() == NAME
	    || yypeek() == OFF || yypeek() == UP || yypeek() == DOWN
	    || yypeek() == WIDTH)
		parse_tickattr();
	return 0;
}

static int
parse_tickattr(void)
{
	int t = yypeek();

	if(t == SIDE && yypeek2() != OFF){
		int s = parse_side();
		tickside(s);
		return 0;
	}
	if(t == IN || t == OUT){
		int v;
		double f;
		int has;
		yyget();
		v = yylval.i;
		if(exprstart(yypeek())){
			f = parse_expr();
			has = 1;
		}else{
			f = 0.0;
			has = 0;
		}
		tickdir(v, f, has);
		return 0;
	}
	if(t == AT || t == FROM){
		if(t == AT){
			Obj *o;
			yyget();
			o = parse_optname();
			parse_ticklist();
			setlist();
			ticklist(o, AT);
			return 0;
		}
		{
			Obj *o = parse_iterator();
			setlist();
			ticklist(o, AT);
			return 0;
		}
	}
	if(t == NAME || t == VARNAME){
		/* iterator starting with optname? only FROM starts
		 * iterator; a bare NAME here is an error */
		yyerror("syntax error");
		return -1;
	}
	if(t == SIDE || t == OFF){
		if(t == SIDE){
			int s = parse_side();
			if(yypeek() != OFF){
				yyerror("syntax error");
				return -1;
			}
			yyget();
			tickoff(s);
			return 0;
		}
		yyget();
		tickoff(LEFT|RIGHT|TOP|BOT);
		return 0;
	}
	parse_labattr();
	return 0;
}

static int
parse_ticklist(void)
{
	parse_tickpoint();
	while(yypeek() == ','){
		yyget();
		parse_tickpoint();
	}
	return 0;
}

static int
parse_tickpoint(void)
{
	double f = parse_expr();

	if(yypeek() == STRING || yypeek() == SPRINTF){
		Attr *a = parse_string();
		savetick(f, a->sval);
	}else
		savetick(f, (char*)0);
	return 0;
}

static Obj*
parse_iterator(void)
{
	Obj *o1, *o2;
	double f1, f2, by;
	int op;
	char *s;

	yyget();	/* FROM */
	o1 = parse_optname();
	f1 = parse_expr();
	if(yyget() != TO){
		yyerror("syntax error");
		return 0;
	}
	o2 = parse_optname();
	f2 = parse_expr();
	op = '+';
	by = 1.0;
	if(yypeek() == BY){
		yyget();
		op = parse_optop();
		by = parse_expr();
	}
	s = parse_optstring();
	iterator(f1, f2, op, by, s);
	return o1;
}

static int
parse_optop(void)
{
	int t = yypeek();

	if(t == '+' || t == '-' || t == '*' || t == '/'){
		yyget();
		return t;
	}
	return ' ';
}

static char*
parse_optstring(void)
{
	if(yypeek() == STRING || yypeek() == SPRINTF){
		Attr *a = parse_string();
		return a->sval;
	}
	return (char*)0;
}

static int
parse_grid(void)
{
	yyget();	/* GRID */
	parse_griddesc();
	ticks();
	return 0;
}

static int
parse_griddesc(void)
{
	parse_gridattr();
	while(yypeek() == SIDE || yypeek() == X || yypeek() == Y
	    || yypeek() == DOT || yypeek() == DASH
	    || yypeek() == SOLID || yypeek() == INVIS
	    || yypeek() == AT || yypeek() == FROM
	    || yypeek() == TICKS || yypeek() == OFF
	    || yypeek() == UP || yypeek() == DOWN
	    || yypeek() == WIDTH)
		parse_gridattr();
	return 0;
}

static int
parse_gridattr(void)
{
	int t = yypeek();

	if(t == SIDE){
		int s = parse_side();
		tickside(s);
		return 0;
	}
	if(t == X){
		yyget();
		tickside(BOT);
		return 0;
	}
	if(t == Y){
		yyget();
		tickside(LEFT);
		return 0;
	}
	if(t == DOT || t == DASH || t == SOLID || t == INVIS){
		Attr *a = parse_linedesc();
		griddesc(a);
		return 0;
	}
	if(t == AT){
		Obj *o;
		yyget();
		o = parse_optname();
		parse_ticklist();
		setlist();
		gridlist(o);
		return 0;
	}
	if(t == FROM){
		Obj *o = parse_iterator();
		setlist();
		gridlist(o);
		return 0;
	}
	if(t == TICKS){
		yyget();
		if(yypeek() != OFF){
			yyerror("syntax error");
			return -1;
		}
		yyget();
		gridtickoff();
		return 0;
	}
	if(t == OFF){
		yyget();
		gridtickoff();
		return 0;
	}
	parse_labattr();
	return 0;
}

static int
parse_line(void)
{
	int ty;
	Point p1, p2;
	Attr *a;

	yyget();	/* LINE; yylval.i is LINE or ARROW */
	ty = yylval.i;
	if(yypeek() == FROM){
		yyget();
		p1 = parse_point();
		if(yyget() != TO){
			yyerror("syntax error");
			return -1;
		}
		p2 = parse_point();
		a = parse_optdesc();
		line(ty, p1, p2, a);
		return 0;
	}
	a = parse_optdesc();
	if(yyget() != FROM){
		yyerror("syntax error");
		return -1;
	}
	p1 = parse_point();
	if(yyget() != TO){
		yyerror("syntax error");
		return -1;
	}
	p2 = parse_point();
	line(ty, p1, p2, a);
	return 0;
}

static int
parse_circle(void)
{
	double r;
	Point p;

	yyget();	/* CIRCLE */
	if(yypeek() == RADIUS){
		yyget();
		r = parse_expr();
		if(yyget() != AT){
			yyerror("syntax error");
			return -1;
		}
		p = parse_point();
		circle(r, p);
		return 0;
	}
	if(yyget() != AT){
		yyerror("syntax error");
		return -1;
	}
	p = parse_point();
	if(yypeek() == RADIUS){
		yyget();
		r = parse_expr();
		circle(r, p);
		return 0;
	}
	circle(0.0, p);
	return 0;
}

static Attr*
parse_stringlist(void)
{
	Attr *a = parse_string();

	while(yypeek() == STRING || yypeek() == SPRINTF){
		Attr *b = parse_string();
		a = addattr(a, b);
	}
	return a;
}

static Attr*
parse_string(void)
{
	int t = yypeek();
	Attr *a;

	if(t == STRING){
		char *s;
		yyget();
		s = yylval.p;
		a = makesattr(s);
		parse_sattrlist();
		return a;
	}
	/* SPRINTF '(' STRING ')' sattrlist |
	 * SPRINTF '(' STRING ',' exprlist ')' sattrlist */
	{
		char *s;
		Attr *e = 0;
		yyget();	/* SPRINTF */
		if(yyget() != '('){
			yyerror("syntax error");
			return makesattr("");
		}
		if(yypeek() != STRING){
			yyerror("syntax error");
			return makesattr("");
		}
		yyget();
		s = yylval.p;
		if(yypeek() == ','){
			yyget();
			e = parse_exprlist();
		}
		if(yyget() != ')'){
			yyerror("syntax error");
			return makesattr(s);
		}
		if(e)
			a = makesattr(sprntf(s, e));
		else
			a = makesattr(sprntf(s, (Attr*)0));
		parse_sattrlist();
		return a;
	}
}

static Attr*
parse_exprlist(void)
{
	Attr *a;
	double f = parse_expr();

	a = makefattr(NUMBER, f);
	while(yypeek() == ','){
		Attr *b;
		yyget();
		f = parse_expr();
		b = makefattr(NUMBER, f);
		a = addattr(a, b);
	}
	return a;
}

static Attr*
parse_sattrlist(void)
{
	/* stringattr acts by side effect (setjust/setsize); the
	 * list value itself is unused by makesattr (grap.y). */
	while(yypeek() == JUST || yypeek() == SIZE)
		parse_stringattr();
	return 0;
}

static int
parse_stringattr(void)
{
	int t = yypeek();

	if(t == JUST){
		int v;
		yyget();
		v = yylval.i;
		setjust(v);
		return 0;
	}
	{
		int op;
		double f;
		yyget();	/* SIZE */
		op = parse_optop();
		f = parse_expr();
		setsize(op, f);
		return 0;
	}
}

static int
parse_coord(void)
{
	Obj *o;

	yyget();	/* COORD */
	o = parse_optname();
	if(yypeek() == X || yypeek() == Y || yypeek() == LOG){
		parse_coordlist();
		coord(o);
		return 0;
	}
	resetcoord(o);
	return 0;
}

static int
parse_coordlist(void)
{
	parse_coorditem();
	while(yypeek() == X || yypeek() == Y || yypeek() == LOG)
		parse_coorditem();
	return 0;
}

static int
parse_coorditem(void)
{
	int t = yypeek();

	if(t == LOG){
		int v = parse_coordlog();
		coordlog(v);
		return 0;
	}
	if(t == X || t == Y){
		int xy = t;
		Obj *o;
		double f1, f2;
		yyget();
		if(yypeek() == FROM){
			yyget();
			o = parse_optname();
			f1 = parse_expr();
			if(yyget() != TO){
				yyerror("syntax error");
				return -1;
			}
			f2 = parse_expr();
			if(xy == X)
				coord_x(makepoint(o, f1, f2));
			else
				coord_y(makepoint(o, f1, f2));
			return 0;
		}
		if(yypeek() == '('){
			/* X point / Y point, point in paren form */
			Point pt = parse_point();
			if(xy == X)
				coord_x(pt);
			else
				coord_y(pt);
			return 0;
		}
		/* X point | Y point | X optname expr TO expr ... */
		o = parse_optname();
		f1 = parse_expr();
		if(yypeek() == TO){
			yyget();
			f2 = parse_expr();
			if(xy == X)
				coord_x(makepoint(o, f1, f2));
			else
				coord_y(makepoint(o, f1, f2));
			return 0;
		}
		if(yypeek() != ','){
			yyerror("syntax error");
			return -1;
		}
		yyget();
		f2 = parse_expr();
		/* rebuild point: point was optname f1 , f2 */
		if(xy == X)
			coord_x(makepoint(o, f1, f2));
		else
			coord_y(makepoint(o, f1, f2));
		return 0;
	}
	yyerror("syntax error");
	return -1;
}

static int
parse_coordlog(void)
{
	int t;

	yyget();	/* LOG */
	t = yypeek();
	if(t == X){
		yyget();
		if(yypeek() == LOG){
			yyget();
			if(yypeek() != Y){
				yyerror("syntax error");
				return XFLAG;
			}
			yyget();
			return XFLAG|YFLAG;
		}
		return XFLAG;
	}
	if(t == Y){
		yyget();
		if(yypeek() == LOG){
			yyget();
			if(yypeek() != X){
				yyerror("syntax error");
				return YFLAG;
			}
			yyget();
			return XFLAG|YFLAG;
		}
		return YFLAG;
	}
	if(t == LOG){
		yyget();
		return XFLAG|YFLAG;
	}
	yyerror("syntax error");
	return 0;
}

static int
parse_plot(void)
{
	if(yypeek() == PLOT){
		yyget();
		if(yypeek() == STRING || yypeek() == SPRINTF){
			Attr *a = parse_stringlist();
			if(yyget() != AT){
				yyerror("syntax error");
				return -1;
			}
			{
				Point p = parse_point();
				plot(a, p);
				return 0;
			}
		}
		{
			double f = parse_expr();
			char *s = parse_optstring();
			if(yyget() != AT){
				yyerror("syntax error");
				return -1;
			}
			{
				Point p = parse_point();
				plotnum(f, s, p);
				return 0;
			}
		}
	}
	{
		Attr *a = parse_stringlist();
		if(yyget() != AT){
			yyerror("syntax error");
			return -1;
		}
		{
			Point p = parse_point();
			plot(a, p);
			return 0;
		}
	}
}

static int
parse_draw(void)
{
	int ty = parse_drawtype();
	Obj *o = parse_optname();

	if(yypeek() == STRING || yypeek() == SPRINTF){
		Attr *str = parse_string();
		Attr *a = parse_optdesc();
		drawdesc(ty, o, a, str->sval);
		return 0;
	}
	{
		Attr *a = parse_optdesc();
		if(yypeek() == STRING || yypeek() == SPRINTF){
			Attr *str = parse_string();
			drawdesc(ty, o, a, str->sval);
			return 0;
		}
		drawdesc(ty, o, a, (char*)0);
		return 0;
	}
}

static int
parse_drawtype(void)
{
	int v;

	yyget();
	v = yylval.i;
	return v;
}

static int
parse_next(void)
{
	Obj *o;
	Point p;
	Attr *a;

	yyget();	/* NEXT */
	o = parse_optname();
	if(yyget() != AT){
		yyerror("syntax error");
		return -1;
	}
	p = parse_point();
	a = parse_optdesc();
	next(o, p, a);
	return 0;
}

static int
parse_copy(void)
{
	yyget();	/* COPY */
	parse_copylist();
	copy();
	return 0;
}

static int
parse_copylist(void)
{
	parse_copyattr();
	while(yypeek() == STRING || yypeek() == SPRINTF
	    || yypeek() == THRU || yypeek() == UNTIL)
		parse_copyattr();
	return 0;
}

static int
parse_copyattr(void)
{
	int t = yypeek();

	if(t == STRING || t == SPRINTF){
		Attr *a = parse_string();
		copyfile(a->sval);
		return 0;
	}
	if(t == THRU){
		Obj *o;
		yyget();
		if(yypeek() != DEFNAME){
			yyerror("syntax error");
			return -1;
		}
		yyget();
		o = yylval.op;
		copydef(o);
		return 0;
	}
	{
		Attr *a;
		yyget();	/* UNTIL */
		a = parse_string();
		copyuntil(a->sval);
		return 0;
	}
}

static int
parse_for(void)
{
	Obj *nm;
	double from, to, by;
	int op;
	char *body;

	yyget();	/* FOR */
	nm = parse_name();
	{
		int t = yyget();
		if(t != FROM && t != '='){
			yyerror("syntax error");
			return -1;
		}
	}
	from = parse_expr();
	if(yyget() != TO){
		yyerror("syntax error");
		return -1;
	}
	to = parse_expr();
	op = '+';
	by = 1.0;
	if(yypeek() == BY){
		yyget();
		op = parse_optop();
		by = parse_expr();
	}
	if(yypeek() != DOSTR){
		yyerror("syntax error");
		return -1;
	}
	yyget();
	body = yylval.p;
	forloop(nm, from, to, op, by, body);
	return 0;
}

static char*
parse_if(void)
{
	double c;
	char *t, *e;

	yyget();	/* IF */
	c = parse_ifexpr();
	if(yypeek() != THEN){
		yyerror("syntax error");
		return 0;
	}
	yyget();
	t = yylval.p;
	e = 0;
	if(yypeek() == ELSE){
		yyget();
		e = yylval.p;
	}
	return ifstat(c, t, e);
}

static double
parse_ifexpr(void)
{
	double f, g;
	int op;

	if((yypeek() == STRING && yypeek2() == EQ)
	    || (yypeek() == STRING && yypeek2() == NE))
		f = parse_stringexpr();
	else
		f = parse_expr();
	while(yypeek() == AND || yypeek() == OR){
		op = yypeek();
		yyget();
		if(yypeek() == STRING)
			g = parse_stringexpr();
		else
			g = parse_expr();
		if(op == AND)
			f = f && g;
		else
			f = f || g;
	}
	return f;
}

static double
parse_stringexpr(void)
{
	char *a, *b;
	int op;

	yyget();
	a = yylval.p;
	op = yypeek();
	if(op != EQ && op != NE){
		yyerror("syntax error");
		return 0;
	}
	yyget();
	if(yypeek() != STRING){
		yyerror("syntax error");
		return 0;
	}
	yyget();
	b = yylval.p;
	if(op == EQ){
		double v = strcmp(a, b) == 0;
		free(a);
		free(b);
		return v;
	}
	{
		double v = strcmp(a, b) != 0;
		free(a);
		free(b);
		return v;
	}
}

static Point
parse_point(void)
{
	Obj *o = parse_optname();

	if(yypeek() == '('){
		double f1, f2;
		Point p;
		yyget();
		f1 = parse_expr();
		parse_comma();
		f2 = parse_expr();
		if(yyget() != ')'){
			yyerror("syntax error");
			return makepoint(o, f1, f2);
		}
		p = makepoint(o, f1, f2);
		return p;
	}
	{
		double f1, f2;
		Point p;
		f1 = parse_expr();
		parse_comma();
		f2 = parse_expr();
		p = makepoint(o, f1, f2);
		return p;
	}
}

static int
parse_comma(void)
{
	if(yyget() != ','){
		yyerror("syntax error");
		return -1;
	}
	return ',';
}

static Obj*
parse_optname(void)
{
	if(yypeek() == NAME){
		Obj *o;
		yyget();
		o = yylval.op;
		return o;
	}
	return lookup(curr_coord, 1);
}

static double
parse_expr(void)
{
	if((yypeek() == NAME || yypeek() == VARNAME)
	    && yypeek2() == '=')
		return parse_assign();
	return parse_or();
}

static double
parse_or(void)
{
	double f = parse_and();

	while(yypeek() == OR){
		yyget();
		f = f || parse_and();
	}
	return f;
}

static double
parse_and(void)
{
	double f = parse_rel();

	while(yypeek() == AND){
		yyget();
		f = f && parse_rel();
	}
	return f;
}

static double
parse_rel(void)
{
	double f = parse_add();
	int t = yypeek();

	switch(t){
	case GT:
		yyget();
		return f > parse_add();
	case LT:
		yyget();
		return f < parse_add();
	case LE:
		yyget();
		return f <= parse_add();
	case GE:
		yyget();
		return f >= parse_add();
	case EQ:
		yyget();
		return f == parse_add();
	case NE:
		yyget();
		return f != parse_add();
	default:
		return f;
	}
}

static double
parse_add(void)
{
	double f = parse_mul();
	int t;

	for(;;){
		t = yypeek();
		if(t == '+'){
			yyget();
			f += parse_mul();
		}else if(t == '-'){
			yyget();
			f -= parse_mul();
		}else
			break;
	}
	return f;
}

static double
parse_mul(void)
{
	double f = parse_unary();
	int t;

	for(;;){
		t = yypeek();
		if(t == '*'){
			yyget();
			f *= parse_unary();
		}else if(t == '/'){
			double d;
			yyget();
			d = parse_unary();
			if(d == 0.0){
				yyerror("division by 0");
				d = 1;
			}
			f /= d;
		}else if(t == '%'){
			long d;
			yyget();
			d = (long)parse_unary();
			if(d == 0){
				yyerror("mod division by 0");
				d = 1;
			}
			f = (long)f % d;
		}else
			break;
	}
	return f;
}

static double
parse_unary(void)
{
	int t = yypeek();

	if(t == '-'){
		yyget();
		return -parse_unary();
	}
	if(t == '+'){
		yyget();
		return parse_unary();
	}
	if(t == NOT){
		yyget();
		return !parse_unary();
	}
	return parse_pow();
}

static double
parse_pow(void)
{
	double f = parse_eprimary();

	if(yypeek() == '^'){
		yyget();
		f = pow(f, parse_unary());
	}
	return f;
}

static double
parse_eprimary(void)
{
	int t = yypeek();
	double f;

	switch(t){
	case NUMBER:
		yyget();
		return yylval.f;
	case NAME:
	case VARNAME:
		{
			Obj *o;
			yyget();
			o = yylval.op;
			return getvar(o);
		}
	case '(':
		yyget();
		if(yypeek() == STRING){
			/* '(' string_expr ')' */
			f = parse_stringexpr();
			if(yyget() != ')'){
				yyerror("syntax error");
				return f;
			}
			return f;
		}
		f = parse_expr();
		if(yyget() != ')'){
			yyerror("syntax error");
			return f;
		}
		return f;
	case LOG:
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			return 0;
		}
		f = parse_expr();
		if(yyget() != ')'){
			yyerror("syntax error");
			return Log10(f);
		}
		return Log10(f);
	case EXP:
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			return 0;
		}
		f = parse_expr();
		if(yyget() != ')'){
			yyerror("syntax error");
			return Exp(f * log(10.0));
		}
		return Exp(f * log(10.0));
	case SIN:
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			return 0;
		}
		f = parse_expr();
		if(yyget() != ')'){
			yyerror("syntax error");
			return sin(f);
		}
		return sin(f);
	case COS:
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			return 0;
		}
		f = parse_expr();
		if(yyget() != ')'){
			yyerror("syntax error");
			return cos(f);
		}
		return cos(f);
	case ATAN2:
		{
			double a, b;
			yyget();
			if(yyget() != '('){
				yyerror("syntax error");
				return 0;
			}
			a = parse_expr();
			if(yyget() != ','){
				yyerror("syntax error");
				return a;
			}
			b = parse_expr();
			if(yyget() != ')'){
				yyerror("syntax error");
				return atan2(a, b);
			}
			return atan2(a, b);
		}
	case SQRT:
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			return 0;
		}
		f = parse_expr();
		if(yyget() != ')'){
			yyerror("syntax error");
			return Sqrt(f);
		}
		return Sqrt(f);
	case RAND:
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			return 0;
		}
		if(yyget() != ')'){
			yyerror("syntax error");
			return 0;
		}
		return (double)rand() / (double)RAND_MAX;
	case MAX:
	case MIN:
		{
			double a, b;
			int op = t;
			yyget();
			if(yyget() != '('){
				yyerror("syntax error");
				return 0;
			}
			a = parse_expr();
			if(yyget() != ','){
				yyerror("syntax error");
				return a;
			}
			b = parse_expr();
			if(yyget() != ')'){
				yyerror("syntax error");
				return a >= b ? a : b;
			}
			if(op == MAX)
				return a >= b ? a : b;
			return a <= b ? a : b;
		}
	case INT:
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			return 0;
		}
		f = parse_expr();
		if(yyget() != ')'){
			yyerror("syntax error");
			return (long)f;
		}
		return (long)f;
	default:
		yyerror("syntax error");
		return 0;
	}
}

static double
parse_assign(void)
{
	Obj *o = parse_name();

	if(yyget() != '='){
		yyerror("syntax error");
		return 0;
	}
	{
		double f = parse_expr();
		return setvar(o, f);
	}
}

static Obj*
parse_name(void)
{
	Obj *o;

	yyget();	/* NAME or VARNAME; yyget sets yylval */
	o = yylval.op;
	return o;
}

static double
parse_optexpr(void)
{
	if(exprstart(yypeek()))
		return parse_expr();
	return 0.0;
}
