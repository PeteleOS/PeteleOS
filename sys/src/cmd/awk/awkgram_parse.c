/*
 * Hand-written recursive-descent replacement for awkgram.y (yacc LALR).
 *
 * Precedence (low -> high), from %right/%left/%nonassoc in awkgram.y:
 *   level 1 (right): ASGNOP
 *   level 2 (right): '?' ':'
 *   level 3 (left): BOR
 *   level 4 (left): AND
 *   level 5 (left): GETLINE
 *   level 6 (nonassoc): APPEND EQ GE GT LE LT NE MATCHOP IN '|'
 *   level 7 (left): ARG BLTIN BREAK CALL CLOSE CONTINUE DELETE DO EXIT
 *     FOR FUNC GSUB IF INDEX LSUBSTR MATCHFCN NEXT NUMBER PRINT PRINTF
 *     RETURN SPLIT SPRINTF STRING SUB SUBSTR REGEXPR VAR VARNF IVAR
 *     WHILE '('
 *   level 8 (left): CAT (concatenation, via pattern term %prec CAT)
 *   level 9 (left): '+' '-'
 *   level 10 (left): '*' '/' '%'
 *   level 11 (left): NOT UMINUS
 *   level 12 (right): POWER
 *   level 13 (right): DECR INCR
 *   level 14 (left): INDIRECT (highest)
 *
 * Call chains:
 *   pattern: parse_pattern (ASGNOP, right) -> parse_cond ('?')
 *     -> parse_bor -> parse_and -> parse_eq
 *     -> parse_match_full (MATCHOP/IN/bar-GETLINE/CAT)
 *     -> parse_operand (re | cat) -> parse_cat -> parse_term
 *   ppattern (print args, no ASGNOP/cond/comparison/bar):
 *     parse_ppattern -> parse_pbor -> parse_pand
 *     -> parse_ppmatch (MATCHOP/IN/CAT) -> parse_operand -> parse_cat
 *   term: parse_term -> parse_term_add -> parse_term_mul
 *     -> parse_term_pow (POWER, right) -> parse_term_unary
 *     (-/+/NOT/CLOSE/DECR/INCR/INDIRECT/GETLINE prefix)
 *     -> parse_term_postfix (INCR/DECR postfix) -> parse_primary
 *   statements: parse_pas -> parse_pa_stats -> parse_pa_stat
 *     (pa_pat/XBEGIN/XEND/FUNC/brace-block) -> parse_pattern/parse_stmtlist
 *     parse_stmt -> parse_simple_stmt/parse_if/parse_while/parse_for/...
 *
 * Error-recovery-map: original had `program: error' (yyclearin; bracecheck;
 *   SYNTAX bailing out) and `simple_stmt: error' (yyclearin; SYNTAX illegal
 *   statement). SYNTAX sets errorflag without exiting. Hand parser records
 *   a local synerr on structural mismatch, propagates NULL up, and recovers
 *   exactly once per bad region: stmtlist recovery reports illegal statement,
 *   pa_stats recovery reports bailing out, each followed by skip to NL/';'
 *   (consumed) or '}'/EOF (left in place). yyclearin is clearing the private
 *   2-token lookahead buffer.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "awk.h"
#include "awkgram_parse.h"

#define makedfa(a,b) compre(a)

extern int errorflag;
extern int safe;

Node *beginloc = 0;
Node *endloc = 0;
int infunc = 0;
int inloop = 0;
char *curfname = 0;
Node *arglist = 0;

YYSTYPE yylval;

void checkdup(Node*, Cell*);

int
yywrap(void)
{
	return 1;
}

/* ---- 2-token lookahead over yylex() ---- */
static int yn = 0;
static int yyt[2];
static YYSTYPE yyv[2];

static void
yyfill(int n)
{
	while(yn < n){
		yyt[yn] = yylex();
		yyv[yn] = yylval;
		yn++;
	}
}

static int
yypeek(void)
{
	yyfill(1);
	return yyt[0];
}

static int
yypeek2(void)
{
	yyfill(2);
	return yyt[1];
}

static YYSTYPE *
peekval(void)
{
	yyfill(1);
	return &yyv[0];
}

static int
yyget(void)
{
	int t;

	yyfill(1);
	t = yyt[0];
	yylval = yyv[0];
	if(yn == 2){
		yyt[0] = yyt[1];
		yyv[0] = yyv[1];
	}
	yn--;
	return t;
}

/* ---- error flag + recovery ---- */
static int synerr = 0;

/* skip NL tokens */
static void
skip_nl(void)
{
	while(yypeek() == NL)
		yyget();
}

/* skip NL/';' separator tokens (pst/opt_pst) */
static void
skip_pst(void)
{
	int t;

	while((t = yypeek()) == NL || t == ';')
		yyget();
}

