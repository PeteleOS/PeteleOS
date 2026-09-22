#include <stdio.h>
#include "pic.h"
#include "picy_parse.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Hand-written recursive-descent replacement for picy.y (LALR).
 *
 * Original yacc grammar (precedence low -> high):
 *	%right '='
 *	%left OROR
 *	%left ANDAND
 *	%nonassoc GT LT LE GE EQ NEQ
 *	%left '+' '-'
 *	%left '*' '/' '%'
 *	%right UMINUS NOT
 *	%right '^'
 *	top: piclist | (empty) | error { ERROR "syntax error" WARNING; }
 *	piclist: picture | piclist picture
 *	picture: prim ST | leftbrace piclist '}' | PLACENAME ':' picture
 *	| PLACENAME ':' ST picture | PLACENAME ':' position ST | asgn ST
 *	| DIR | PRINT expr ST | PRINT position ST | PRINT text ST
 *	| RESET varlist ST | copy | for | if | ST
 *	expr: NUMBER | VARNAME | asgn | expr '+'/'-'/'*'/... expr
 *	| '-' expr %prec UMINUS | '(' expr ')' | place DOTX/... etc
 *	(see picy.y for the full 328-line grammar; all actions below
 *	are copied verbatim, including mid-rule leftthing/rightthing
 *	and $<o>4 handling in prim.)
 *
 * Precedence-map (hand parser, low -> high):
 *	level 1 (right): '=' (asgn only; assignment is a statement-level
 *	  form here, parsed as parse_asgn before binary ops)
 *	level 2 (left): OROR
 *	level 3 (left): ANDAND
 *	level 4 (nonassoc): GT LT LE GE EQ NEQ
 *	level 5 (left): '+' '-'
 *	level 6 (left): '*' '/' '%'
 *	level 7 (right, prefix): UMINUS('-'/'+') NOT
 *	level 8 (highest, right): '^'
 * Call chain: parse_top -> parse_piclist -> parse_picture ->
 *	parse_prim/parse_position/parse_expr family ->
 *	parse_oror -> parse_andand -> parse_rel -> parse_add ->
 *	parse_mul -> parse_unary -> parse_pow -> parse_eprefix.
 *
 * Error-recovery-map: original had one `error' production at `top'
 * level. Hand parser calls yyerror("syntax error") (== ERROR/WARNING
 * in pic.h, defined in input.c) at the failure point, skips input to
 * the next statement terminator ST (or EOF), and continues with the
 * next picture, preserving pic's keep-going behaviour. Mid-picture
 * failures unwind to the piclist loop, which is the yyerrok
 * equivalent (yyhave is cleared when skipping).
 *
 * Lookahead: the calc_parse.c 1-token yyhave/yypeek/yyget discipline
 * is kept verbatim; one extra helper yypeek2() (2-token lookahead,
 * same buffer) disambiguates PLACENAME ':' picture vs position and
 * VARNAME '=' asgn vs plain variable, which LALR(1) resolves with
 * one token of context past the current nonterminal.
 */

YYSTYPE y;
YYSTYPE yylval, yyval;

