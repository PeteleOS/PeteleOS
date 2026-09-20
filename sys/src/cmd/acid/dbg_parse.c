/*
 * Hand-written recursive-descent replacement for dbg.y (LALR).
 *
 * Original yacc grammar (precedence low -> high):
 *	%left ';'
 *	%right '='
 *	%left Tfmt
 *	%left Toror
 *	%left Tandand
 *	%left '|'
 *	%left '^'
 *	%left '&'
 *	%left Teq Tneq
 *	%left '<' '>' Tleq Tgeq
 *	%left Tlsh Trsh
 *	%left '+' '-'
 *	%left '*' '/' '%'
 *	%right Tdec Tinc Tindir '.' '[' '('
 *	prog: | prog bigstmnt
 *	bigstmnt: stmnt {execute}
 *	    | Tfn Tid '(' args ')' zsemi '{' slist '}' {define}
 *	    | Tfn Tid {clear}
 *	    | Tcomplex name '{' members '}' ';' {defcomplex}
 *	stmnt: zexpr ';' | '{' slist '}' | Tif ... | Tloop ... |
 *	    Twhile ... | Tret ... | Tlocal ... | Tcomplex ...
 *	expr: castexpr | binary ('*','/','%','+','-',Tlsh,Trsh,
 *	    '<','>',Tleq,Tgeq,Teq,Tneq,'&','^','|',Tandand,Toror,
 *	    '=') | expr Tfmt
 *	castexpr: monexpr | '(' Tid ')' monexpr
 *	monexpr: term | prefix ('*','@','+','-',Tdec,Tinc,Thead,
 *	    Ttail,Tappend, Tdelete, '!', '~', Teval)
 *	term: '(' expr ')' | '{' args '}' | term '[' expr ']' |
 *	    term Tdec/Tinc | term '.' Tid | term Tindir Tid |
 *	    name '(' args ')' | Tbuiltin name '(' args ')' |
 *	    name | Tconst | Tfconst | Tstring | Twhat zname
 *	name: Tid | Tid ':' name
 *	args: zexpr | args ',' zexpr
 *
 * Precedence-map (hand parser, low -> high):
 *	level 1: '=' (right, OASGN)
 *	level 2: Tfmt (left, OFMT)
 *	level 3: Toror (left, OCOR)
 *	level 4: Tandand (left, OCAND)
 *	level 5: '|' (left, OLOR)
 *	level 6: '^' (left, OXOR)
 *	level 7: '&' (left, OLAND)
 *	level 8: Teq/Tneq (left, OEQ/ONEQ)
 *	level 9: '<'/'>'/Tleq/Tgeq (left, OLT/OGT/OLEQ/OGEQ)
 *	level 10: Tlsh/Trsh (left, OLSH/ORSH)
 *	level 11: '+'/'-' (left, OADD/OSUB)
 *	level 12: '*'/‘/'/'%' (left, OMUL/ODIV/OMOD)
 *	level 13: cast (right, OCAST), prefix monexpr (right),
 *	           postfix term ('[', '.', Tindir, Tdec, Tinc, '(' call)
 *	Call chain: parse_expr -> parse_assign -> parse_fmt ->
 *	  parse_oror -> parse_andand -> parse_bor -> parse_bxor ->
 *	  parse_band -> parse_eq -> parse_rel -> parse_shift ->
 *	  parse_add -> parse_mul -> parse_castexpr -> parse_monexpr ->
 *	  parse_term -> parse_name/parse_args.
 *	Left recursion replaced by loops (prog, members, slist,
 *	idlist, args, binary levels: while(peek==op) consume).
 *
 * Error-recovery-map: original had no `error' production; syntax
 *	error called yyerror() (print "%L: ...") and yacc aborted
 *	(ret1). Hand parser calls yyerror() at the failure point,
 *	then skips to sync (';' or '}' or Eof) and continues with
 *	the next bigstmnt/slist, preserving diagnostics while
 *	allowing interactive use to survive one bad line.
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>
#define Extern extern
#include "acid.h"
#include "dbg_parse.h"

YYSTYPE yylval;
/* 2-token lookahead over yylex() */
static int nla;
static int latok[2];
static YYSTYPE laval[2];