/* yyclearin equivalent + skip to NL/';' (consumed) or '}'/EOF (kept) */
static void
sync_stmt(void)
{
	int t;

	yn = 0;
	for(;;){
		t = yylex();
		if(t == 0 || t == '}'){
			yyt[0] = t;
			yyv[0] = yylval;
			yn = 1;
			return;
		}
		if(t == NL || t == ';')
			return;
	}
}

/* expect exact token; mismatch sets synerr */
static int
expect(int tok)
{
	if(yypeek() != tok){
		synerr = 1;
		return 0;
	}
	yyget();
	return 1;
}

static int
expect_lparen(void)
{
	return expect('(');
}

/* rparen/comma/and/bor/do/else/lbrace/rbrace allow trailing NLs */
static int
expect_rparen(void)
{
	if(!expect(')'))
		return 0;
	skip_nl();
	return 1;
}

static int
expect_rbrace(void)
{
	if(!expect('}'))
		return 0;
	skip_nl();
	return 1;
}

static int
expect_lbrace(void)
{
	if(!expect('{'))
		return 0;
	skip_nl();
	return 1;
}

static int
expect_comma(void)
{
	if(!expect(','))
		return 0;
	skip_nl();
	return 1;
}

/* ---- forward declarations ---- */
static int can_start_pa_stat(int t);
static Node *parse_pas(void);
static Node *parse_pa_stats(void);
static Node *parse_pa_stat(void);
static Node *parse_pattern(void);
static Node *parse_cond(void);
static Node *parse_bor(void);
static Node *parse_and(void);
static Node *parse_eq(void);
static Node *parse_match_full(void);
static Node *parse_ppattern(void);
static Node *parse_pbor(void);
static Node *parse_pand(void);
static Node *parse_ppmatch(void);
static Node *parse_operand(void);
static Node *parse_cat(void);
static Node *parse_re(void);
static char *parse_reg_expr(void);
static Node *parse_term(void);
static Node *parse_term_add(void);
static Node *parse_term_mul(void);
static Node *parse_term_pow(void);
static Node *parse_term_unary(void);
static Node *parse_term_postfix(void);
static Node *parse_primary(void);
static Node *parse_var(void);
static Node *parse_varname(void);
static Cell *parse_funcname(void);
static Node *parse_varlist(void);
static Node *parse_patlist(void);
static Node *parse_pplist(void);
static Node *parse_prarg(void);
static Node *parse_simple_stmt(void);
static Node *parse_opt_simple_stmt(void);
static int parse_st(void);
static Node *parse_stmt(void);
static Node *parse_stmtlist(void);
static Node *parse_for(void);

/* ---- token classes ---- */
static int
can_start_term(int t)
{
	switch(t){
	case '-': case '+': case NOT: case BLTIN: case CALL: case CLOSE:
	case DECR: case INCR: case VAR: case ARG: case VARNF: case IVAR:
	case INDIRECT: case GETLINE: case INDEX: case '(': case MATCHFCN:
	case NUMBER: case SPLIT: case SPRINTF: case STRING: case SUB:
	case GSUB: case SUBSTR:
		return 1;
	default:
		return 0;
	}
}

static int
can_start_var(int t)
{
	switch(t){
	case VAR: case ARG: case VARNF: case IVAR: case INDIRECT:
		return 1;
	default:
		return 0;
	}
}

/* var nonterminal statue: cell var, ARG/VARNF/INDIRECT wrapper, ARRAY */
static int
is_var_node(Node *n)
{
	if(n == NIL)
		return 0;
	if(isvalue(n))
		return ((Cell *)(n->narg[0]))->csub == CVAR;
	if(n->ntype != NEXPR)
		return 0;
	return n->nobj == ARG || n->nobj == VARNF
	    || n->nobj == INDIRECT || n->nobj == ARRAY;
}

/* ---- program ---- */
int
yyparse(void)
{
	Node *t;

	yn = 0;
	synerr = 0;
	t = parse_pas();
	if(synerr){
		synerr = 0;
		yyerror("bailing out");
		sync_stmt();
	}
	if(yypeek() != 0){
		yyerror("bailing out");
		yn = 0;
		while(yylex() != 0)
			;
	}
	if(errorflag == 0)
		winner = stat3(PROGRAM, beginloc, t, endloc);
	return 0;
}

/* pas: opt_pst | opt_pst pa_stats opt_pst */
static Node *
parse_pas(void)
{
	Node *t;

	skip_pst();
	if(yypeek() == 0)
		return NIL;
	t = parse_pa_stats();
	skip_pst();
	return t;
}

/* pa_stats: pa_stat | pa_stats opt_pst pa_stat */
static Node *
parse_pa_stats(void)
{
	Node *t = NIL, *s;
	int pk;

	for(;;){
		skip_pst();
		pk = yypeek();
		if(pk == 0 || pk == '}')
			break;
		if(!can_start_pa_stat(pk)){
			yyget();
			yyerror("bailing out");
			sync_stmt();
			synerr = 0;
			continue;
		}
		s = parse_pa_stat();
		if(s == NULL && synerr){
			yyerror("bailing out");
			sync_stmt();
			synerr = 0;
			continue;
		}
		t = linkum(t, s);
	}
	return t;
}

