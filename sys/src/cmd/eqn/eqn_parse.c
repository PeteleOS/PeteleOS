#include "e.h"
#include "eqn_parse.h"

/*
 * Hand-written recursive-descent replacement for eqn.y (LALR).
 *
 * Original yacc grammar (precedence low -> high):
 *	%right	FROM TO
 *	%left	OVER SQRT
 *	%right	SUP SUB
 *	%right	SIZE FONT ROMAN ITALIC BOLD FAT
 *	%right	UP DOWN BACK FWD
 *	%left	LEFT RIGHT
 *	%right	DOT DOTDOT HAT TILDE BAR LOWBAR HIGHBAR UNDER VEC DYAD UTILDE
 *	stuff: eqn { putout($1); }
 *	| error { ERROR "syntax error" WARNING; }
 *	| { eqnreg = 0; }
 *	eqn: box | eqn box { eqnbox($1, $2, 0); }
 *	| eqn lineupbox { eqnbox($1, $2, 1); } | LINEUP { lineup(0); }
 *	box: '{' eqn '}' | QTEXT | CONTIG | SPACE | THIN | TAB
 *	| SUM | PROD | UNION | INTER | box OVER box | MARK box
 *	| size box | font box | FAT box | SQRT box
 *	| box SUB box sbox | box SUP box | int SUB box sbox
 *	| int SUP box | int | box FROM box tbox | box TO box
 *	| left eqn right | left eqn | box diacrit | fwd/up/back/down box
 *	| column | MATRIX ... etc (see eqn.y)
 *
 * Precedence-map (hand parser):
 *	level 1 (lowest, right): FROM TO
 *	level 2 (left): OVER, SQRT (prefix sqrt binds here)
 *	level 3 (right): SUP SUB
 *	level 4 (right, prefix): SIZE FONT ROMAN ITALIC BOLD FAT
 *	level 5 (right, prefix): UP DOWN BACK FWD
 *	level 6 (left): LEFT RIGHT (delimiters, not binary ops)
 *	level 7 (highest, right): diacritics DOT DOTDOT HAT TILDE BAR
 *	  LOWBAR HIGHBAR UNDER VEC DYAD UTILDE (postfix on box)
 * Call chain: parse_stuff -> parse_eqn -> parse_box (= parse_fromto,
 *	FROM/TO, lowest, right) -> parse_over (OVER, left) ->
 *	parse_supsub (SUB/SUP, right) -> parse_diacritbox (diacritics,
 *	highest, postfix) -> parse_prefixbox (SIZE/FONT/moves, right) ->
 *	parse_boxprimary (atoms; MARK/FAT/SQRT/INT/LEFT/columns) ->
 *	parse_sbox/parse_tbox helpers.
 * Mid-rule {ps -= deltaps;} actions run immediately after consuming
 * SUB/SUP/FROM/TO, copying eqn.y verbatim.
 *
 * Error-recovery-map: original had one `error' production at `stuff'
 * level; any syntax error called yyerror("syntax error") via the
 * ERROR/WARNING macros. Hand parser calls yyerror("syntax error")
 * at the failure point, sets synerr, discards input up to end-of-input
 * (token 0/EOF, matching lex.c's EOF return on .EN/righteq) and
 * returns 1, preserving main.c's repeated-yyparse protocol.
 */

YYSTYPE yylval;
YYSTYPE yyval;

extern int yylex(void);
extern void yyerror(char *s);

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

static void
yyskip(void)
{
	int t;

	/* discard to end-of-input; lex returns 0/EOF at .EN/righteq */
	while((t = yyget()) != 0 && t != EOF)
		;
}

static int parse_stuff(void);
static int parse_eqn(void);
static int parse_lineupbox(void);
static int parse_sbox(void);
static int parse_tbox(void);
static int parse_box(void);
static int parse_fromto(void);
static int parse_frommiddle(void);
static int parse_over(void);
static int parse_supsub(void);
static int parse_submiddle(void);
static int parse_diacritbox(void);
static int parse_prefixbox(void);
static int parse_boxprimary(void);
static int parse_int(void);
static int parse_diacrit(void);
static int parse_left(void);
static int parse_right(void);
static int parse_column(void);
static int parse_collist(void);
static int parse_col(void);
static int parse_list(void);
static int parse_size(void);
static int parse_font(void);
static int parse_text(void);