static void
lafill(int n)
{
	while(nla <= n){
		latok[nla] = yylex();
		laval[nla] = yylval;
		nla++;
	}
}

static int
yypeek(int n)
{
	lafill(n);
	return latok[n];
}

static int
yyget(void)
{
	int t;

	lafill(0);
	t = latok[0];
	yylval = laval[0];
	if(nla == 2){
		latok[0] = latok[1];
		laval[0] = laval[1];
		nla = 1;
	}else
		nla = 0;
	return t;
}

static void
yyclearla(void)
{
	nla = 0;
}

static void
skiptosync(void)
{
	int t;

	for(;;){
		t = yypeek(0);
		if(t == Eof || t == ';' || t == '}')
			break;
		yyget();
	}
}

/* forward decls */
static void parse_prog(void);
static void parse_bigstmnt(void);
static void parse_zsemi(void);
static Node *parse_members(void);
static Node *parse_mname(void);
static Node *parse_member(void);
static Lsym *parse_zname(void);
static Node *parse_slist(void);
static Node *parse_stmnt(void);
static Node *parse_idlist(void);
static Node *parse_zexpr(void);
static Node *parse_expr(void);
static Node *parse_assign(void);
static Node *parse_fmt(void);
static Node *parse_oror(void);
static Node *parse_andand(void);
static Node *parse_bor(void);
static Node *parse_bxor(void);
static Node *parse_band(void);
static Node *parse_eq(void);
static Node *parse_rel(void);
static Node *parse_shift(void);
static Node *parse_add(void);
static Node *parse_mul(void);
static Node *parse_castexpr(void);
static Node *parse_monexpr(void);
static Node *parse_term(void);
static Node *parse_name(void);
static Node *parse_args(void);

int
yyparse(void)
{
	yyclearla();
	parse_prog();
	return 0;
}

static void
parse_prog(void)
{
	int t;

	for(;;){
		t = yypeek(0);
		if(t == Eof)
			break;
		parse_bigstmnt();
	}
}

static void
parse_bigstmnt(void)
{
	int t;
	Lsym *fn;
	Node *a, *s;

	t = yypeek(0);
	if(t == Tfn){
		yyget();
		if(yypeek(0) != Tid){
			yyerror("expected name after defn");
			skiptosync();
			return;
		}
		yyget();
		fn = yylval.sym;
		if(yypeek(0) == '('){
			yyget();
			a = parse_args();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptosync();
				return;
			}
			parse_zsemi();
			if(yyget() != '{'){
				yyerror("expected '{'");
				skiptosync();
				return;
			}
			s = parse_slist();
			if(yyget() != '}'){
				yyerror("expected '}'");
				skiptosync();
				return;
			}
			fn->proc = an(OLIST, a, s);
			return;
		}
		fn->proc = nil;
		return;
	}
	if(t == Tcomplex){
		Node *nm, *m;
		/* Lookahead: Tcomplex name '{' ... vs Tcomplex Tid name ';'.
		 * Parse name first, then branch on '{'.
		 * parse_name consumes Tid (':' name)?, so save/restore
		 * is not needed: we parse and keep.
		 */
		yyget();
		/* name must start with Tid */
		if(yypeek(0) != Tid){
			yyerror("expected name after complex");
			skiptosync();
			return;
		}
		nm = parse_name();
		if(yypeek(0) == '{'){
			yyget();
			m = parse_members();
			if(yyget() != '}'){
				yyerror("expected '}'");
				skiptosync();
				return;
			}
			if(yyget() != ';'){
				yyerror("expected ';'");
				skiptosync();
				return;
			}
			defcomplex(nm, m);
			return;
		}
		/* otherwise it is a stmnt: Tcomplex Tid name ';'.
		 * We already consumed Tcomplex and one name (the type).
		 * Need second name and ';'.
		 */
		if(yypeek(0) != Tid){
			/* Could be Tid ':' ... already consumed? Actually
			 * parse_name above consumed the type name; next
			 * must be the variable name.
			 */
			yyerror("expected name after complex type");
			skiptosync();
			return;
		}
		a = parse_name();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosync();
			return;
		}
		s = an(OCOMPLEX, a, ZN);
		/* $$->sym = $2 where $2 is the Tid after Tcomplex.
		 * Our nm is the type name node; its sym is the type.
		 */
		s->sym = nm->sym;
		/* make stmnt a root and execute like other stmnts */
		mkvar("_thiscmd")->proc = s;
		execute(s);
		mkvar("_thiscmd")->proc = nil;
		gc();
		if(interactive)
			Bprint(bout, "acid: ");
		return;
	}
	/* otherwise stmnt with execution */
	s = parse_stmnt();
	/* parse_stmnt may return nil on empty/error; still execute? */
	mkvar("_thiscmd")->proc = s;
	execute(s);
	mkvar("_thiscmd")->proc = nil;
	gc();
	if(interactive)
		Bprint(bout, "acid: ");
}