static int
can_start_pa_stat(int t)
{
	if(t == XBEGIN || t == XEND || t == FUNC || t == '{')
		return 1;
	if(t == '/')
		return 1;
	return can_start_term(t);
}

/* pa_stat: pattern-action | block | BEGIN/END | function def */
static Node *
parse_pa_stat(void)
{
	int t = yypeek();
	Node *a, *b, *c;
	Cell *cp;

	if(t == XBEGIN || t == XEND){
		int isbegin = (t == XBEGIN);

		yyget();
		if(!expect_lbrace())
			return NULL;
		a = parse_stmtlist();
		if(synerr)
			return NULL;
		if(!expect_rbrace())
			return NULL;
		if(isbegin)
			beginloc = linkum(beginloc, a);
		else
			endloc = linkum(endloc, a);
		return NIL;
	}
	if(t == FUNC){
		Node *vl, *body;

		yyget();
		cp = parse_funcname();
		if(cp == NULL)
			return NULL;
		if(!expect_lparen())
			return NULL;
		vl = parse_varlist();
		if(synerr)
			return NULL;
		if(!expect_rparen())
			return NULL;
		infunc++;
		if(!expect_lbrace())
			return NULL;
		body = parse_stmtlist();
		if(synerr)
			return NULL;
		if(!expect_rbrace())
			return NULL;
		infunc--;
		curfname = 0;
		defn(cp, vl, body);
		return NIL;
	}
	if(t == '{'){
		yyget();
		skip_nl();
		a = parse_stmtlist();
		if(synerr)
			return NULL;
		if(!expect_rbrace())
			return NULL;
		return stat2(PASTAT, NIL, a);
	}
	/* pa_pat forms */
	a = parse_pattern();
	if(a == NULL)
		return NULL;
	a = notnull(a);
	if(yypeek() == ','){
		yyget();
		skip_nl();
		b = parse_pattern();
		if(b == NULL)
			return NULL;
		b = notnull(b);
		if(yypeek() == '{'){
			yyget();
			skip_nl();
			c = parse_stmtlist();
			if(synerr)
				return NULL;
			if(!expect_rbrace())
				return NULL;
			return pa2stat(a, b, c);
		}
		return pa2stat(a, b, stat2(PRINT, rectonode(), NIL));
	}
	if(yypeek() == '{'){
		yyget();
		skip_nl();
		b = parse_stmtlist();
		if(synerr)
			return NULL;
		if(!expect_rbrace())
			return NULL;
		return stat2(PASTAT, a, b);
	}
	return stat2(PASTAT, a, stat2(PRINT, rectonode(), NIL));
}

/* pattern: var ASGNOP pattern (right) | cond */
static Node *
parse_pattern(void)
{
	Node *l = parse_cond(), *r;
	int op;

	if(l == NULL)
		return NULL;
	if(yypeek() == ASGNOP){
		if(!is_var_node(l)){
			synerr = 1;
			return NULL;
		}
		op = peekval()->i;
		yyget();
		r = parse_pattern();
		if(r == NULL)
			return NULL;
		return op2(op, l, r);
	}
	return l;
}

/* pattern '?' pattern ':' pattern (right) */
static Node *
parse_cond(void)
{
	Node *l = parse_bor(), *m, *r;

	if(l == NULL)
		return NULL;
	if(yypeek() == '?'){
		yyget();
		m = parse_pattern();
		if(m == NULL)
			return NULL;
		if(yypeek() != ':'){
			synerr = 1;
			return NULL;
		}
		yyget();
		r = parse_pattern();
		if(r == NULL)
			return NULL;
		return op3(CONDEXPR, notnull(l), m, r);
	}
	return l;
}

/* pattern bor pattern (left) */
static Node *
parse_bor(void)
{
	Node *l = parse_and(), *r;

	if(l == NULL)
		return NULL;
	for(;;){
		if(yypeek() != BOR)
			return l;
		yyget();
		skip_nl();
		r = parse_and();
		if(r == NULL)
			return NULL;
		l = op2(BOR, notnull(l), notnull(r));
	}
}

/* pattern and pattern (left) */
static Node *
parse_and(void)
{
	Node *l = parse_eq(), *r;

	if(l == NULL)
		return NULL;
	for(;;){
		if(yypeek() != AND)
			return l;
		yyget();
		skip_nl();
		r = parse_eq();
		if(r == NULL)
			return NULL;
		l = op2(AND, notnull(l), notnull(r));
	}
}

/* pattern EQ/GE/GT/LE/LT/NE pattern (nonassoc: single) */
static Node *
parse_eq(void)
{
	Node *l = parse_match_full(), *r;
	int op = yypeek();

	if(l == NULL)
		return NULL;
	if(op != EQ && op != GE && op != GT && op != LE && op != LT
	&& op != NE)
		return l;
	yyget();
	r = parse_match_full();
	if(r == NULL)
		return NULL;
	return op2(op, l, r);
}