static int
parse_int(void)
{
	if(yyget() != INT){
		yyerror("syntax error");
		return -1;
	}
	setintegral();
	return INT;
}

static int
parse_diacrit(void)
{
	int t;

	t = yyget();
	switch(t){
	case HAT:
		return HAT;
	case VEC:
		return VEC;
	case DYAD:
		return DYAD;
	case BAR:
		return BAR;
	case LOWBAR:
		return LOWBAR;
	case HIGHBAR:
		return HIGHBAR;
	case UNDER:
		return UNDER;
	case DOT:
		return DOT;
	case TILDE:
		return TILDE;
	case UTILDE:
		return UTILDE;
	case DOTDOT:
		return DOTDOT;
	default:
		yyerror("syntax error");
		return -1;
	}
}

static int
parse_left(void)
{
	int t, v;

	t = yyget();
	if(t != LEFT){
		yyerror("syntax error");
		return -1;
	}
	t = yypeek();
	if(t == '{'){
		yyget();
		return '{';
	}
	if(parse_text() < 0)
		return -1;
	v = ((char*)yylval)[0];
	return v;
}

static int
parse_right(void)
{
	int t, v;

	t = yyget();
	if(t != RIGHT){
		yyerror("syntax error");
		return -1;
	}
	t = yypeek();
	if(t == '}'){
		yyget();
		return '}';
	}
	if(parse_text() < 0)
		return -1;
	v = ((char*)yylval)[0];
	return v;
}

static int
parse_text(void)
{
	int t;

	t = yyget();
	if(t != CONTIG && t != QTEXT){
		yyerror("syntax error");
		return -1;
	}
	return t;
}

static int
parse_size(void)
{
	char *s;

	if(yyget() != SIZE){
		yyerror("syntax error");
		return -1;
	}
	if(parse_text() < 0)
		return -1;
	s = (char*)yylval;
	yyval = ps;
	setsize(s);
	return ps;
}

static int
parse_font(void)
{
	int t;
	char *s;
	static char R[] = "R";
	static char I[] = "I";
	static char B[] = "B";

	t = yypeek();
	if(t == ROMAN){
		yyget();
		setfont(R);
		return ROMAN;
	}
	if(t == ITALIC){
		yyget();
		setfont(I);
		return ITALIC;
	}
	if(t == BOLD){
		yyget();
		setfont(B);
		return BOLD;
	}
	if(t == FONT){
		yyget();
		if(parse_text() < 0)
			return -1;
		s = (char*)yylval;
		setfont(s);
		return FONT;
	}
	yyerror("syntax error");
	return -1;
}

static int
parse_col(void)
{
	int t, v;

	t = yyget();
	switch(t){
	case LCOL:
		v = startcol(LCOL);
		break;
	case CCOL:
		v = startcol(CCOL);
		break;
	case RCOL:
		v = startcol(RCOL);
		break;
	case COL:
		v = startcol(COL);
		break;
	default:
		yyerror("syntax error");
		return -1;
	}
	yyval = v;
	return v;
}

/* list: eqn | list ABOVE eqn  -- appends to lp[ct++] */
static int
parse_list(void)
{
	int e;

	e = parse_eqn();
	if(e < 0)
		return -1;
	lp[ct++] = e;
	while(yypeek() == ABOVE){
		yyget();
		e = parse_eqn();
		if(e < 0)
			return -1;
		lp[ct++] = e;
	}
	return 0;
}

/* column: col '{' list '}' | col text num '{' list '}' */
static int
parse_column(void)
{
	int c, gap, t;
	char *s;

	c = parse_col();
	if(c < 0)
		return -1;
	gap = DEFGAP;
	t = yypeek();
	if(t == CONTIG || t == QTEXT){
		yyget();
		s = (char*)yylval;
		gap = atoi(s);
	}
	if(yyget() != '{'){
		yyerror("syntax error");
		return -1;
	}
	if(parse_list() < 0)
		return -1;
	if(yyget() != '}'){
		yyerror("syntax error");
		return -1;
	}
	column(c, gap);
	return c;
}

static int
parse_collist(void)
{
	if(parse_column() < 0)
		return -1;
	while(yypeek() == LCOL || yypeek() == CCOL
	    || yypeek() == RCOL || yypeek() == COL){
		if(parse_column() < 0)
			return -1;
	}
	return 0;
}

