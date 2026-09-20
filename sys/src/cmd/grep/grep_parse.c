#include "grep.h"

/*
 * Hand-written recursive-descent replacement for cmd/grep/grep.y (LALR).
 *
 * Original yacc grammar (precedence low -> high — purely structural,
 * no %left/%right/%nonassoc declarations):
 *	prog: (empty) { yyerror("empty pattern"); }
 *	|	expr newlines
 *	expr:	expr0
 *	|	expr newlines expr0	{ $$ = re2or($1, $3); }
 *	expr0:	expr1
 *	|	LSTAR { literal = 1; } expr1	{ $$ = $2; }
 *	expr1:	expr2
 *	|	expr1 LALT expr2	{ $$ = re2or($1, $3); }
 *	expr2:	expr3
 *	|	expr2 expr3		{ $$ = re2cat($1, $2); }
 *	expr3:	expr4
 *	|	expr3 LSTAR		{ $$ = re2star($1); }
 *	|	expr3 LPLUS		{ ... one-or-more patch ... }
 *	|	expr3 LQUES		{ ... optional patch ... }
 *	expr4:	LCHAR | LBEGIN | LEND | LDOT | LCLASS | LLPAREN expr1 LRPAREN
 *	newlines: LNEWLINE | newlines LNEWLINE
 *
 * Precedence-map (structural, low -> high):
 *   top-level alternation via newlines [parse_prog]
 *   LALT [parse_expr1]
 *   cat via juxtaposition [parse_expr2]
 *   postfix star/plus/ques [parse_expr3]
 *   primary [parse_expr4]
 *
 * Call chain: parse_prog -> parse_expr -> parse_expr0 -> parse_expr1 ->
 *   parse_expr2 -> parse_expr3 -> parse_expr4.
 *
 * Error-recovery-map:
 *   original: only `yyerror("empty pattern")` fires for empty input.
 *   hand: parse_prog checks for empty input first.
 */

static int	yyhave;
static int	yytok;
static YYSTYPE yyval;

YYSTYPE yylval;

static long
yylex(void)
{
	char *q, *eq;
	int c, s;

	if(peekc) {
		s = peekc;
		peekc = 0;
		return s;
	}
	c = getrec();
	if(literal) {
		if(c != 0 && c != '\n') {
			yylval.val = c;
			return LCHAR;
		}
		literal = 0;
	}
	switch(c) {
	default:
		yylval.val = c;
		s = LCHAR;
		break;
	case '\\':
		c = getrec();
		yylval.val = c;
		s = LCHAR;
		if(c == '\n')
			s = LNEWLINE;
		break;
	case '[':
		goto getclass;
	case '(':
		s = LLPAREN;
		break;
	case ')':
		s = LRPAREN;
		break;
	case '|':
		s = LALT;
		break;
	case '*':
		s = LSTAR;
		break;
	case '+':
		s = LPLUS;
		break;
	case '?':
		s = LQUES;
		break;
	case '^':
		s = LBEGIN;
		break;
	case '$':
		s = LEND;
		break;
	case '.':
		s = LDOT;
		break;
	case 0:
		peekc = -1;
	case '\n':
		s = LNEWLINE;
		break;
	}
	return s;

getclass:
	q = u.string;
	eq = q + nelem(u.string) - 5;
	c = getrec();
	if(c == '^') {
		q[0] = '^';
		q[1] = '\n';
		q[2] = '-';
		q[3] = '\n';
		q += 4;
		c = getrec();
	}
	for(;;) {
		if(q >= eq)
			error("class too long");
		if(c == ']' || c == 0)
			break;
		if(c == '\\') {
			*q++ = c;
			c = getrec();
			if(c == 0)
				break;
		}
		*q++ = c;
		c = getrec();
	}
	*q = 0;
	if(c == 0)
		return LBAD;
	yylval.str = u.string;
	return LCLASS;
}

static int
yypeek(void)
{
	if(!yyhave){
		yytok = yylex();
		yyval = yylval;
		yyhave = 1;
	}
	return yytok;
}

static int
yyget(void)
{
	if(yyhave){
		yyhave = 0;
		yylval = yyval;
		return yytok;
	}
	return yylex();
}

static void
parse_newlines(void)
{
	while(yypeek() == LNEWLINE)
		yyget();
}

/*
 * Returns non-zero if token t can start an expr4 (and hence expr3+).
 */