/* MATCHOP/IN/bar-GETLINE then CAT juxtaposition */
static Node *
parse_match_full(void)
{
	Node *l = parse_operand(), *r, *v;
	int op;
	char *s;

	if(l == NULL)
		return NULL;
	if(yypeek() == MATCHOP){
		op = peekval()->i;
		yyget();
		if(yypeek() == '/'){
			yyget();
			startreg();
			if(yypeek() != REGEXPR){
				synerr = 1;
				return NULL;
			}
			s = peekval()->s;
			yyget();
			if(yypeek() != '/'){
				synerr = 1;
				return NULL;
			}
			yyget();
			l = op3(op, NIL, l, (Node *)makedfa(s, 0));
		}else{
			r = parse_cat();
			if(r == NULL)
				return NULL;
			if(constnode(r))
				l = op3(op, NIL, l,
				    (Node *)makedfa(strnode(r), 0));
			else
				l = op3(op, (Node *)1, l, r);
		}
	}else if(yypeek() == IN){
		yyget();
		v = parse_varname();
		if(v == NULL)
			return NULL;
		l = op2(INTEST, l, makearr(v));
	}else if(yypeek() == '|'){
		yyget();
		if(yypeek() != GETLINE){
			synerr = 1;
			return NULL;
		}
		yyget();
		if(can_start_var(yypeek())){
			v = parse_var();
			if(v == NULL)
				return NULL;
			l = op3(GETLINE, v, itonp('|'), l);
		}else
			l = op3(GETLINE, NIL, itonp('|'), l);
	}
	while(can_start_term(yypeek())){
		r = parse_term();
		if(r == NULL)
			return NULL;
		l = op2(CAT, l, r);
	}
	return l;
}

/* ppattern: pbor (print-arg patterns: no ASGNOP/cond/comparison/bar) */
static Node *
parse_ppattern(void)
{
	return parse_pbor();
}

static Node *
parse_pbor(void)
{
	Node *l = parse_pand(), *r;

	if(l == NULL)
		return NULL;
	for(;;){
		if(yypeek() != BOR)
			return l;
		yyget();
		skip_nl();
		r = parse_pand();
		if(r == NULL)
			return NULL;
		l = op2(BOR, notnull(l), notnull(r));
	}
}

static Node *
parse_pand(void)
{
	Node *l = parse_ppmatch(), *r;

	if(l == NULL)
		return NULL;
	for(;;){
		if(yypeek() != AND)
			return l;
		yyget();
		skip_nl();
		r = parse_ppmatch();
		if(r == NULL)
			return NULL;
		l = op2(AND, notnull(l), notnull(r));
	}
}

/* ppattern MATCHOP/IN then CAT (no bar-GETLINE, no comparisons) */
static Node *
parse_ppmatch(void)
{
	Node *l = parse_operand(), *r, *v;
	int op;
	char *s;

	if(l == NULL)
		return NULL;
	if(yypeek() == MATCHOP){
		op = peekval()->i;
		yyget();
		if(yypeek() == '/'){
			yyget();
			startreg();
			if(yypeek() != REGEXPR){
				synerr = 1;
				return NULL;
			}
			s = peekval()->s;
			yyget();
			if(yypeek() != '/'){
				synerr = 1;
				return NULL;
			}
			yyget();
			l = op3(op, NIL, l, (Node *)makedfa(s, 0));
		}else{
			r = parse_cat();
			if(r == NULL)
				return NULL;
			if(constnode(r))
				l = op3(op, NIL, l,
				    (Node *)makedfa(strnode(r), 0));
			else
				l = op3(op, (Node *)1, l, r);
		}
	}else if(yypeek() == IN){
		yyget();
		v = parse_varname();
		if(v == NULL)
			return NULL;
		l = op2(INTEST, l, makearr(v));
	}
	while(can_start_term(yypeek())){
		r = parse_term();
		if(r == NULL)
			return NULL;
		l = op2(CAT, l, r);
	}
	return l;
}

/* operand: re | cat */
static Node *
parse_operand(void)
{
	if(yypeek() == '/')
		return parse_re();
	return parse_cat();
}

/* juxtaposition: pattern term %prec CAT */
static Node *
parse_cat(void)
{
	Node *l = parse_term(), *r;

	if(l == NULL)
		return NULL;
	while(can_start_term(yypeek())){
		r = parse_term();
		if(r == NULL)
			return NULL;
		l = op2(CAT, l, r);
	}
	return l;
}

/* re: reg_expr | NOT re */
static Node *
parse_re(void)
{
	char *s = parse_reg_expr();

	if(s == NULL)
		return NULL;
	return op3(MATCH, NIL, rectonode(), (Node *)makedfa(s, 0));
}