static void
parse_zsemi(void)
{
	while(yypeek(0) == ';')
		yyget();
}

static Node*
parse_members(void)
{
	Node *m, *n;

	m = parse_member();
	for(;;){
		int t = yypeek(0);
		/* member starts with Tconst, Tid, or '{' */
		if(t != Tconst && t != Tid && t != '{')
			break;
		/* Need to distinguish: if next could start a member?
		 * member: Tconst ... | mname ... | '{' ... .
		 * mname is Tid. So Tid always starts a member.
		 * Tconst always starts a member. '{' starts a member.
		 * But '}' ends members. So break on '}'.
		 */
		n = parse_member();
		m = an(OLIST, m, n);
	}
	return m;
}

static Node*
parse_mname(void)
{
	Node *n;

	if(yyget() != Tid){
		yyerror("expected member name");
		return an(ONAME, ZN, ZN);
	}
	n = an(ONAME, ZN, ZN);
	n->sym = yylval.sym;
	return n;
}

static Node*
parse_member(void)
{
	int t;
	Node *a, *b, *m;

	t = yypeek(0);
	if(t == '{'){
		yyget();
		a = parse_members();
		if(yyget() != '}'){
			yyerror("expected '}'");
			skiptosync();
		}
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosync();
		}
		return an(OCTRUCT, a, ZN);
	}
	/* Tconst Tconst mname ';' | Tconst mname Tconst mname ';'
	 * | mname Tconst mname ';'
	 * Distinguish by peeking.
	 */
	if(t == Tconst){
		uvlong f1, v;
		yyget();
		f1 = yylval.ival;
		t = yypeek(0);
		if(t == Tconst){
			yyget();
			v = yylval.ival;
			m = parse_mname();
			if(yyget() != ';'){
				yyerror("expected ';'");
				skiptosync();
			}
			m->ival = v;
			m->fmt = f1;
			return m;
		}
		if(t == Tid){
			/* Tconst mname Tconst mname ';' */
			a = parse_mname();
			if(yypeek(0) != Tconst){
				yyerror("expected constant");
				skiptosync();
				return a;
			}
			yyget();
			v = yylval.ival;
			b = parse_mname();
			if(yyget() != ';'){
				yyerror("expected ';'");
				skiptosync();
			}
			b->ival = v;
			b->fmt = f1;
			b->right = a;
			return b;
		}
		yyerror("expected member name or constant");
		skiptosync();
		return an(ONAME, ZN, ZN);
	}
	/* mname Tconst mname ';' */
	if(t == Tid){
		uvlong v;
		a = parse_mname();
		if(yypeek(0) != Tconst){
			yyerror("expected constant after member name");
			skiptosync();
			return a;
		}
		yyget();
		v = yylval.ival;
		b = parse_mname();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosync();
		}
		b->ival = v;
		b->left = a;
		return b;
	}
	yyerror("expected member");
	skiptosync();
	return an(ONAME, ZN, ZN);
}

static Lsym*
parse_zname(void)
{
	if(yypeek(0) == Tid){
		yyget();
		return yylval.sym;
	}
	return 0;
}