static int
expr3_can_start(int t)
{
	return t == LCHAR || t == LBEGIN || t == LEND ||
	       t == LDOT || t == LCLASS || t == LLPAREN;
}

/*
 * expr4: LCHAR | LBEGIN | LEND | LDOT | LCLASS | LLPAREN expr1 LRPAREN
 */
static Re2
parse_expr4(void)
{
	Re2 r;

	switch(yypeek()){
	case LCHAR:
		yyget();
		r.beg = ral(Tclass);
		r.beg->lo = yyval.val;
		r.beg->hi = yyval.val;
		r.end = r.beg;
		return r;
	case LBEGIN:
		yyget();
		r.beg = ral(Tbegin);
		r.end = r.beg;
		return r;
	case LEND:
		yyget();
		r.beg = ral(Tend);
		r.end = r.beg;
		return r;
	case LDOT:
		yyget();
		return re2class("^\n");
	case LCLASS:
		yyget();
		return re2class(yylval.str);
	case LLPAREN:
		yyget();
		r = parse_expr1();
		if(yypeek() != LRPAREN)
			yyerror("syntax error");
		yyget();
		return r;
	default:
		yyerror("syntax error");
		r.beg = nil;
		r.end = nil;
		return r;
	}
}

/*
 * expr3: expr4 (LSTAR | LPLUS | LQUES)*
 */
static Re2
parse_expr3(void)
{
	Re2 r, tmp;

	r = parse_expr4();
	for(;;){
		switch(yypeek()){
		case LSTAR:
			yyget();
			r = re2star(r);
			break;
		case LPLUS:
			yyget();
			tmp.beg = ral(Talt);
			patchnext(r.end, tmp.beg);
			tmp.beg->alt = r.beg;
			tmp.end = tmp.beg;
			tmp.beg = r.beg;
			r = tmp;
			break;
		case LQUES:
			yyget();
			tmp.beg = ral(Talt);
			tmp.beg->alt = r.beg;
			tmp.end = r.end;
			appendnext(tmp.end, tmp.beg);
			r = tmp;
			break;
		default:
			return r;
		}
	}
}

/*
 * expr2: expr3 (expr3)*  — concatenation via juxtaposition
 */
static Re2
parse_expr2(void)
{
	Re2 r;

	r = parse_expr3();
	while(expr3_can_start(yypeek()))
		r = re2cat(r, parse_expr3());
	return r;
}

/*
 * expr1: expr2 (LALT expr2)*
 */
static Re2
parse_expr1(void)
{
	Re2 r;

	r = parse_expr2();
	while(yypeek() == LALT){
		yyget();
		r = re2or(r, parse_expr2());
	}
	return r;
}

/*
 * expr0: [LSTAR] expr1   (LSTAR sets literal=1)
 */
static Re2
parse_expr0(void)
{
	Re2 r;

	if(yypeek() == LSTAR){
		yyget();
		literal = 1;
	}
	r = parse_expr1();
	return r;
}

/*
 * expr: expr0 | expr newlines expr0 { re2or($1,$3) }
 *
 * After the first expr0, a run of newlines precedes a possible second
 * expr0 (alternative).  We consume newlines then check whether an
 * expr0 can start; if so, join with re2or and repeat.
 */
static Re2
parse_expr(void)
{
	Re2 r;

	r = parse_expr0();
	for(;;){
		/* consume a newline run */
		if(yypeek() != LNEWLINE)
			break;
		yyget();
		while(yypeek() == LNEWLINE)
			yyget();
		if(yypeek() == 0 || yypeek() == -1)
			break;
		if(!expr3_can_start(yypeek()))
			break;
		r = re2or(r, parse_expr0());
	}
	return r;
}

int
yyparse(void)
{
	Re2 r;

	yyhave = 0;
	if(yypeek() == 0 || yypeek() == -1){
		yyerror("empty pattern");
		return 0;
	}

	r = parse_expr();
	parse_newlines();

	/*
	 * prog: expr newlines { finalize: prepend .* and append ^.|\n }
	 */
	{
		Re2 e1;

		e1.beg = ral(Tend);
		e1.end = e1.beg;
		r = re2cat(r, e1);
		r = re2cat(re2star(re2or(re2char(0x00, '\n'-1), re2char('\n'+1, 0xff))), r);
		r = re2cat(re2star(re2char(0x00, 0xff)), r);
		topre = r;
	}
	return 0;
}