/* reg_expr: '/' startreg REGEXPR '/' */
static char *
parse_reg_expr(void)
{
	char *s;

	if(yypeek() != '/'){
		synerr = 1;
		return NULL;
	}
	yyget();
	startreg();
	if(yypeek() != REGEXPR){
		synerr = 1;
		return NULL;
	}
	s = peekval()->s;
	yyget();
	if(yypeek() != '/'){
		synerr = 1;
		return NULL;
	}
	yyget();
	return s;
}

/* ---- term (arithmetic) ---- */
static Node *
parse_term(void)
{
	return parse_term_add();
}

static Node *
parse_term_add(void)
{
	Node *l = parse_term_mul(), *r;
	int op;

	if(l == NULL)
		return NULL;
	for(;;){
		op = yypeek();
		if(op != '+' && op != '-')
			return l;
		yyget();
		r = parse_term_mul();
		if(r == NULL)
			return NULL;
		l = op2(op == '+' ? ADD : MINUS, l, r);
	}
}

static Node *
parse_term_mul(void)
{
	Node *l = parse_term_pow(), *r;
	int op;

	if(l == NULL)
		return NULL;
	for(;;){
		op = yypeek();
		if(op == '*'){
			yyget();
			r = parse_term_pow();
			if(r == NULL)
				return NULL;
			l = op2(MULT, l, r);
		}else if(op == '/'){
			yyget();
			if(yypeek() == ASGNOP){
				yyget();
				r = parse_term_pow();
				if(r == NULL)
					return NULL;
				l = op2(DIVEQ, l, r);
			}else{
				r = parse_term_pow();
				if(r == NULL)
					return NULL;
				l = op2(DIVIDE, l, r);
			}
		}else if(op == '%'){
			yyget();
			r = parse_term_pow();
			if(r == NULL)
				return NULL;
			l = op2(MOD, l, r);
		}else
			return l;
	}
}

/* POWER (right assoc) */
static Node *
parse_term_pow(void)
{
	Node *l = parse_term_unary(), *r;

	if(l == NULL)
		return NULL;
	if(yypeek() == POWER){
		yyget();
		r = parse_term_pow();
		if(r == NULL)
			return NULL;
		return op2(POWER, l, r);
	}
	return l;
}

/* prefix: -/+ / NOT / CLOSE / DECR/INCR / INDIRECT / GETLINE */
static Node *
parse_term_unary(void)
{
	Node *v, *r;
	int lt;

	if(yypeek() == '-'){
		yyget();
		v = parse_term_pow();
		if(v == NULL)
			return NULL;
		return op1(UMINUS, v);
	}
	if(yypeek() == '+'){
		yyget();
		return parse_term_pow();
	}
	if(yypeek() == NOT){
		yyget();
		if(yypeek() == '/'){
			char *s = parse_reg_expr();

			if(s == NULL)
				return NULL;
			return op1(NOT, notnull(op3(MATCH, NIL,
			    rectonode(), (Node *)makedfa(s, 0))));
		}
		v = parse_term_pow();
		if(v == NULL)
			return NULL;
		return op1(NOT, notnull(v));
	}
	if(yypeek() == CLOSE){
		yyget();
		v = parse_term_add();
		if(v == NULL)
			return NULL;
		return op1(CLOSE, v);
	}
	if(yypeek() == DECR || yypeek() == INCR){
		int op = yypeek();

		yyget();
		v = parse_var();
		if(v == NULL)
			return NULL;
		return op1(op == DECR ? PREDECR : PREINCR, v);
	}
	if(yypeek() == INDIRECT){
		yyget();
		v = parse_term_unary();
		if(v == NULL)
			return NULL;
		return op1(INDIRECT, v);
	}
	if(yypeek() == GETLINE){
		yyget();
		if(yypeek() == LT){
			lt = peekval()->i;
			yyget();
			r = parse_term_add();
			if(r == NULL)
				return NULL;
			return op3(GETLINE, NIL, itonp(lt), r);
		}
		if(can_start_var(yypeek())){
			v = parse_var();
			if(v == NULL)
				return NULL;
			if(yypeek() == LT){
				lt = peekval()->i;
				yyget();
				r = parse_term_add();
				if(r == NULL)
					return NULL;
				return op3(GETLINE, v, itonp(lt), r);
			}
			return op3(GETLINE, v, NIL, NIL);
		}
		return op3(GETLINE, NIL, NIL, NIL);
	}
	return parse_term_postfix();
}

/* postfix INCR/DECR */
static Node *
parse_term_postfix(void)
{
	Node *n = parse_primary();
	int op;

	if(n == NULL)
		return NULL;
	while(yypeek() == INCR || yypeek() == DECR){
		op = yypeek();
		yyget();
		if(!is_var_node(n)){
			synerr = 1;
			return NULL;
		}
		n = op1(op == INCR ? POSTINCR : POSTDECR, n);
	}
	return n;
}