static Node*
parse_slist(void)
{
	Node *s, *n;

	s = parse_stmnt();
	for(;;){
		int t = yypeek(0);
		if(t == '}' || t == Eof)
			break;
		n = parse_stmnt();
		s = an(OLIST, s, n);
	}
	return s;
}

static Node*
parse_stmnt(void)
{
	int t;
	Node *a, *b, *c;

	t = yypeek(0);
	if(t == '{'){
		yyget();
		a = parse_slist();
		if(yyget() != '}'){
			yyerror("expected '}'");
			skiptosync();
		}
		return a;
	}
	if(t == Tif){
		yyget();
		a = parse_expr();
		if(yyget() != Tthen){
			yyerror("expected then");
			skiptosync();
			return an(OIF, a, ZN);
		}
		b = parse_stmnt();
		if(yypeek(0) == Telse){
			yyget();
			c = parse_stmnt();
			return an(OIF, a, an(OELSE, b, c));
		}
		return an(OIF, a, b);
	}
	if(t == Tloop){
		yyget();
		a = parse_expr();
		if(yyget() != ','){
			yyerror("expected ','");
			skiptosync();
			return an(ODO, a, ZN);
		}
		b = parse_expr();
		if(yyget() != Tdo){
			yyerror("expected do");
			skiptosync();
			return an(ODO, an(OLIST, a, b), ZN);
		}
		c = parse_stmnt();
		return an(ODO, an(OLIST, a, b), c);
	}
	if(t == Twhile){
		yyget();
		a = parse_expr();
		if(yyget() != Tdo){
			yyerror("expected do");
			skiptosync();
			return an(OWHILE, a, ZN);
		}
		b = parse_stmnt();
		return an(OWHILE, a, b);
	}
	if(t == Tret){
		yyget();
		a = parse_expr();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosync();
		}
		return an(ORET, a, ZN);
	}
	if(t == Tlocal){
		yyget();
		a = parse_idlist();
		return an(OLOCAL, a, ZN);
	}
	if(t == Tcomplex){
		Lsym *ty;
		yyget();
		if(yypeek(0) != Tid){
			yyerror("expected type name");
			skiptosync();
			return an(OCOMPLEX, ZN, ZN);
		}
		yyget();
		ty = yylval.sym;
		a = parse_name();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosync();
		}
		b = an(OCOMPLEX, a, ZN);
		b->sym = ty;
		return b;
	}
	/* zexpr ';' */
	a = parse_zexpr();
	if(yyget() != ';'){
		yyerror("expected ';'");
		skiptosync();
	}
	return a;
}

static Node*
parse_idlist(void)
{
	Node *a, *n;
	Lsym *s;

	if(yyget() != Tid){
		yyerror("expected identifier");
		return an(ONAME, ZN, ZN);
	}
	s = yylval.sym;
	a = an(ONAME, ZN, ZN);
	a->sym = s;
	while(yypeek(0) == ','){
		yyget();
		if(yyget() != Tid){
			yyerror("expected identifier");
			break;
		}
		s = yylval.sym;
		n = an(ONAME, a, ZN);
		n->sym = s;
		a = n;
	}
	return a;
}

static Node*
parse_zexpr(void)
{
	int t = yypeek(0);

	if(t == ';' || t == ',' || t == ')' || t == '}' || t == Eof)
		return 0;
	return parse_expr();
}

static Node*
parse_expr(void)
{
	return parse_assign();
}

static Node*
parse_assign(void)
{
	Node *l, *r;

	l = parse_fmt();
	if(yypeek(0) == '='){
		yyget();
		r = parse_assign();
		return an(OASGN, l, r);
	}
	return l;
}

static Node*
parse_fmt(void)
{
	Node *l;

	l = parse_oror();
	while(yypeek(0) == Tfmt){
		yyget();
		l = an(OFMT, l, con(yylval.ival));
	}
	return l;
}

static Node*
parse_oror(void)
{
	Node *l, *r;

	l = parse_andand();
	while(yypeek(0) == Toror){
		yyget();
		r = parse_andand();
		l = an(OCOR, l, r);
	}
	return l;
}