extern int yylex(void);

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef BOX
#define BOX 1
#undef LINE
#define LINE 2
#undef ARROW
#define ARROW 3
#undef CIRCLE
#define CIRCLE 4
#undef ELLIPSE
#define ELLIPSE 5
#undef ARC
#define ARC 6
#undef SPLINE
#define SPLINE 7
#undef BLOCK
#define BLOCK 8
#undef TEXT
#define TEXT 9
#undef TROFF
#define TROFF 10
#undef MOVE
#define MOVE 11
#undef BLOCKEND
#define BLOCKEND 12
#undef PLACE
#define PLACE 13
#undef PRINT
#define PRINT 270
#undef RESET
#define RESET 271
#undef THRU
#define THRU 272
#undef UNTIL
#define UNTIL 273
#undef FOR
#define FOR 274
#undef IF
#define IF 275
#undef COPY
#define COPY 276
#undef THENSTR
#define THENSTR 277
#undef ELSESTR
#define ELSESTR 278
#undef DOSTR
#define DOSTR 279
#undef PLACENAME
#define PLACENAME 280
#undef VARNAME
#define VARNAME 281
#undef SPRINTF
#define SPRINTF 282
#undef DEFNAME
#define DEFNAME 283
#undef ATTR
#define ATTR 284
#undef TEXTATTR
#define TEXTATTR 285
#undef LEFT
#define LEFT 286
#undef RIGHT
#define RIGHT 287
#undef UP
#define UP 288
#undef DOWN
#define DOWN 289
#undef FROM
#define FROM 290
#undef TO
#define TO 291
#undef AT
#define AT 292
#undef BY
#define BY 293
#undef WITH
#define WITH 294
#undef HEAD
#define HEAD 295
#undef CW
#define CW 296
#undef CCW
#define CCW 297
#undef THEN
#define THEN 298
#undef HEIGHT
#define HEIGHT 299
#undef WIDTH
#define WIDTH 300
#undef RADIUS
#define RADIUS 301
#undef DIAMETER
#define DIAMETER 302
#undef LENGTH
#define LENGTH 303
#undef SIZE
#define SIZE 304
#undef CORNER
#define CORNER 305
#undef HERE
#define HERE 306
#undef LAST
#define LAST 307
#undef NTH
#define NTH 308
#undef SAME
#define SAME 309
#undef BETWEEN
#define BETWEEN 310
#undef AND
#define AND 311
#undef EAST
#define EAST 312
#undef WEST
#define WEST 313
#undef NORTH
#define NORTH 314
#undef SOUTH
#define SOUTH 315
#undef NE
#define NE 316
#undef NW
#define NW 317
#undef SE
#define SE 318
#undef SW
#define SW 319
#undef START
#define START 320
#undef END
#define END 321
#undef DOTX
#define DOTX 322
#undef DOTY
#define DOTY 323
#undef DOTHT
#define DOTHT 324
#undef DOTWID
#define DOTWID 325
#undef DOTRAD
#define DOTRAD 326
#undef NUMBER
#define NUMBER 327
#undef LOG
#define LOG 328
#undef EXP
#define EXP 329
#undef SIN
#define SIN 330
#undef COS
#define COS 331
#undef ATAN2
#define ATAN2 332
#undef SQRT
#define SQRT 333
#undef RAND
#define RAND 334
#undef MAX
#define MAX 335
#undef MIN
#define MIN 336
#undef INT
#define INT 337
#undef DIR
#define DIR 338
#undef DOT
#define DOT 339
#undef DASH
#define DASH 340
#undef CHOP
#define CHOP 341
#undef FILL
#define FILL 342
#undef ST
#define ST 343
#undef OROR
#define OROR 344
#undef ANDAND
#define ANDAND 345
#undef GT
#define GT 346
#undef LT
#define LT 347
#undef LE
#define LE 348
#undef GE
#define GE 349
#undef EQ
#define EQ 350
#undef NEQ
#define NEQ 351
#undef UMINUS
#define UMINUS 352
#undef NOT
#define NOT 353

/* small FIFO lookahead buffer over yylex() */
#define NLA 4
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
yypeek3(void)
{
	yyfill(2);
	return yyt[2];
}

static int
yypeek4(void)
{
	yyfill(3);
	return yyt[3];
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
	int t;

	/* skip to next statement terminator or end */
	while((t = yypeek()) != ST && t != 0 && t != EOF)
		yyget();
	if(yypeek() == ST)
		yyget();
}

static obj* parse_top(void);
static obj* parse_piclist(void);
static obj* parse_picture(void);
static int parse_varlist(void);
static double parse_asgn(void);
static int parse_copy(void);
static int parse_copylist(void);
static int parse_copyattr(void);
static int parse_for(void);
static int parse_if(void);
static double parse_ifexpr(void);
static int parse_optop(void);
static obj* parse_leftbrace(void);
static obj* parse_prim(void);
static obj* parse_lbracket(void);
static int parse_attrlist(void);
static int parse_attr(void);
static int parse_textlist(void);
static int parse_textattr(void);
static char* parse_text(void);
static int parse_exprlist(void);
static obj* parse_position(void);
static obj* parse_place(void);
static obj* parse_blockname(void);
static int parse_last(void);
static int parse_nthval(void);
static int parse_type(void);
static double parse_expr(void);
static double parse_oror(void);
static double parse_andand(void);
static double parse_rel(void);
static double parse_add(void);
static double parse_mul(void);
static double parse_unary(void);
static double parse_pow(void);
static double parse_eprefix(void);

/* can token t start an expr? */
static int
exprstart(int t)
{
	return t == NUMBER || t == VARNAME || t == '(' || t == NOT
	    || t == '-' || t == '+' || t == PLACENAME || t == LAST
	    || t == NTH || t == HERE || t == CORNER || t == LOG
	    || t == EXP || t == SIN || t == COS || t == ATAN2
	    || t == SQRT || t == RAND || t == MAX || t == MIN
	    || t == INT;
}