/* primaries: constants, calls, (group|plist), var */
static Node *
parse_primary(void)
{
	int t = yypeek();
	Cell *cp;
	Node *a, *b, *v;
	int sub;

	switch(t){
	case NUMBER:
	case STRING:
		cp = peekval()->cp;
		yyget();
		return celltonode(cp, CCON);
	case '(':
		yyget();
		a = parse_pattern();
		if(a == NULL)
			return NULL;
		if(yypeek() == ','){
			do{
				if(!expect_comma())
					return NULL;
				b = parse_pattern();
				if(b == NULL)
					return NULL;
				a = linkum(a, b);
			}while(yypeek() == ',');
			if(yypeek() != ')'){
				synerr = 1;
				return NULL;
			}
			yyget();
			if(yypeek() == IN){
				yyget();
				v = parse_varname();
				if(v == NULL)
					return NULL;
				return op2(INTEST, a, makearr(v));
			}
			synerr = 1;
			return NULL;
		}
		if(yypeek() != ')'){
			synerr = 1;
			return NULL;
		}
		yyget();
		return a;
	case BLTIN:
	case CALL:
		break;
	default:
		break;
	}
	if(t == BLTIN || t == CALL){
		if(t == BLTIN){
			sub = peekval()->i;
			yyget();
		}else{
			cp = peekval()->cp;
			yyget();
		}
		if(yypeek() == '('){
			yyget();
			if(yypeek() == ')'){
				yyget();
				if(t == BLTIN)
					return op2(BLTIN, itonp(sub),
					    rectonode());
				return op2(CALL, celltonode(cp, CVAR),
				    NIL);
			}
			a = parse_patlist();
			if(a == NULL)
				return NULL;
			if(yypeek() != ')'){
				synerr = 1;
				return NULL;
			}
			yyget();
			if(t == BLTIN)
				return op2(BLTIN, itonp(sub), a);
			return op2(CALL, celltonode(cp, CVAR), a);
		}
		if(t == BLTIN)
			return op2(BLTIN, itonp(sub), rectonode());
		return op2(CALL, celltonode(cp, CVAR), NIL);
	}
	if(can_start_var(t))
		return parse_var();
	synerr = 1;
	return NULL;
}

/* var: varname | varname '[' patlist ']' | IVAR | INDIRECT term */
static Node *
parse_var(void)
{
	int t = yypeek();
	Cell *cp;
	Node *v, *l;

	if(t == IVAR){
		cp = peekval()->cp;
		yyget();
		return op1(INDIRECT, celltonode(cp, CVAR));
	}
	if(t == INDIRECT){
		yyget();
		v = parse_term_unary();
		if(v == NULL)
			return NULL;
		return op1(INDIRECT, v);
	}
	if(t == VAR || t == ARG || t == VARNF){
		v = parse_varname();
		if(v == NULL)
			return NULL;
		if(yypeek() == '['){
			yyget();
			l = parse_patlist();
			if(l == NULL)
				return NULL;
			if(yypeek() != ']'){
				synerr = 1;
				return NULL;
			}
			yyget();
			return op2(ARRAY, makearr(v), l);
		}
		return v;
	}
	synerr = 1;
	return NULL;
}

/* varname: VAR | ARG | VARNF */
static Node *
parse_varname(void)
{
	int t = yypeek();
	Cell *cp;
	int i;

	if(t == VAR){
		cp = peekval()->cp;
		yyget();
		return celltonode(cp, CVAR);
	}
	if(t == ARG){
		i = peekval()->i;
		yyget();
		return op1(ARG, itonp(i));
	}
	if(t == VARNF){
		cp = peekval()->cp;
		yyget();
		return op1(VARNF, (Node *)cp);
	}
	synerr = 1;
	return NULL;
}

/* funcname: VAR | CALL (records name, returns cell) */
static Cell *
parse_funcname(void)
{
	int t = yypeek();
	Cell *cp;

	if(t != VAR && t != CALL){
		synerr = 1;
		return NULL;
	}
	cp = peekval()->cp;
	yyget();
	setfname(cp);
	return cp;
}

/* varlist: empty | VAR | varlist comma VAR */
static Node *
parse_varlist(void)
{
	Node *l;
	Cell *cp;

	if(yypeek() == ')'){
		arglist = NIL;
		return NIL;
	}
	if(yypeek() != VAR){
		synerr = 1;
		return NULL;
	}
	cp = peekval()->cp;
	yyget();
	l = celltonode(cp, CVAR);
	arglist = l;
	while(yypeek() == ','){
		if(!expect_comma())
			return NULL;
		if(yypeek() != VAR){
			synerr = 1;
			return NULL;
		}
		cp = peekval()->cp;
		yyget();
		checkdup(l, cp);
		l = linkum(l, celltonode(cp, CVAR));
		arglist = l;
	}
	return l;
}

/* patlist: pattern | patlist comma pattern */
static Node *
parse_patlist(void)
{
	Node *l = parse_pattern(), *r;

	if(l == NULL)
		return NULL;
	while(yypeek() == ','){
		if(!expect_comma())
			return NULL;
		r = parse_pattern();
		if(r == NULL)
			return NULL;
		l = linkum(l, r);
	}
	return l;
}