static Node*
parse_andand(void)
{
	Node *l, *r;

	l = parse_bor();
	while(yypeek(0) == Tandand){
		yyget();
		r = parse_bor();
		l = an(OCAND, l, r);
	}
	return l;
}

static Node*
parse_bor(void)
{
	Node *l, *r;

	l = parse_bxor();
	while(yypeek(0) == '|'){
		yyget();
		r = parse_bxor();
		l = an(OLOR, l, r);
	}
	return l;
}

static Node*
parse_bxor(void)
{
	Node *l, *r;

	l = parse_band();
	while(yypeek(0) == '^'){
		yyget();
		r = parse_band();
		l = an(OXOR, l, r);
	}
	return l;
}

static Node*
parse_band(void)
{
	Node *l, *r;

	l = parse_eq();
	while(yypeek(0) == '&'){
		yyget();
		r = parse_eq();
		l = an(OLAND, l, r);
	}
	return l;
}

static Node*
parse_eq(void)
{
	Node *l, *r;
	int t;

	l = parse_rel();
	for(;;){
		t = yypeek(0);
		if(t != Teq && t != Tneq)
			break;
		yyget();
		r = parse_rel();
		if(t == Teq)
			l = an(OEQ, l, r);
		else
			l = an(ONEQ, l, r);
	}
	return l;
}

static Node*
parse_rel(void)
{
	Node *l, *r;
	int t;

	l = parse_shift();
	for(;;){
		t = yypeek(0);
		if(t != '<' && t != '>' && t != Tleq && t != Tgeq)
			break;
		yyget();
		r = parse_shift();
		if(t == '<')
			l = an(OLT, l, r);
		else if(t == '>')
			l = an(OGT, l, r);
		else if(t == Tleq)
			l = an(OLEQ, l, r);
		else
			l = an(OGEQ, l, r);
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
		t = yypeek(0);
		if(t != Tlsh && t != Trsh)
			break;
		yyget();
		r = parse_add();
		if(t == Tlsh)
			l = an(OLSH, l, r);
		else
			l = an(ORSH, l, r);
	}
	return l;
}

static Node*
parse_add(void)
{
	Node *l, *r;
	int t;

	l = parse_mul();
	for(;;){
		t = yypeek(0);
		if(t != '+' && t != '-')
			break;
		yyget();
		r = parse_mul();
		if(t == '+')
			l = an(OADD, l, r);
		else
			l = an(OSUB, l, r);
	}
	return l;
}

static Node*
parse_mul(void)
{
	Node *l, *r;
	int t;

	l = parse_castexpr();
	for(;;){
		t = yypeek(0);
		if(t != '*' && t != '/' && t != '%')
			break;
		yyget();
		r = parse_castexpr();
		if(t == '*')
			l = an(OMUL, l, r);
		else if(t == '/')
			l = an(ODIV, l, r);
		else
			l = an(OMOD, l, r);
	}
	return l;
}