static obj*
parse_top(void)
{
	int t;

	t = yypeek();
	if(t == 0 || t == EOF)
		return 0;
	if(!exprstart(t) && t != BOX && t != LINE && t != ARROW
	    && t != CIRCLE && t != ELLIPSE && t != ARC
	    && t != SPLINE && t != MOVE && t != TEXT
	    && t != SPRINTF && t != TROFF && t != '['
	    && t != '{' && t != PLACENAME && t != DIR
	    && t != PRINT && t != RESET && t != COPY
	    && t != FOR && t != IF && t != ST){
		yyerror("syntax error");
		yysync();
		return 0;
	}
	return parse_piclist();
}

static obj*
parse_piclist(void)
{
	obj *o;

	o = parse_picture();
	for(;;){
		int t = yypeek();
		if(t == 0 || t == EOF || t == '}' || t == ']')
			break;
		o = parse_picture();
	}
	return o;
}

static obj*
parse_picture(void)
{
	int t;
	char *s;

	t = yypeek();
	switch(t){
	case ST:
		yyget();
		return 0;
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
		t = yypeek();
		if(t == TEXT || t == SPRINTF){
			s = parse_text();
			if(yypeek() != ST){
				yyerror("syntax error");
				yysync();
				return 0;
			}
			yyget();
			printf("%s\n", s);
			free(s);
			return makenode(PLACE, 0);
		}
		/* PRINT expr ST vs PRINT position ST: expr first; a
		 * bare position that is not an expr falls back below */
		if(exprstart(t) && !(t == PLACENAME && yypeek2() != '.'
		    && yypeek2() != CORNER)){
			double f = parse_expr();
			if(yypeek() != ST){
				yyerror("syntax error");
				yysync();
				return 0;
			}
			yyget();
			printexpr(f);
			return makenode(PLACE, 0);
		}
		{
			obj *o = parse_position();
			if(yypeek() != ST){
				yyerror("syntax error");
				yysync();
				return 0;
			}
			yyget();
			printpos(o);
			return makenode(PLACE, 0);
		}
	case RESET:
		yyget();
		parse_varlist();
		if(yypeek() != ST){
			yyerror("syntax error");
			yysync();
			return 0;
		}
		yyget();
		resetvar();
		makeiattr(0, 0);
		return makenode(PLACE, 0);
	case DIR:
		yyget();
		setdir(yylval.i);
		return makenode(PLACE, 0);
	case PLACENAME:
		{
			char *nm;
			yyget();
			nm = yylval.p;
			if(yyget() != ':'){
				yyerror("syntax error");
				yysync();
				return 0;
			}
			if(yypeek() == ST){
				obj *o;
				yyget();
				o = parse_picture();
				y.o = o;
				makevar(nm, PLACENAME, y);
				return o;
			}
			t = yypeek();
			/* picture-starters vs position: prim keywords,
			 * braces, FOR/IF/COPY/PRINT/RESET/DIR/ST and
			 * VARNAME '=' go the picture way */
			if(t == BOX || t == LINE || t == ARROW
			    || t == CIRCLE || t == ELLIPSE || t == ARC
			    || t == SPLINE || t == MOVE || t == TEXT
			    || t == SPRINTF || t == TROFF || t == '['
			    || t == '{' || t == FOR || t == IF
			    || t == COPY || t == PRINT || t == RESET
			    || t == DIR || t == ST || t == PLACENAME
			    || (t == VARNAME && yypeek2() == '=')){
				obj *o = parse_picture();
				y.o = o;
				makevar(nm, PLACENAME, y);
				return o;
			}
			{
				obj *o = parse_position();
				if(yypeek() != ST){
					yyerror("syntax error");
					yysync();
					return 0;
				}
				yyget();
				y.o = o;
				makevar(nm, PLACENAME, y);
				return o;
			}
		}
	default:
		if(t == VARNAME && yypeek2() == '='){
			double f = parse_asgn();
			if(yypeek() != ST){
				yyerror("syntax error");
				yysync();
				return 0;
			}
			yyget();
			y.f = f;
			y.o = y.o;
			y.o = makenode(PLACE, 0);
			return y.o;
		}
		if(t == BOX || t == LINE || t == ARROW
		    || t == CIRCLE || t == ELLIPSE || t == ARC
		    || t == SPLINE || t == MOVE || t == TEXT
		    || t == SPRINTF || t == TROFF || t == '['){
			obj *o = parse_prim();
			if(yypeek() == ST)
				yyget();
			codegen = 1;
			makeiattr(0, 0);
			return o;
		}
		if(t == '{'){
			obj *l, *o;
			l = parse_leftbrace();
			o = parse_piclist();
			if(yyget() != '}'){
				yyerror("syntax error");
				yysync();
				return 0;
			}
			rightthing(l, '}');
			return o;
		}
		yyerror("syntax error");
		yysync();
		return 0;
	}
}

