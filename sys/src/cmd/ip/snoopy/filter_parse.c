#include <u.h>
#include <libc.h>
#include <ctype.h>
#include "dat.h"
#include "filter_parse.h"

/*
 * Hand-written recursive-descent replacement for ip/snoopy/filter.y (LALR).
 *
 * Original yacc grammar (precedence low -> high, declared before %%):
 *	%right '!'
 *	%left  '|'
 *	%left  '&'
 *	%left  LOR
 *	%left  LAND
 *	filter: expr		{ filter = $$; }
 *	expr: WORD
 *	| WORD '=' WORD		{ $2->l = $1; $2->r = $3; $$ = $2; }
 *	| WORD NE WORD		{ $2->l = newfilter(); $2->l->op='='; $2->l->l=$1;
 *				  $2->l->r=$3; $2->op='!'; $$=$2; }
 *	| WORD '(' expr ')'	{ $1->l = $3; $$ = $1; }
 *	| '(' expr ')'		{ $$ = $2; }
 *	| expr LOR expr		{ $2->l = $1; $2->r = $3; $$ = $2; }
 *	| expr LAND expr	{ $2->l = $1; $2->r = $3; $$ = $2; }
 *	| '!' expr		{ $1->l = $2; $$ = $1; }
 *
 * Precedence-map (recursive-descent levels, low -> high):
 *   LOR (left) < LAND (left) < '|' (left) < '&' (left) < '!' (right)
 * Call chain: parse_filter -> parse_lor -> parse_land -> parse_bitor ->
 *             parse_bitand -> parse_unary -> parse_primary.
 * Note: WORD carries no %prec so it binds loosest — `expr LOR expr` wins
 * over `WORD LOR WORD`, hence LOR/LAND sit below the WORD productions.
 *
 * Error-recovery-map:
 *   original: no `error' production; yyerror()->sysfatal hard-abort.
 *   hand: identical.  yylex() is context-free, so syntax errors are only
 *   unexpected EOF (e.g. '(' with no matching ')') or a trailing token
 *   after the top expr; both route through yyerror() -> sysfatal.
 */

#define YYEOF 0

/*
 * Lexer state + YYSTYPE interface (kept verbatim from filter.y's yylex
 * so the file is self-contained once filter.y is deleted).
 */
YYSTYPE yylval;

char	*yylp;		/* next character to be lex'd */
char	*yybuffer;
char	*yyend;

static int	yyhave;
static int	yytok;
static YYSTYPE yyval;

static int	yylex(void);
static Filter	*parse_lor(void);
static Filter	*parse_land(void);
static Filter	*parse_bitor(void);
static Filter	*parse_bitand(void);
static Filter	*parse_unary(void);
static Filter	*parse_primary(void);
static Filter	*parse_word(Filter *f);

static int
yylex(void)
{
	char *p;
	int c;

	if(yylp == nil)
		return 0;
	while(isspace(*yylp))
		yylp++;
	if(*yylp == 0)
		return 0;

	yylval = newfilter();

	p = strpbrk(yylp, "!|&()= ");
	if(p == 0){
		yylval->op = WORD;
		yylval->s = strdup(yylp);
		if(yylval->s == nil)
			sysfatal("parsing filter: %r");
		yylp = nil;
		return WORD;
	}
	c = *p;
	if(p != yylp){
		yylval->op = WORD;
		*p = 0;
		yylval->s = strdup(yylp);
		if(yylval->s == nil)
			sysfatal("parsing filter: %r");
		*p = c;
		yylp = p;
		return WORD;
	}

	yylp++;
	if(c == '!' && *yylp == '='){
		c = NE;
		yylp++;
	}
	else if(c == '&' && *yylp == '&'){
		c = LAND;
		yylp++;
	}
	else if(c == '|' && *yylp == '|'){
		c = LOR;
		yylp++;
	}
	yylval->op = c;
	return c;
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

/*
 * Initialize the parsing.  Done once for each header field.
 */
void
yyinit(char *p)
{
	yylp = p;
}

void
yyerror(char*)
{
	sysfatal("error parsing filter");
}

/*
 * parse_word: given a WORD Filter f (already lexed), handle the optional
 * trailing '=' WORD, NE WORD, or '(' expr ')' suffix.  Returns the
 * (possibly modified) filter.
 */
static Filter*
parse_word(Filter *f)
{
	switch(yypeek()){
	case '=':
		yyget();
		f->l = yylval;		/* next WORD */
		if(yypeek() != WORD)
			yyerror("error parsing filter");
		f->r = yylval; yyget();
		break;
	case NE:
		yyget();
		f->l = newfilter();
		f->l->op = '=';
		f->l->l = f;
		if(yypeek() != WORD)
			yyerror("error parsing filter");
		f->l->r = yylval; yyget();
		f->op = '!';
		break;
	case '(':
		yyget();
		f->l = parse_lor();
		if(yypeek() != ')')
			yyerror("error parsing filter");
		yyget();
		break;
	default:
		/* bare WORD */
		break;
	}
	return f;
}

static Filter*
parse_primary(void)
{
	int t;
	Filter *f;

	t = yypeek();
	switch(t){
	case WORD:
		yyget();
		return parse_word(yylval);
	case '(':
		yyget();
		f = parse_lor();
		if(yypeek() != ')')
			yyerror("error parsing filter");
		yyget();
		return f;
	default:
		/* '!' handled in parse_unary */
		if(t == '!')
			return parse_unary();
		yyerror("error parsing filter");
		return nil;	/* not reached */
	}
}

static Filter*
parse_unary(void)
{
	Filter *f;

	if(yypeek() == '!'){
		f = newfilter();
		f->op = yyget();	/* consume '!' */
		f->l = parse_unary();
		return f;
	}
	return parse_primary();
}

static Filter*
parse_bitand(void)
{
	Filter *f;

	f = parse_unary();
	while(yypeek() == '&'){
		yyget();
		f->l = f;
		f->r = parse_unary();
	}
	return f;
}

static Filter*
parse_bitor(void)
{
	Filter *f;

	f = parse_bitand();
	while(yypeek() == '|'){
		yyget();
		f->l = f;
		f->r = parse_bitand();
	}
	return f;
}

static Filter*
parse_land(void)
{
	Filter *f;

	f = parse_bitor();
	while(yypeek() == LAND){
		yyget();
		f->l = f;
		f->r = parse_bitor();
	}
	return f;
}

static Filter*
parse_lor(void)
{
	Filter *f;

	f = parse_land();
	while(yypeek() == LOR){
		yyget();
		f->l = f;
		f->r = parse_land();
	}
	return f;
}

int
yyparse(void)
{
	yyhave = 0;
	filter = parse_lor();
	if(yypeek() != YYEOF && yypeek() != 0)
		yyerror("error parsing filter");
	return 0;
}