/*
 * parse_boxprimary handles atomic boxes and the prefix operators
 * whose operand is a full box (MARK, FAT) or binds at OVER level
 * (SQRT).  SIZE/FONT/move prefixes live one level up in
 * parse_prefixbox; infix/postfix levels are parse_fromto (FROM/TO,
 * lowest, right), parse_over (OVER, left), parse_supsub (SUB/SUP,
 * right) and parse_diacritbox (diacritics, highest, postfix).
 */
static int
parse_boxprimary(void)
{
	int t, b, v, n;

	t = yypeek();
	switch(t){
	case '{':
		yyget();
		b = parse_eqn();
		if(b < 0)
			return -1;
		if(yyget() != '}'){
			yyerror("syntax error");
			return -1;
		}
		return b;
	case QTEXT:
		yyget();
		text(QTEXT, (char*)yylval);
		return 0;
	case CONTIG:
		yyget();
		text(CONTIG, (char*)yylval);
		return 0;
	case SPACE:
		yyget();
		text(SPACE, (char*)0);
		return 0;
	case THIN:
		yyget();
		text(THIN, (char*)0);
		return 0;
	case TAB:
		yyget();
		text(TAB, (char*)0);
		return 0;
	case SUM:
		yyget();
		funny(SUM);
		return 0;
	case PROD:
		yyget();
		funny(PROD);
		return 0;
	case UNION:
		yyget();
		funny(UNION);
		return 0;
	case INTER:
		yyget();
		funny(INTER);
		return 0;
	case MARK:
		yyget();
		b = parse_box();
		if(b < 0)
			return -1;
		mark(b);
		return 0;
	case FAT:
		yyget();
		b = parse_box();
		if(b < 0)
			return -1;
		fatbox(b);
		return 0;
	case SQRT:
		/* SQRT box; SQRT shares OVER's level (%left OVER SQRT),
		 * so the operand stops at OVER/FROM/TO (left-assoc
		 * reduce) but includes SUP/SUB/diacritics above it. */
		yyget();
		b = parse_supsub();
		if(b < 0)
			return -1;
		sqrt(b);
		return 0;
	case INT:
		/* int | int SUB box sbox | int SUP box  (eqn.y) */
		v = parse_int();
		if(v < 0)
			return -1;
		t = yypeek();
		if(t == SUB || t == SUP){
			yyget();
			ps -= deltaps;
			if(t == SUB){
				b = parse_submiddle();
				if(b < 0)
					return -1;
				n = parse_sbox();
				integral(v, b, n);
			}else{
				b = parse_supsub();
				if(b < 0)
					return -1;
				integral(v, 0, b);
			}
			return 0;
		}
		integral(v, 0, 0);
		return 0;
	case LEFT:
		{
			int l, e, r;
			l = parse_left();
			if(l < 0)
				return -1;
			e = parse_eqn();
			if(e < 0)
				return -1;
			t = yypeek();
			if(t == RIGHT){
				r = parse_right();
				if(r < 0)
					return -1;
				paren(l, e, r);
			}else
				paren(l, e, 0);
			return 0;
		}
	case LCOL:
	case CCOL:
	case RCOL:
	case COL:
		b = parse_column();
		if(b < 0)
			return -1;
		pile(b);
		ct = b;
		return 0;
	case MATRIX:
		yyget();
		v = ct;
		if(yyget() != '{'){
			yyerror("syntax error");
			return -1;
		}
		if(parse_collist() < 0)
			return -1;
		if(yyget() != '}'){
			yyerror("syntax error");
			return -1;
		}
		matrix(v);
		ct = v;
		return 0;
	default:
		yyerror("syntax error");
		return -1;
	}
}

static int
parse_sbox(void)
{
	/* sbox: SUP box | empty  (eqn.y, %prec SUP).
	 * The box nests SUP/SUB right but stops at OVER/FROM/TO below. */
	if(yypeek() == SUP){
		int b;
		yyget();
		b = parse_supsub();
		if(b < 0)
			return -1;
		return b;
	}
	return 0;
}