static int
parse_varlist(void)
{
	int t;

	for(;;){
		t = yypeek();
		if(t == VARNAME){
			yyget();
			makevattr(yylval.p);
		}else if(t == ','){
			yyget();
			if(yypeek() != VARNAME){
				yyerror("syntax error");
				return -1;
			}
			yyget();
			makevattr(yylval.p);
		}else
			break;
	}
	return 0;
}

static double
parse_asgn(void)
{
	char *nm;
	double f;

	yyget();	/* VARNAME; yyget sets yylval to its value */
	nm = yylval.p;
	if(yyget() != '='){
		yyerror("syntax error");
		return 0;
	}
	f = parse_expr();
	y.f = f;
	makevar(nm, VARNAME, y);
	checkscale(nm);
	return f;
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
	while(yypeek() == TEXT || yypeek() == SPRINTF
	    || yypeek() == THRU || yypeek() == UNTIL)
		parse_copyattr();
	return 0;
}

static int
parse_copyattr(void)
{
	int t = yypeek();

	if(t == TEXT || t == SPRINTF){
		char *s = parse_text();
		copyfile(s);
	}else if(t == THRU){
		yyget();
		if(yyget() != DEFNAME){
			yyerror("syntax error");
			return -1;
		}
		copydef(yylval.st);
	}else if(t == UNTIL){
		yyget();
		{
			char *s = parse_text();
			copyuntil(s);
		}
	}else{
		yyerror("syntax error");
		return -1;
	}
	return 0;
}

static int
parse_for(void)
{
	char *nm;
	double from, to, by;
	int op;
	char *body;

	yyget();	/* FOR */
	if(yypeek() != VARNAME){
		yyerror("syntax error");
		yysync();
		return -1;
	}
	yyget();
	nm = yylval.p;
	y.f = 0;
	makevar(nm, VARNAME, y);
	t = yyget();
	if(t != FROM && t != '='){
		yyerror("syntax error");
		yysync();
		return -1;
	}
	from = parse_expr();
	if(yyget() != TO){
		yyerror("syntax error");
		yysync();
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
		yysync();
		return -1;
	}
	yyget();
	body = yylval.p;
	forloop(nm, from, to, op, by, body);
	return 0;
}

static int
parse_if(void)
{
	double c;
	char *t, *e;

	yyget();	/* IF */
	c = parse_ifexpr();
	if(yypeek() != THENSTR){
		yyerror("syntax error");
		yysync();
		return -1;
	}
	yyget();
	t = yylval.p;
	e = 0;
	if(yypeek() == ELSESTR){
		yyget();
		e = yylval.p;
	}
	ifstat(c, t, e);
	return 0;
}