static Node*
parse_castexpr(void)
{
	Node *n;
	Lsym *s;

	if(yypeek(0) == '(' && yypeek(1) == Tid){
		/* Need to distinguish '(' Tid ')' monexpr (cast)
		 * from '(' expr ')' (grouping in term).
		 * If tokens are '(' Tid ')', it is a cast.
		 * Otherwise it is grouping; let monexpr->term handle.
		 * We need 3-token lookahead: '(' Tid ')'.
		 * Our buffer has only 2; fetch third via temporary?
		 * Instead: consume '(' Tid and check for ')'.
		 * If not ')', we must backtrack - but grouping with
		 * single Tid like '(x)' would misparse as cast attempt.
		 * Handle: if '(' Tid ')' then cast, else treat as term.
		 * For '(x)', '(' Tid ')' holds (x is Tid), but '(x)'
		 * as grouping vs cast '(x)expr'? In acid, '(' Tid ')'
		 * followed by monexpr is cast; '(x)' alone (where x is
		 * Tid) followed by operator (e.g., '+', ';', ')') is
		 * grouping. Distinguish by what follows ')'.
		 * Simplest: if after '(' Tid ')' the next token can
		 * start a monexpr and is not a binary operator/terminator,
		 * treat as cast. Binary operators: '*','/','%','+','-',
		 * Tlsh,Trsh,'<','>',Tleq,Tgeq,Teq,Tneq,'&','^','|',
		 * Tandand,Toror,'=',Tfmt,',',';',')',']','}'.
		 * If next is a monexpr starter that is not also a binary
		 * continuation, it is ambiguous. For faithfulness, we
		 * check: if token after ')' is one that cannot continue
		 * a binary expr (e.g., ';', ',', ')', ']', '}', Eof,
		 * Tthen, Tdo, Telse), then it was grouping '(x)'.
		 * Otherwise treat as cast.
		 * To avoid 3-token buffering complexity, we peek by
		 * consuming and pushing back via our 2-slot buffer plus
		 * one extra manual save.
		 */
		int t0, t1;
		YYSTYPE v0, v1;
		/* save lookahead state */
		lafill(1);
		t0 = latok[0]; v0 = laval[0];
		t1 = latok[1]; v1 = laval[1];
		/* consume '(' Tid */
		yyget();
		yyget();
		if(yypeek(0) == ')'){
			int after;
			yyget(); /* ')' */
			after = yypeek(0);
			/* tokens that terminate an expr -> grouping */
			if(after == ';' || after == ',' || after == ')' ||
			   after == ']' || after == '}' || after == Eof ||
			   after == Tthen || after == Tdo || after == Telse ||
			   after == Toror || after == Tandand || after == '|' ||
			   after == '^' || after == '&' || after == Teq ||
			   after == Tneq || after == '<' || after == '>' ||
			   after == Tleq || after == Tgeq || after == Tlsh ||
			   after == Trsh || after == '+' || after == '-' ||
			   after == '*' || after == '/' || after == '%' ||
			   after == '=' || after == Tfmt || after == Tdec ||
			   after == Tinc || after == '.' || after == Tindir ||
			   after == '[' || after == '('){
				/* grouping: '(Tid)' */
				n = an(ONAME, ZN, ZN);
				n->sym = v1.sym;
				return n;
			}
			/* cast */
			s = v1.sym;
			n = parse_monexpr();
			n = an(OCAST, n, ZN);
			n->sym = s;
			return n;
		}
		/* not '(' Tid ')', restore and fall through to monexpr */
		latok[0] = t0; laval[0] = v0;
		latok[1] = t1; laval[1] = v1;
		nla = 2;
	}
	return parse_monexpr();
}

static Node*
parse_monexpr(void)
{
	int t;
	Node *a, *b;

	t = yypeek(0);
	if(t == '*'){
		yyget();
		return an(OINDM, parse_monexpr(), ZN);
	}
	if(t == '@'){
		yyget();
		return an(OINDC, parse_monexpr(), ZN);
	}
	if(t == '+'){
		yyget();
		return an(OADD, parse_monexpr(), ZN);
	}
	if(t == '-'){
		yyget();
		a = con(0);
		return an(OSUB, a, parse_monexpr());
	}
	if(t == Tdec){
		yyget();
		return an(OEDEC, parse_monexpr(), ZN);
	}
	if(t == Tinc){
		yyget();
		return an(OEINC, parse_monexpr(), ZN);
	}
	if(t == Thead){
		yyget();
		return an(OHEAD, parse_monexpr(), ZN);
	}
	if(t == Ttail){
		yyget();
		return an(OTAIL, parse_monexpr(), ZN);
	}
	if(t == Tappend){
		yyget();
		a = parse_monexpr();
		if(yyget() != ','){
			yyerror("expected ','");
			skiptosync();
			return a;
		}
		b = parse_monexpr();
		return an(OAPPEND, a, b);
	}
	if(t == Tdelete){
		yyget();
		a = parse_monexpr();
		if(yyget() != ','){
			yyerror("expected ','");
			skiptosync();
			return a;
		}
		b = parse_monexpr();
		return an(ODELETE, a, b);
	}
	if(t == '!'){
		yyget();
		return an(ONOT, parse_monexpr(), ZN);
	}
	if(t == '~'){
		yyget();
		return an(OXOR, parse_monexpr(), con(-1));
	}
	if(t == Teval){
		yyget();
		return an(OEVAL, parse_monexpr(), ZN);
	}
	return parse_term();
}