static int
parse_tbox(void)
{
	/* tbox: TO box | empty  (eqn.y, %prec TO).
	 * TO/FROM nest right at the lowest level, so the box takes
	 * everything via parse_fromto. */
	if(yypeek() == TO){
		int b;
		yyget();
		b = parse_fromto();
		if(b < 0)
			return -1;
		return b;
	}
	return 0;
}

static int
parse_box(void)
{
	/* entry point: lowest precedence level (FROM/TO) */
	return parse_fromto();
}

/*
 * Level 1 (lowest, right): FROM TO  (eqn.y %right FROM TO)
 *	a FROM b tbox		fromto(a, b, tbox)
 *	a TO b			fromto(a, 0, b)
 * Right recursion nests `a FROM b FROM c' as a FROM (b FROM c),
 * matching yacc.  The FROM middle box must not swallow a following
 * TO (it starts tbox), so it uses parse_frommiddle; the TO right
 * operand takes everything via parse_fromto.
 */
static int
parse_fromto(void)
{
	int b, m, sup;

	b = parse_over();
	if(b < 0)
		return -1;
	if(yypeek() == FROM){
		yyget();
		ps -= deltaps;
		m = parse_frommiddle();
		if(m < 0)
			return -1;
		sup = parse_tbox();
		if(sup < 0)
			return -1;
		fromto(b, m, sup);
		return 0;
	}
	if(yypeek() == TO){
		yyget();
		ps -= deltaps;
		m = parse_fromto();
		if(m < 0)
			return -1;
		fromto(b, 0, m);
		return 0;
	}
	return b;
}

/*
 * FROM middle box: FROM-chains nest right, but a TO ends the middle
 * (it starts tbox).
 */
static int
parse_frommiddle(void)
{
	int b, m, sup;

	b = parse_over();
	if(b < 0)
		return -1;
	if(yypeek() == FROM){
		yyget();
		ps -= deltaps;
		m = parse_frommiddle();
		if(m < 0)
			return -1;
		sup = parse_tbox();
		if(sup < 0)
			return -1;
		fromto(b, m, sup);
		return 0;
	}
	return b;
}

/*
 * Level 2 (left): OVER  (eqn.y %left OVER SQRT)
 *	a OVER b OVER c parses as (a OVER b) OVER c: loop with the
 *	right operand one level up, so OVER never nests right.
 */
static int
parse_over(void)
{
	int b, b2;

	b = parse_supsub();
	if(b < 0)
		return -1;
	while(yypeek() == OVER){
		yyget();
		b2 = parse_supsub();
		if(b2 < 0)
			return -1;
		boverb(b, b2);
		b = 0;
	}
	return b;
}

/*
 * Level 3 (right): SUP SUB  (eqn.y %right SUP SUB)
 *	a SUP b SUP c parses as a SUP (b SUP c): right recursion.
 *	A SUP following a SUB middle box belongs to sbox (as in eqn.y,
 *	where the sbox production wins the reduce/reduce conflict), so
 *	the SUB middle box uses parse_submiddle, which stops at SUP.
 */
static int
parse_supsub(void)
{
	int b, r, sup;

	b = parse_diacritbox();
	if(b < 0)
		return -1;
	if(yypeek() == SUB){
		yyget();
		ps -= deltaps;
		r = parse_submiddle();
		if(r < 0)
			return -1;
		sup = parse_sbox();
		if(sup < 0)
			return -1;
		subsup(b, r, sup);
		return 0;
	}
	if(yypeek() == SUP){
		yyget();
		ps -= deltaps;
		r = parse_supsub();
		if(r < 0)
			return -1;
		subsup(b, 0, r);
		return 0;
	}
	return b;
}

/*
 * SUB middle box: SUB-chains nest right, but a SUP ends the middle
 * (it starts sbox).
 */
static int
parse_submiddle(void)
{
	int b, r, sup;

	b = parse_diacritbox();
	if(b < 0)
		return -1;
	if(yypeek() == SUB){
		yyget();
		ps -= deltaps;
		r = parse_submiddle();
		if(r < 0)
			return -1;
		sup = parse_sbox();
		if(sup < 0)
			return -1;
		subsup(b, r, sup);
		return 0;
	}
	return b;
}

/*
 * Highest level (postfix): diacritics (eqn.y %right DOT DOTDOT HAT
 * TILDE BAR LOWBAR HIGHBAR UNDER VEC DYAD UTILDE).
 * `a HAT BAR' chains left as (a HAT) BAR.
 */