static double
parse_ifexpr(void)
{
	if(yypeek() == TEXT || yypeek() == SPRINTF){
		char *a, *b;
		int t, eq;
		double v;

		a = parse_text();
		t = yypeek();
		if(t != EQ && t != NEQ){
			yyerror("syntax error");
			return 0;
		}
		eq = t;
		yyget();
		b = parse_text();
		if(eq == EQ)
			v = strcmp(a, b) == 0;
		else
			v = strcmp(a, b) != 0;
		free(a);
		free(b);
		return v;
	}
	return parse_expr();
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

static obj*
parse_leftbrace(void)
{
	yyget();	/* '{' */
	return leftthing('{');
}

static obj*
parse_lbracket(void)
{
	yyget();	/* '[' */
	return leftthing('[');
}

static obj*
parse_prim(void)
{
	int t;
	obj *o, *blk;

	t = yypeek();
	switch(t){
	case BOX:
		yyget();
		parse_attrlist();
		return boxgen();
	case CIRCLE:
	case ELLIPSE:
		yyget();
		t = yylval.i;
		parse_attrlist();
		return circgen(t);
	case ARC:
		yyget();
		t = yylval.i;
		parse_attrlist();
		return arcgen(t);
	case LINE:
	case ARROW:
	case SPLINE:
		yyget();
		t = yylval.i;
		parse_attrlist();
		return linegen(t);
	case MOVE:
		yyget();
		parse_attrlist();
		return movegen();
	case TEXT:
	case SPRINTF:
		parse_textlist();
		parse_attrlist();
		return textgen();
	case TROFF:
		yyget();
		return troffgen(yylval.p);
	case '[':
		blk = parse_lbracket();
		o = parse_piclist();
		if(yyget() != ']'){
			yyerror("syntax error");
			return 0;
		}
		rightthing(blk, ']');
		parse_attrlist();
		return blockgen(blk, o);
	default:
		yyerror("syntax error");
		return 0;
	}
}

static int
parse_attrlist(void)
{
	int t;

	for(;;){
		t = yypeek();
		if(t == ST || t == 0 || t == EOF || t == '}' || t == ']'
		    || t == BOX || t == LINE || t == ARROW
		    || t == CIRCLE || t == ELLIPSE || t == ARC
		    || t == SPLINE || t == MOVE || t == TROFF
		    || t == PRINT || t == FOR || t == IF
		    || t == COPY || t == RESET)
			break;
		if(parse_attr() < 0)
			break;
	}
	return 0;
}

static int
parse_attr(void)
{
	int t = yypeek();
	double f;

	if(t == ATTR){
		int a;
		yyget();
		a = yylval.i;
		if(exprstart(yypeek())){
			f = parse_expr();
			makefattr(a, !DEFAULT, f);
		}else
			makefattr(a, DEFAULT, 0.0);
		return 0;
	}
	if(t == DIR){
		int a;
		yyget();
		a = yylval.i;
		if(exprstart(yypeek())){
			f = parse_expr();
			makefattr(a, !DEFAULT, f);
		}else
			makefattr(a, DEFAULT, 0.0);
		return 0;
	}
	if(t == FROM || t == TO || t == AT || t == BY){
		int a;
		yyget();
		a = yylval.i;
		{
			obj *o = parse_position();
			makeoattr(a, o);
		}
		return 0;
	}
	if(t == WITH){
		yyget();
		if(yypeek() == CORNER){
			int c;
			yyget();
			c = yylval.i;
			makeiattr(WITH, c);
			return 0;
		}
		if(yypeek() == '.'){
			char *nm;
			obj *b, *p;
			yyget();
			if(yypeek() != PLACENAME){
				yyerror("syntax error");
				return -1;
			}
			yyget();
			nm = yylval.p;
			b = getblock(getlast(1, BLOCK), nm);
			if(yypeek() == CORNER){
				int c;
				yyget();
				c = yylval.i;
				p = getpos(b, c);
				makeoattr(PLACE, p);
			}else
				makeoattr(PLACE, b);
			return 0;
		}
		{
			obj *o = parse_position();
			makeoattr(PLACE, o);
			return 0;
		}
	}
	if(t == SAME){
		int v;
		yyget();
		v = yylval.i;
		makeiattr(SAME, v);
		return 0;
	}
	if(t == TEXTATTR && !exprstart(yypeek2())){
		/* lone TEXTATTR (maketattr) vs text TEXTATTR handled
		 * in parse_textattr; peek TEXTATTR followed by attr-end */
		int v;
		yyget();
		v = yylval.i;
		maketattr(v, (char*)0);
		return 0;
	}
	if(t == HEAD){
		int v;
		yyget();
		v = yylval.i;
		makeiattr(HEAD, v);
		return 0;
	}
	if(t == DOT || t == DASH || t == CHOP || t == FILL){
		int a;
		yyget();
		a = yylval.i;
		if(t == CHOP && yypeek() == PLACENAME){
			char *nm;
			yyget();
			nm = yylval.p;
			makeattr(CHOP, PLACENAME, getvar(nm));
			return 0;
		}
		if(exprstart(yypeek())){
			f = parse_expr();
			makefattr(a, !DEFAULT, f);
		}else
			makefattr(a, DEFAULT, 0.0);
		return 0;
	}
	if(t == NOEDGE){
		yyget();
		makeiattr(NOEDGE, 0);
		return 0;
	}
	if(t == TEXT || t == SPRINTF || t == TEXTATTR){
		parse_textlist();
		return 0;
	}
	/* default: expr => makefattr(curdir(), !DEFAULT, $1) */
	f = parse_expr();
	makefattr(curdir(), !DEFAULT, f);
	return 0;
}

static int
parse_textlist(void)
{
	parse_textattr();
	while(yypeek() == TEXT || yypeek() == SPRINTF
	    || yypeek() == TEXTATTR)
		parse_textattr();
	return 0;
}

static int
parse_textattr(void)
{
	int t = yypeek();

	if(t == TEXT || t == SPRINTF){
		char *s = parse_text();
		t = yypeek();
		if(t == TEXTATTR){
			int a;
			yyget();
			a = yylval.i;
			maketattr(a, s);
		}else
			maketattr(CENTER, s);
		return 0;
	}
	if(t == TEXTATTR){
		/* textattr TEXTATTR => addtattr (picy.y); the leading
		 * text was consumed by the previous iteration */
		int a;
		yyget();
		a = yylval.i;
		addtattr(a);
		return 0;
	}
	yyerror("syntax error");
	return -1;
}

static char*
parse_text(void)
{
	int t = yypeek();
	char *s;

	if(t == TEXT){
		yyget();
		return yylval.p;
	}
	if(t == SPRINTF){
		char *a;
		yyget();
		if(yyget() != '('){
			yyerror("syntax error");
			return "";
		}
		a = parse_text();
		if(yypeek() == ','){
			yyget();
			parse_exprlist();
		}
		if(yyget() != ')'){
			yyerror("syntax error");
			return a;
		}
		return sprintgen(a);
	}
	yyerror("syntax error");
	return "";
}

static int
parse_exprlist(void)
{
	double f = parse_expr();

	exprsave(f);
	while(yypeek() == ','){
		yyget();
		f = parse_expr();
		exprsave(f);
	}
	return 0;
}

static obj*
parse_position(void)
{
	obj *o;

	o = parse_place();
	for(;;){
		int t = yypeek();
		if(t != '+' && t != '-')
			break;
		yyget();
		if(yypeek() == '('){
			double a, b;
			yyget();
			a = parse_expr();
			if(yyget() != ','){
				yyerror("syntax error");
				return o;
			}
			b = parse_expr();
			if(yyget() != ')'){
				yyerror("syntax error");
				return o;
			}
			if(t == '+')
				o = fixpos(o, a, b);
			else
				o = fixpos(o, -a, -b);
		}else if(exprstart(yypeek())){
			/* position '+' place vs '+' expr ','
			 * expr: try place first when PLACENAME-ish */
			if((yypeek() == PLACENAME || yypeek() == CORNER
			    || yypeek() == HERE || yypeek() == LAST
			    || yypeek() == NTH) && yypeek2() != '.'
			    && yypeek2() != DOTX && yypeek2() != DOTY
			    && yypeek2() != DOTHT && yypeek2() != DOTWID
			    && yypeek2() != DOTRAD){
				obj *p = parse_place();
				if(t == '+')
					o = addpos(o, p);
				else
					o = subpos(o, p);
			}else{
				double a, b;
				a = parse_expr();
				if(yyget() != ','){
					yyerror("syntax error");
					return o;
				}
				b = parse_expr();
				if(t == '+')
					o = fixpos(o, a, b);
				else
					o = fixpos(o, -a, -b);
			}
		}else{
			yyerror("syntax error");
			return o;
		}
	}
	return o;
}

/* place/position bases that start with expr forms:
 * '(' position ')' | expr ',' expr | '(' place ',' place ')'
 * | expr LT/BETWEEN ... -- handled here by trial parse */
static obj*
parse_place(void)
{
	int t = yypeek();
	char *nm;
	obj *o;

	switch(t){
	case PLACENAME:
		/* bare PLACENAME, or PLACENAME '.' PLACENAME blockname.
		 * (PLACENAME '.' VARNAME is an expr form handled in
		 * parse_eprefix before parse_place is reached.) */
		yyget();
		nm = yylval.p;
		if(yypeek() == '.' && yypeek2() == PLACENAME){
			char *m2;
			yyget();
			yyget();
			m2 = yylval.p;
			y = getvar(nm);
			o = getblock(y.o, m2);
			if(yypeek() == CORNER){
				int c;
				yyget();
				c = yylval.i;
				return getpos(o, c);
			}
			return o;
		}
		y = getvar(nm);
		if(yypeek() == CORNER){
			int c;
			yyget();
			c = yylval.i;
			return getpos(y.o, c);
		}
		return y.o;
	case CORNER:
		{
			int c;
			yyget();
			c = yylval.i;
			t = yypeek();
			if(t == PLACENAME){
				yyget();
				nm = yylval.p;
				y = getvar(nm);
				return getpos(y.o, c);
			}
			if(t == LAST){
				int n = parse_last();
				int ty = parse_type();
				return getpos(getlast(n, ty), c);
			}
			if(t == NTH){
				if(yypeek2() == LAST){
					int n = parse_last();
					int ty = parse_type();
					return getpos(getlast(n, ty), c);
				}else{
					int n = parse_nthval();
					int ty = parse_type();
					return getpos(getfirst(n, ty), c);
				}
			}
			if(t == LAST || t == NTH || t == PLACENAME){
				o = parse_blockname();
				return getpos(o, c);
			}
			yyerror("syntax error");
			return gethere();
		}
	case HERE:
		yyget();
		return gethere();
	case LAST:
		{
			int n = parse_last();
			t = yypeek();
			if(t == BLOCK && yypeek2() == '.'
			    && yypeek3() == PLACENAME){
				yyget();
				yyget();
				yyget();
				nm = yylval.p;
				o = getblock(getlast(n, BLOCK), nm);
				if(yypeek() == CORNER){
					int c;
					yyget();
					c = yylval.i;
					return getpos(o, c);
				}
				return o;
			}
			{
				int ty = parse_type();
				if(yypeek() == CORNER){
					int c;
					yyget();
					c = yylval.i;
					return getpos(getlast(n, ty), c);
				}
				return getlast(n, ty);
			}
		}
	case NTH:
		if(yypeek2() == LAST){
			int n = parse_last();
			t = yypeek();
			if(t == BLOCK && yypeek2() == '.'
			    && yypeek3() == PLACENAME){
				yyget();
				yyget();
				yyget();
				nm = yylval.p;
				o = getblock(getlast(n, BLOCK), nm);
				if(yypeek() == CORNER){
					int c;
					yyget();
					c = yylval.i;
					return getpos(o, c);
				}
				return o;
			}
			{
				int ty = parse_type();
				if(yypeek() == CORNER){
					int c;
					yyget();
					c = yylval.i;
					return getpos(getlast(n, ty), c);
				}
				return getlast(n, ty);
			}
		}
		{
			int n = parse_nthval();
			t = yypeek();
			if(t == BLOCK && yypeek2() == '.'
			    && yypeek3() == PLACENAME){
				yyget();
				yyget();
				yyget();
				nm = yylval.p;
				o = getblock(getfirst(n, BLOCK), nm);
				if(yypeek() == CORNER){
					int c;
					yyget();
					c = yylval.i;
					return getpos(o, c);
				}
				return o;
			}
			{
				int ty = parse_type();
				if(yypeek() == CORNER){
					int c;
					yyget();
					c = yylval.i;
					return getpos(getfirst(n, ty), c);
				}
				return getfirst(n, ty);
			}
		}
	default:
		/* expr-based positions: expr ',' expr etc. */
		{
			double a, b;
			a = parse_expr();
			t = yypeek();
			if(t == ','){
				yyget();
				b = parse_expr();
				return makepos(a, b);
			}
			if(t == LT || t == BETWEEN){
				obj *p1, *p2;
				yyget();
				p1 = parse_position();
				if(yyget() != ','){
					yyerror("syntax error");
					return makepos(a, a);
				}
				if(t == LT && yypeek() == AND){
					/* expr BETWEEN position AND position
					 * shares the LT path; AND consumed */
					yyget();
				}else if(t == LT){
					/* expr LT position ',' position GT */
					;
				}else{
					if(yyget() != AND){
						yyerror("syntax error");
						return makepos(a, a);
					}
				}
				p2 = parse_position();
				if(t == LT){
					if(yyget() != GT){
						yyerror("syntax error");
						return makepos(a, a);
					}
				}
				return makebetween(a, p1, p2);
			}
			/* lone expr is not a place; wrap as position */
			return makepos(a, 0);
		}
	}
}

static obj*
parse_blockname(void)
{
	int t = yypeek();
	char *nm;
	obj *o;

	if(t == LAST || (t == NTH && yypeek2() == LAST)){
		/* last BLOCK '.' PLACENAME with last = LAST+|NTH LAST */
		int n = parse_last();
		int ty = parse_type();
		if(yyget() != '.'){
			yyerror("syntax error");
			return gethere();
		}
		if(yypeek() != PLACENAME){
			yyerror("syntax error");
			return gethere();
		}
		yyget();
		nm = yylval.p;
		return getblock(getlast(n, ty), nm);
	}
	if(t == NTH){
		/* NTH BLOCK '.' PLACENAME */
		int n = parse_nthval();
		int ty = parse_type();
		if(yyget() != '.'){
			yyerror("syntax error");
			return gethere();
		}
		if(yypeek() != PLACENAME){
			yyerror("syntax error");
			return gethere();
		}
		yyget();
		nm = yylval.p;
		return getblock(getfirst(n, ty), nm);
	}
	/* PLACENAME '.' PLACENAME */
	yyget();
	nm = yylval.p;
	if(yyget() != '.'){
		yyerror("syntax error");
		return gethere();
	}
	{
		char *m2;
		if(yypeek() != PLACENAME){
			yyerror("syntax error");
			return gethere();
		}
		yyget();
		m2 = yylval.p;
		y = getvar(nm);
		o = getblock(y.o, m2);
		return o;
	}
}

static int
parse_last(void)
{
	int n = 0;

	/* last: LAST+ | NTH LAST  (picy.y) */
	if(yypeek() == LAST){
		while(yypeek() == LAST){
			yyget();
			n++;
		}
		return n;
	}
	/* NTH LAST */
	yyget();
	n = yylval.i;
	if(yypeek() == LAST)
		yyget();
	else
		yyerror("syntax error");
	return n;
}

static int
parse_nthval(void)
{
	int n;

	yyget();	/* NTH; yyget sets yylval to its value */
	n = yylval.i;
	return n;
}

static int
parse_type(void)
{
	int t = yypeek();

	switch(t){
	case BOX:
	case CIRCLE:
	case ELLIPSE:
	case ARC:
	case LINE:
	case ARROW:
	case SPLINE:
	case BLOCK:
		yyget();
		return t;
	default:
		yyerror("syntax error");
		return BOX;
	}
}

static double
parse_expr(void)
{
	/* asgn has lowest precedence and is right-assoc */
	if(yypeek() == VARNAME && yypeek2() == '=')
		return parse_asgn();
	return parse_oror();
}

static double
parse_oror(void)
{
	double f = parse_andand();

	while(yypeek() == OROR){
		yyget();
		f = f || parse_andand();
	}
	return f;
}

static double
parse_andand(void)
{
	double f = parse_rel();

	while(yypeek() == ANDAND){
		yyget();
		f = f && parse_rel();
	}
	return f;
}

static double
parse_rel(void)
{
	double f = parse_add();
	int t;

	t = yypeek();
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
	case NEQ:
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
	double f = parse_eprefix();

	if(yypeek() == '^'){
		yyget();
		f = pow(f, parse_unary());
	}
	return f;
}

static double
parse_eprefix(void)
{
	int t = yypeek();
	double f;

	switch(t){
	case NUMBER:
		yyget();
		return yylval.f;
	case VARNAME:
		yyget();
		return getfval(yylval.p);
	case '(':
		yyget();
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
		return (float)rand() / 32767.0;
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
		/* PLACENAME '.' VARNAME (getblkvar) */
		if(t == PLACENAME && yypeek2() == '.'
		    && yypeek3() == VARNAME){
			char *pn, *vn;
			obj *b;
			yyget();
			pn = yylval.p;
			yyget();	/* '.' */
			yyget();
			vn = yylval.p;
			y = getvar(pn);
			b = y.o;
			return getblkvar(b, vn);
		}
		/* last/NTH BLOCK '.' VARNAME (getblkvar) */
		if(t == LAST && yypeek2() == BLOCK
		    && yypeek3() == '.' && yypeek4() == VARNAME){
			char *vn;
			obj *b;
			yyget();	/* single LAST */
			yyget();	/* BLOCK */
			yyget();	/* '.' */
			yyget();
			vn = yylval.p;
			b = getlast(1, BLOCK);
			return getblkvar(b, vn);
		}
		if(t == NTH && yypeek2() == BLOCK
		    && yypeek3() == '.' && yypeek4() == VARNAME){
			int n;
			char *vn;
			obj *b;
			yyget();
			n = yylval.i;
			yyget();	/* BLOCK */
			yyget();	/* '.' */
			yyget();
			vn = yylval.p;
			b = getfirst(n, BLOCK);
			return getblkvar(b, vn);
		}
		/* place DOTX/... | last/NTH ... */
		if(t == PLACENAME || t == CORNER || t == HERE
		    || t == LAST || t == NTH){
			obj *o = parse_place();
			t = yypeek();
			if(t == DOTX || t == DOTY || t == DOTHT
			    || t == DOTWID || t == DOTRAD){
				int c;
				yyget();
				c = yylval.i;
				return getcomp(o, c);
			}
			yyerror("syntax error");
			return 0;
		}
		yyerror("syntax error");
		return 0;
	}
}

int
yyparse(void)
{
	int t;

	yyhave = 0;
	t = yypeek();
	if(t == 0 || t == EOF)
		return 0;
	parse_top();
	if(yypeek() != 0 && yypeek() != EOF){
		yyerror("syntax error");
		yysync();
		return 1;
	}
	return 0;
}