static Node*
parse_term(void)
{
	int t;
	Node *a, *b;

	t = yypeek(0);
	if(t == '('){
		yyget();
		a = parse_expr();
		if(yyget() != ')'){
			yyerror("expected ')'");
			skiptosync();
		}
		goto postfix;
	}
	if(t == '{'){
		yyget();
		a = parse_args();
		if(yyget() != '}'){
			yyerror("expected '}'");
			skiptosync();
		}
		a = an(OCTRUCT, a, ZN);
		goto postfix;
	}
	if(t == Tbuiltin){
		yyget();
		a = parse_name();
		if(yyget() != '('){
			yyerror("expected '('");
			skiptosync();
			return a;
		}
		b = parse_args();
		if(yyget() != ')'){
			yyerror("expected ')'");
			skiptosync();
		}
		a = an(OCALL, a, b);
		a->builtin = 1;
		goto postfix;
	}
	if(t == Tconst){
		yyget();
		a = con(yylval.ival);
		goto postfix;
	}
	if(t == Tfconst){
		yyget();
		a = an(OCONST, ZN, ZN);
		a->type = TFLOAT;
		a->fmt = 'f';
		a->fval = yylval.fval;
		goto postfix;
	}
	if(t == Tstring){
		yyget();
		a = an(OCONST, ZN, ZN);
		a->type = TSTRING;
		a->string = yylval.string;
		a->fmt = 's';
		goto postfix;
	}
	if(t == Twhat){
		Lsym *s;
		yyget();
		s = parse_zname();
		a = an(OWHAT, ZN, ZN);
		a->sym = s;
		goto postfix;
	}
	/* name or name '(' args ')' */
	if(t == Tid){
		a = parse_name();
		if(yypeek(0) == '('){
			yyget();
			b = parse_args();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptosync();
			}
			a = an(OCALL, a, b);
		}
		goto postfix;
	}
	yyerror("expected expression");
	skiptosync();
	return con(0);

postfix:
	for(;;){
		t = yypeek(0);
		if(t == '['){
			yyget();
			b = parse_expr();
			if(yyget() != ']'){
				yyerror("expected ']'");
				skiptosync();
			}
			a = an(OINDEX, a, b);
			continue;
		}
		if(t == Tdec){
			yyget();
			a = an(OPDEC, a, ZN);
			continue;
		}
		if(t == '.'){
			Lsym *s;
			yyget();
			if(yyget() != Tid){
				yyerror("expected field name");
				skiptosync();
				continue;
			}
			s = yylval.sym;
			b = an(ODOT, a, ZN);
			b->sym = s;
			a = b;
			continue;
		}
		if(t == Tindir){
			Lsym *s;
			yyget();
			if(yyget() != Tid){
				yyerror("expected field name");
				skiptosync();
				continue;
			}
			s = yylval.sym;
			b = an(ODOT, an(OINDM, a, ZN), ZN);
			b->sym = s;
			a = b;
			continue;
		}
		if(t == Tinc){
			yyget();
			a = an(OPINC, a, ZN);
			continue;
		}
		break;
	}
	return a;
}

static Node*
parse_name(void)
{
	Node *a, *b;
	Lsym *s;

	if(yyget() != Tid){
		yyerror("expected name");
		return an(ONAME, ZN, ZN);
	}
	s = yylval.sym;
	a = an(ONAME, ZN, ZN);
	a->sym = s;
	if(yypeek(0) == ':'){
		yyget();
		b = parse_name();
		a = an(OFRAME, b, ZN);
		a->sym = s;
	}
	return a;
}

static Node*
parse_args(void)
{
	Node *a, *b;

	a = parse_zexpr();
	while(yypeek(0) == ','){
		yyget();
		b = parse_zexpr();
		a = an(OLIST, a, b);
	}
	return a;
}