static int
parse_diacritbox(void)
{
	int b, d;

	b = parse_prefixbox();
	if(b < 0)
		return -1;
	while(yypeek() == DOT || yypeek() == DOTDOT || yypeek() == HAT
	    || yypeek() == TILDE || yypeek() == BAR || yypeek() == LOWBAR
	    || yypeek() == HIGHBAR || yypeek() == UNDER || yypeek() == VEC
	    || yypeek() == DYAD || yypeek() == UTILDE){
		d = parse_diacrit();
		if(d < 0)
			return -1;
		diacrit(b, d);
		b = 0;
	}
	return b;
}

/*
 * Prefix levels: SIZE FONT ROMAN ITALIC BOLD (%prec SIZE/FONT) and
 * FWD UP BACK DOWN (%prec UP), all right-associative.  One function
 * with right recursion covers nesting (`size 10 up 5 x') either way
 * around; the operand falls through to parse_diacritbox, so the
 * lower-precedence infix operators (FROM/TO, OVER, SUB/SUP)
 * correctly stay outside the prefix operand.
 */
static int
parse_prefixbox(void)
{
	int t, v, n;

	t = yypeek();
	if(t == SIZE){
		v = parse_size();
		if(v < 0)
			return -1;
		n = parse_prefixbox();
		if(n < 0)
			return -1;
		size(v, n);
		return 0;
	}
	if(t == ROMAN || t == ITALIC || t == BOLD || t == FONT){
		v = parse_font();
		if(v < 0)
			return -1;
		n = parse_prefixbox();
		if(n < 0)
			return -1;
		font(v, n);
		return 0;
	}
	if(t == FWD || t == UP || t == BACK || t == DOWN){
		int f = t;
		yyget();
		if(parse_text() < 0)
			return -1;
		n = atoi((char*)yylval);
		v = parse_prefixbox();
		if(v < 0)
			return -1;
		if(f == FWD)
			move(FWD, n, v);
		else if(f == UP)
			move(UP, n, v);
		else if(f == BACK)
			move(BACK, n, v);
		else
			move(DOWN, n, v);
		return 0;
	}
	return parse_boxprimary();
}

static int
parse_lineupbox(void)
{
	int b;

	if(yyget() != LINEUP){
		yyerror("syntax error");
		return -1;
	}
	b = parse_box();
	if(b < 0)
		return -1;
	lineup(1);
	return b;
}

static int
parse_eqn(void)
{
	int b, b2;

	/* eqn must start a box; LINEUP alone is also an eqn */
	if(yypeek() == LINEUP){
		yyget();
		lineup(0);
		b = 0;
	}else{
		b = parse_box();
		if(b < 0)
			return -1;
	}
	for(;;){
		int t = yypeek();
		if(t == LINEUP){
			b2 = parse_lineupbox();
			if(b2 < 0)
				return -1;
			eqnbox(b, b2, 1);
			b = 0;
		}else if(t == '{' || t == QTEXT || t == CONTIG
		    || t == SPACE || t == THIN || t == TAB
		    || t == SUM || t == PROD || t == UNION
		    || t == INTER || t == MARK || t == SIZE
		    || t == ROMAN || t == ITALIC || t == BOLD
		    || t == FAT || t == SQRT || t == INT
		    || t == FWD || t == UP || t == BACK
		    || t == DOWN || t == LEFT || t == LCOL
		    || t == CCOL || t == RCOL || t == COL
		    || t == MATRIX){
			b2 = parse_box();
			if(b2 < 0)
				return -1;
			eqnbox(b, b2, 0);
			b = 0;
		}else
			break;
	}
	return b;
}

static int
parse_stuff(void)
{
	int t, e;

	t = yypeek();
	if(t == 0 || t == EOF){
		yyget();
		eqnreg = 0;
		return 0;
	}
	e = parse_eqn();
	if(e < 0){
		yyerror("syntax error");
		return -1;
	}
	/* trailing garbage ends the equation; lex returns EOF at .EN */
	putout(e);
	return 0;
}

int
yyparse(void)
{
	int r;

	yyhave = 0;
	r = parse_stuff();
	if(r < 0){
		synerr = 1;
		yyskip();
		return 1;
	}
	return 0;
}