/* pplist: ppattern | pplist comma ppattern */
static Node *
parse_pplist(void)
{
	Node *l = parse_ppattern(), *r;

	if(l == NULL)
		return NULL;
	while(yypeek() == ','){
		if(!expect_comma())
			return NULL;
		r = parse_ppattern();
		if(r == NULL)
			return NULL;
		l = linkum(l, r);
	}
	return l;
}

/* prarg: empty | pplist | '(' plist ')' (covered via pplist) */
static Node *
parse_prarg(void)
{
	int t = yypeek();

	if(t == '/' || can_start_term(t))
		return parse_pplist();
	return rectonode();
}

/* opt_simple_stmt: empty | simple_stmt */
static Node *
parse_opt_simple_stmt(void)
{
	int t = yypeek();

	if(t == ';' || t == ')')
		return NIL;
	return parse_simple_stmt();
}

/* simple_stmt: print-forms | DELETE | pattern | error */
static Node *
parse_simple_stmt(void)
{
	int t = yypeek(), op, op2v;
	Node *a, *b, *v;

	if(t == PRINT || t == PRINTF){
		op = t;
		yyget();
		a = parse_prarg();
		if(a == NULL)
			return NULL;
		if(yypeek() == '|'){
			yyget();
			b = parse_term();
			if(b == NULL)
				return NULL;
			if(safe)
				SYNTAX("print | is unsafe");
			return stat3(op, a, itonp('|'), b);
		}
		if(yypeek() == APPEND){
			op2v = peekval()->i;
			yyget();
			b = parse_term();
			if(b == NULL)
				return NULL;
			if(safe)
				SYNTAX("print >> is unsafe");
			return stat3(op, a, itonp(op2v), b);
		}
		if(yypeek() == GT){
			op2v = peekval()->i;
			yyget();
			b = parse_term();
			if(b == NULL)
				return NULL;
			if(safe)
				SYNTAX("print > is unsafe");
			return stat3(op, a, itonp(op2v), b);
		}
		return stat3(op, a, NIL, NIL);
	}
	if(t == DELETE){
		yyget();
		v = parse_varname();
		if(v == NULL)
			return NULL;
		if(yypeek() == '['){
			yyget();
			a = parse_patlist();
			if(a == NULL)
				return NULL;
			if(yypeek() != ']'){
				synerr = 1;
				return NULL;
			}
			yyget();
			return stat2(DELETE, makearr(v), a);
		}
		return stat2(DELETE, makearr(v), NIL);
	}
	a = parse_pattern();
	if(a == NULL)
		return NULL;
	return exptostat(a);
}

/* st: nl | ';' opt_nl (return ignored by callers) */
static int
parse_st(void)
{
	if(yypeek() == NL){
		skip_nl();
		return 1;
	}
	if(yypeek() == ';'){
		yyget();
		skip_nl();
		return 1;
	}
	synerr = 1;
	return 0;
}

/* stmt: control forms | simple_stmt st | ';' */
static Node *
parse_stmt(void)
{
	int t = yypeek(), op;
	Node *a, *b, *c;

	switch(t){
	case BREAK:
	case CONTINUE:
		op = t;
		yyget();
		if(!inloop)
			SYNTAX(op == BREAK
			    ? "break illegal outside of loops"
			    : "continue illegal outside of loops");
		if(!parse_st())
			return NULL;
		return stat1(op, NIL);
	case DO:
		yyget();
		inloop++;
		b = parse_stmt();
		if(b == NULL && synerr)
			return NULL;
		inloop--;
		if(yypeek() != WHILE){
			synerr = 1;
			return NULL;
		}
		yyget();
		if(!expect_lparen())
			return NULL;
		c = parse_pattern();
		if(c == NULL)
			return NULL;
		if(!expect_rparen())
			return NULL;
		if(!parse_st())
			return NULL;
		return stat2(DO, b, notnull(c));
	case EXIT:
		yyget();
		if(yypeek() == NL || yypeek() == ';'){
			if(!parse_st())
				return NULL;
			return stat1(EXIT, NIL);
		}
		a = parse_pattern();
		if(a == NULL)
			return NULL;
		if(!parse_st())
			return NULL;
		return stat1(EXIT, a);
	case FOR:
		return parse_for();
	case IF:
		yyget();
		if(!expect_lparen())
			return NULL;
		a = parse_pattern();
		if(a == NULL)
			return NULL;
		if(!expect_rparen())
			return NULL;
		a = notnull(a);
		b = parse_stmt();
		if(b == NULL && synerr)
			return NULL;
		if(yypeek() == ELSE){
			yyget();
			skip_nl();
			c = parse_stmt();
			if(c == NULL && synerr)
				return NULL;
			return stat3(IF, a, b, c);
		}
		return stat3(IF, a, b, NIL);
	case '{':
		if(!expect_lbrace())
			return NULL;
		a = parse_stmtlist();
		if(synerr)
			return NULL;
		if(!expect_rbrace())
			return NULL;
		return a;
	case NEXT:
	case NEXTFILE:
		op = t;
		yyget();
		if(infunc)
			SYNTAX(op == NEXT
			    ? "next is illegal inside a function"
			    : "nextfile is illegal inside a function");
		if(!parse_st())
			return NULL;
		return stat1(op, NIL);
	case RETURN:
		yyget();
		if(yypeek() == NL || yypeek() == ';'){
			if(!parse_st())
				return NULL;
			return stat1(RETURN, NIL);
		}
		a = parse_pattern();
		if(a == NULL)
			return NULL;
		if(!parse_st())
			return NULL;
		return stat1(RETURN, a);
	case WHILE:
		yyget();
		if(!expect_lparen())
			return NULL;
		a = parse_pattern();
		if(a == NULL)
			return NULL;
		if(!expect_rparen())
			return NULL;
		a = notnull(a);
		inloop++;
		b = parse_stmt();
		if(b == NULL && synerr)
			return NULL;
		inloop--;
		return stat2(WHILE, a, b);
	case ';':
		yyget();
		skip_nl();
		return NIL;
	default:
		break;
	}
	if(t == PRINT || t == PRINTF || t == DELETE || t == '/'
	|| can_start_term(t)){
		a = parse_simple_stmt();
		if(a == NULL)
			return NULL;
		if(!parse_st())
			return NULL;
		return a;
	}
	synerr = 1;
	return NULL;
}

/* stmtlist: stmt | stmtlist stmt */
static Node *
parse_stmtlist(void)
{
	Node *t = NIL, *s;

	for(;;){
		if(yypeek() == 0 || yypeek() == '}')
			break;
		s = parse_stmt();
		if(s == NULL && synerr){
			yyerror("illegal statement");
			sync_stmt();
			synerr = 0;
			continue;
		}
		t = linkum(t, s);
	}
	return t;
}

/* for: C-style (2 forms) | for-in (detected via 2-token lookahead) */
static Node *
parse_for(void)
{
	Node *s1, *s2, *cond, *body, *v1, *v2;
	int t1;

	yyget();
	if(!expect_lparen())
		return NULL;
	t1 = yypeek();
	if((t1 == VAR || t1 == ARG || t1 == VARNF) && yypeek2() == IN){
		v1 = parse_varname();
		if(v1 == NULL)
			return NULL;
		yyget();
		v2 = parse_varname();
		if(v2 == NULL)
			return NULL;
		if(!expect_rparen())
			return NULL;
		inloop++;
		body = parse_stmt();
		if(body == NULL && synerr)
			return NULL;
		inloop--;
		return stat3(IN, v1, makearr(v2), body);
	}
	s1 = parse_opt_simple_stmt();
	if(s1 == NULL && synerr)
		return NULL;
	if(yypeek() != ';'){
		synerr = 1;
		return NULL;
	}
	yyget();
	if(yypeek() == ';'){
		yyget();
		skip_nl();
		s2 = parse_opt_simple_stmt();
		if(s2 == NULL && synerr)
			return NULL;
		if(!expect_rparen())
			return NULL;
		inloop++;
		body = parse_stmt();
		if(body == NULL && synerr)
			return NULL;
		inloop--;
		return stat4(FOR, s1, NIL, s2, body);
	}
	skip_nl();
	cond = parse_pattern();
	if(cond == NULL)
		return NULL;
	if(yypeek() != ';'){
		synerr = 1;
		return NULL;
	}
	yyget();
	skip_nl();
	s2 = parse_opt_simple_stmt();
	if(s2 == NULL && synerr)
		return NULL;
	if(!expect_rparen())
		return NULL;
	inloop++;
	body = parse_stmt();
	if(body == NULL && synerr)
		return NULL;
	inloop--;
	return stat4(FOR, s1, notnull(cond), s2, body);
}

/* ---- helpers copied from awkgram.y epilogue ---- */
void
setfname(Cell *p)
{
	if(isarr(p))
		SYNTAX("%s is an array, not a function", p->nval);
	else if(isfcn(p))
		SYNTAX("you can't define function %s more than once",
		    p->nval);
	curfname = p->nval;
}

int
constnode(Node *p)
{
	return isvalue(p) && ((Cell *)(p->narg[0]))->csub == CCON;
}

char *
strnode(Node *p)
{
	return ((Cell *)(p->narg[0]))->sval;
}

Node *
notnull(Node *n)
{
	switch(n->nobj){
	case LE: case LT: case EQ: case NE: case GT: case GE:
	case BOR: case AND: case NOT:
		return n;
	default:
		return op2(NE, n, nullnode);
	}
}

/* check if name already in list */
void
checkdup(Node *vl, Cell *cp)
{
	char *s = cp->nval;

	for(; vl; vl = vl->nnext){
		if(strcmp(s, ((Cell *)(vl->narg[0]))->nval) == 0){
			SYNTAX("duplicate argument %s", s);
			break;
		}
	}
}
