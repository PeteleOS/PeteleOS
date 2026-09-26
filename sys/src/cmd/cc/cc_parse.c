/*
 * Hand-written recursive-descent replacement for cc.y (LALR, 1183 lines).
 *
 * Original yacc precedence (low -> high):
 *	%left ';'
 *	%left ','
 *	%right '=' LPE LME LMLE LDVE LMDE LRSHE LLSHE LANDE LXORE LORE
 *	%right '?' ':'
 *	%left LOROR
 *	%left LANDAND
 *	%left '|'
 *	%left '^'
 *	%left '&'
 *	%left LEQ LNE
 *	%left '<' '>' LLE LGE
 *	%left LLSH LRSH
 *	%left '+' '-'
 *	%left '*' '/' '%'
 *	%right LMM LPP LMG '.' '[' '('
 *
 * Precedence-map (hand parser, low -> high, each level one function):
 *	',' (parse_cexpr, OCOMMA; elist variant OLIST for call args)
 *	'=' + compound (parse_expr/parse_assign, right, OAS/OASADD/...)
 *	'?' ':' (parse_ternary, right, OCOND; middle is cexpr)
 *	LOROR (parse_loror, left, OOROR)
 *	LANDAND (parse_landand, left, OANDAND)
 *	'|' (parse_bor, left, OOR)
 *	'^' (parse_bxor, left, OXOR)
 *	'&' (parse_band, left, OAND)
 *	LEQ/LNE (parse_eq, left, OEQ/ONE)
 *	'<'/'>'/LLE/LGE (parse_rel, left, OLT/OGT/OLE/OGE)
 *	LLSH/LRSH (parse_shift, left, OASHL/OASHR)
 *	'+'/'-' (parse_add, left, OADD/OSUB)
 *	'*'/'/'/'%' (parse_mul, left, OMUL/ODIV/OMOD)
 *	cast/unary/postfix (parse_xuexpr/parse_uexpr/parse_pexpr, right):
 *	  '(' tlist abdecor ')' xuexpr (OCAST),
 *	  '(' tlist abdecor ')' '{' ilist '}' (OSTRUCT extension),
 *	  prefix '*','&','+','-','!','~',LPP,LMM,LSIZEOF,LSIGNOF,
 *	  postfix '(' zelist ')', '[' cexpr ']', '.'/'->' ltag, LPP/LMM.
 *	Call chain: parse_cexpr -> parse_expr -> parse_ternary ->
 *	  parse_loror -> parse_landand -> parse_bor -> parse_bxor ->
 *	  parse_band -> parse_eq -> parse_rel -> parse_shift ->
 *	  parse_add -> parse_mul -> parse_xuexpr -> parse_uexpr ->
 *	  parse_pexpr.
 *	Left recursion replaced by loops:
 *	  prog: prog xdecl -> while(peek()!=EOF) parse_xdecl();
 *	  xdlist/adlist/edlist/pdlist ',' lists -> while loops;
 *	  elist/arglist/slist/members with OLIST -> loops + invert()
 *	  where yacc did.
 *
 * Error-recovery-map: original had one `error' production:
 *	stmnt: error ';' {$$=Z;}
 *	Hand parser: parse_stmnt() on unexpected token or sub-parse
 *	failure calls yyerror("syntax error, ...") then skips to sync
 *	(';' or '}' or EOF), consumes ';' if present and returns Z,
 *	matching yacc's yyerrok behaviour. Other levels propagate Z
 *	up; decl levels skip to ';'/'}'. No longjmp; nerrors drives
 *	"too many errors" via yyerror().
 *
 * Split design in this one file:
 *	(a) decl: parse_xdecl, parse_xdlist, parse_xdecor/2, parse_adecl,
 *	    parse_adlist, parse_pdecl/pdlist, parse_edecl/zedlist/edlist/
 *	    edecor, parse_abdecor/1/2/3, parse_init/qual/qlist/ilist,
 *	    parse_zarglist/arglist, type helpers parse_zctlist/ctlist/
 *	    tlist/types/complex/sbody/enum/spec-bits, parse_name/tag/ltag.
 *	    Mid-rule actions preserved (dodecl/doinit/markdcl/argmark/
 *	    revertdcl/codgen/dotag/sualign/doenum/contig etc.).
 *	(b) stmt: parse_block/slist/labels/label/stmnt/forexpr/ulstmnt,
 *	    parse_zcexpr/zexpr/lexpr/cexpr (cexpr=comma OCOMMA).
 *	(c) expr: parse_* levels above + parse_zelist/elist/string/lstring.
 *	Interface preserved: typedef YYSTYPE (%union), YYSTYPE yylval,
 *	token numbers (enum >255), long yylex(void) from lex.c,
 *	void yyerror(char*,...) from sub.c, int yyparse(void).
 */

#include "cc.h"
#include "cc_parse.h"

/* N-token lookahead over yylex().
 * Must cover the deepest yypeek() used by the parser
 * (parse_arg_one peeks up to index 3 to tell tlist xdecor
 * from tlist abdecor, e.g. `int (*)(Fmt*)`).
 * The old 2-entry buffer overflowed on yypeek(2)/yypeek(3)
 * and corrupted the parse, e.g. "expected ')'" on
 * sys/include/libc.h protoypes with function pointers.
 */
/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef NLA
#define NLA 8
static int nla;
static long latok[NLA];
static YYSTYPE laval[NLA];

static void
lafill(int n)
{
	if(n < 0)
		return;
	if(n >= NLA){
		yyerror("lookahead overflow");
		n = NLA-1;
	}
	while(nla <= n){
		latok[nla] = yylex();
		laval[nla] = yylval;
		nla++;
	}
}

static long
yypeek(int n)
{
	if(n < 0 || n >= NLA){
		yyerror("lookahead overflow");
		return 0;
	}
	lafill(n);
	return latok[n];
}

static long
yyget(void)
{
	long t;
	int i;

	lafill(0);
	t = latok[0];
	yylval = laval[0];
	for(i = 1; i < nla; i++){
		latok[i-1] = latok[i];
		laval[i-1] = laval[i];
	}
	if(nla > 0)
		nla--;
	return t;
}

static void
yyclearla(void)
{
	nla = 0;
}

static void
skiptosemi(void)
{
	long t;

	for(;;){
		t = yypeek(0);
		if(t == -1 || t == 0 || t == ';' || t == '}')
			break;
		yyget();
	}
	if(yypeek(0) == ';')
		yyget();
}

/* forward decls: decl batch */
static void parse_xdecl(void);
static void parse_xdlist_rest(Node *first);
static Node *parse_xdecor_full(void);
static Node *parse_xdecor2base(void);
static Node *parse_tag(void);
static Sym *parse_ltag(void);
static Node *parse_name(void);
static Node *parse_adecl(void);
static Node *parse_adlist_one(void);
static Node *parse_adlist(void);
static void parse_pdecl(void);
static void parse_pdlist(void);
static void parse_edecl(void);
static void parse_edlist(void);
static Node *parse_edecor(void);
static Node *parse_abdecor(void);
static Node *parse_abdecor1(void);
static Node *parse_abdecor2(void);
static Node *parse_abdecor3(void);
static Node *parse_init(void);
static Node *parse_qual(void);
static Node *parse_ilist(void);
static Node *parse_zarglist(void);
static Node *parse_arglist(void);
static Node *parse_arg_one(void);
/* type helpers */
static void parse_zctlist(void);
static void parse_ctlist(void);
static Type *parse_tlist(void);
static void parse_types(Type **pt, int *pc);
static Type *parse_complex(void);
static Type *parse_sbody(void);
static void parse_enumrest(void);
static long specbit(long tok);
static int is_typestart(long t);
static int is_specword(long t);
/* stmt batch */
static Node *parse_block(void);
static Node *parse_slist(void);
static Node *parse_label(void);
static Node *parse_stmnt(void);
static Node *parse_forexpr(void);
static Node *parse_ulstmnt(void);
static Node *parse_zcexpr(void);
static Node *parse_zcexpr_rp(void);
static Node *parse_zexpr(void);
static Node *parse_lexpr(void);
static Node *parse_cexpr(void);
/* expr batch */
static Node *parse_expr(void);
static Node *parse_ternary(void);
static Node *parse_loror(void);
static Node *parse_landand(void);
static Node *parse_bor(void);
static Node *parse_bxor(void);
static Node *parse_band(void);
static Node *parse_eq(void);
static Node *parse_rel(void);
static Node *parse_shift(void);
static Node *parse_add(void);
static Node *parse_mul(void);
static Node *parse_xuexpr(void);
static Node *parse_uexpr(void);
static Node *parse_pexpr(void);
static Node *parse_string(void);
static Node *parse_lstring(void);
static Node *parse_zelist(void);
static Node *parse_elist(void);
static int is_assignop(long t);
static int assignop(long t);

YYSTYPE yylval;

int
yyparse(void)
{
	long t;

	yyclearla();
	for(;;){
		t = yypeek(0);
		if(t == -1 || t == 0)
			break;
		parse_xdecl();
	}
	return 0;
}

/* ---------- (a) decl batch ---------- */

static void
parse_xdecl(void)
{
	Node *x;
	long t;

	parse_zctlist();
	t = yypeek(0);
	/* function definition? zctlist xdecor ... block.
	 * Need to look ahead: xdecor then pdecl/block vs xdlist ';'.
	 * We parse xdecor speculatively: if after xdecor we see
	 * '{' or type-start (pdecl) then function; else xdlist.
	 * Simplest: parse first xdecor, then branch.
	 */
	if(t == ';'){
		yyget();
		dodecl(xdecl, lastclass, lasttype, Z);
		return;
	}
	/* must have declarator */
	x = parse_xdecor_full();
	t = yypeek(0);
	if(t == '{' || is_typestart(t)){
		/* function definition: xdecor pdecl block */
		Node *n, *xd, *blk;
		lastdcl = T;
		firstarg = S;
		dodecl(xdecl, lastclass, lasttype, x);
		if(lastdcl == T || lastdcl->etype != TFUNC){
			diag(x, "not a function");
			lastdcl = types[TFUNC];
		}
		thisfn = lastdcl;
		markdcl();
		firstdcl = dclstack;
		xd = x;
		argmark(xd, 0);
		parse_pdecl();
		argmark(xd, 1);
		blk = parse_block();
		/* block action: revertdcl + codgen */
		n = revertdcl();
		if(n)
			blk = new(OLIST, n, blk);
		if(!debug['a'] && !debug['Z'])
			codgen(blk, xd);
		return;
	}
	/* otherwise xdlist: first already parsed, handle '=' init and ',' loop */
	if(t == '='){
		yyget();
		x = dodecl(xdecl, lastclass, lasttype, x);
		{
			Node *in = parse_init();
			doinit(x->sym, x->type, 0L, in);
		}
	}else{
		dodecl(xdecl, lastclass, lasttype, x);
	}
	parse_xdlist_rest(Z);
	if(yypeek(0) == ';')
		yyget();
	else{
		yyerror("expected ';'");
		skiptosemi();
	}
}

static void
parse_xdlist_rest(Node *first)
{
	Node *x;
	long t;

	USED(first);
	for(;;){
		t = yypeek(0);
		if(t != ',')
			break;
		yyget();
		/* xdlist ',' xdlist: right side can itself be list, but loop covers */
		x = parse_xdecor_full();
		if(yypeek(0) == '='){
			yyget();
			x = dodecl(xdecl, lastclass, lasttype, x);
			{
				Node *in = parse_init();
				doinit(x->sym, x->type, 0L, in);
			}
		}else
			dodecl(xdecl, lastclass, lasttype, x);
	}
}

static Node*
parse_xdecor_full(void)
{
	long g;
	Node *n;

	if(yypeek(0) == '*'){
		yyget();
		g = 0;
		while(yypeek(0)==LCONSTNT || yypeek(0)==LVOLATILE || yypeek(0)==LRESTRICT){
			long b = specbit(yypeek(0));
			if(b == BCONSTNT || b == BVOLATILE)
				g = typebitor(g, b);
			yyget();
		}
		n = parse_xdecor_full();
		{
			Node *m = new(OIND, n, Z);
			m->garb = simpleg(g);
			return m;
		}
	}
	return parse_xdecor2base();
}

static Node*
parse_xdecor2base(void)
{
	Node *n;

	if(yypeek(0) == '(' && yypeek(1) != LNAME && yypeek(1) != LTYPE){
		/* Could be '(' xdecor ')' grouping vs function params?
		 * xdecor2base grouping: '(' xdecor ')'.
		 * But '(' followed by type/ster etc is ambiguous.
		 * Heuristic: if after '(' we see '*' or type-start that
		 * looks like abstract, still try xdecor grouping.
		 * We attempt grouping: '(' xdecor ')' then postfix loop.
		 */
	}
	if(yypeek(0) == LNAME || yypeek(0) == LTYPE){
		n = parse_tag();
		goto postfix;
	}
	if(yypeek(0) == '('){
		yyget();
		/* need to decide: '(' xdecor ')' vs '(' ... ?
		 * In declarator context, '(' xdecor ')' is grouping.
		 * Try parse xdecor; if next is ')' then grouping.
		 */
		n = parse_xdecor_full();
		if(yypeek(0) != ')'){
			yyerror("expected ')'");
			skiptosemi();
			goto postfix;
		}
		yyget();
		goto postfix;
	}
	yyerror("expected declarator");
	skiptosemi();
	return Z;

postfix:
	for(;;){
		if(yypeek(0) == '('){
			Node *a;
			yyget();
			a = parse_zarglist();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptosemi();
			}
			n = new(OFUNC, n, a);
			continue;
		}
		if(yypeek(0) == '['){
			Node *e;
			yyget();
			e = parse_zexpr();
			if(yyget() != ']'){
				yyerror("expected ']'");
				skiptosemi();
			}
			n = new(OARRAY, n, e);
			continue;
		}
		break;
	}
	return n;
}

static Node*
parse_tag(void)
{
	Sym *s;
	Node *n;

	s = parse_ltag();
	n = new(ONAME, Z, Z);
	n->sym = s;
	n->type = s->type;
	n->etype = TVOID;
	if(n->type != T)
		n->etype = n->type->etype;
	n->xoffset = s->offset;
	n->class = s->class;
	return n;
}

static Sym*
parse_ltag(void)
{
	long t = yyget();
	if(t == LNAME || t == LTYPE)
		return yylval.sym;
	yyerror("expected tag");
	return lookup();
}

static Node*
parse_name(void)
{
	Sym *s;
	Node *n;

	if(yyget() != LNAME){
		yyerror("expected name");
		s = lookup();
	}else
		s = yylval.sym;
	n = new(ONAME, Z, Z);
	if(s->class == CLOCAL)
		s = mkstatic(s);
	n->sym = s;
	n->type = s->type;
	n->etype = TVOID;
	if(n->type != T)
		n->etype = n->type->etype;
	n->xoffset = s->offset;
	n->class = s->class;
	s->aused = 1;
	return n;
}

static Node*
parse_adecl(void)
{
	Node *a;

	parse_ctlist();
	if(yypeek(0) == ';'){
		yyget();
		return dodecl(adecl, lastclass, lasttype, Z);
	}
	a = parse_adlist();
	if(yyget() != ';'){
		yyerror("expected ';'");
		skiptosemi();
	}
	return a;
}

static Node*
parse_adlist_one(void)
{
	Node *x, *l;

	x = parse_xdecor_full();
	if(yypeek(0) == '='){
		long w;
		Node *in;
		Sym *s;
		Type *t;
		yyget();
		x = dodecl(adecl, lastclass, lasttype, x);
		/*
		 * save the declarator's sym/type BEFORE x gets reassigned
		 * to doinit()'s result below. The original yacc grammar
		 * kept these in a separate $1 slot from $$; collapsing
		 * both into one `x' variable here left contig() reading
		 * ->sym off of doinit()'s result node instead of off the
		 * declarator, which is nil/garbage for that node shape and
		 * crashed contig() on the first local array declared with
		 * an initializer (e.g. `char buf[8] = {...};' inside a
		 * function body).
		 */
		s = x->sym;
		t = x->type;
		in = parse_init();
		w = s->type->width;
		x = doinit(s, t, 0L, in);
		l = contig(s, x, w);
		return l;
	}
	dodecl(adecl, lastclass, lasttype, x);
	return Z;
}

static Node*
parse_adlist(void)
{
	Node *l, *r;

	l = parse_adlist_one();
	while(yypeek(0) == ','){
		yyget();
		r = parse_adlist_one();
		if(r != Z){
			if(l != Z)
				l = new(OLIST, l, r);
			else
				l = r;
		}
	}
	return l;
}

static void
parse_pdecl(void)
{
	while(is_typestart(yypeek(0))){
		parse_ctlist();
		parse_pdlist();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
			return;
		}
	}
}

static void
parse_pdlist(void)
{
	Node *x;

	x = parse_xdecor_full();
	dodecl(pdecl, lastclass, lasttype, x);
	while(yypeek(0) == ','){
		yyget();
		/* pdlist ',' pdlist: right can be list; loop */
		x = parse_xdecor_full();
		dodecl(pdecl, lastclass, lasttype, x);
	}
}

static void
parse_edecl(void)
{
	Type *t;

	t = parse_tlist();
	lasttype = t;
	/* zedlist ';' */
	if(yypeek(0) == ';'){
		lastfield = 0;
		edecl(CXXX, lasttype, S);
		yyget();
	}else{
		parse_edlist();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
	}
	/* edecl tlist zedlist ';' loop */
	while(is_typestart(yypeek(0))){
		/* need to distinguish tlist starting new edecl vs
		 * expression? In sbody, only decls appear, so loop.
		 * Peek for type-start and not '}'.
		 */
		if(yypeek(0) == '}')
			break;
		t = parse_tlist();
		lasttype = t;
		if(yypeek(0) == ';'){
			lastfield = 0;
			edecl(CXXX, lasttype, S);
			yyget();
		}else{
			parse_edlist();
			if(yyget() != ';'){
				yyerror("expected ';'");
				skiptosemi();
			}
		}
	}
}

static void
parse_edlist(void)
{
	Node *x;

	x = parse_edecor();
	dodecl(edecl, CXXX, lasttype, x);
	while(yypeek(0) == ','){
		yyget();
		/* edlist ',' edlist */
		x = parse_edecor();
		dodecl(edecl, CXXX, lasttype, x);
		/* yacc edlist ',' edlist has no value; loop suffices */
		if(yypeek(0) == ',')
			continue;
		/* handle right-nested via recursion? loop covers */
	}
}

static Node*
parse_edecor(void)
{
	Node *n;

	if(yypeek(0) == ':'){
		Node *e;
		yyget();
		e = parse_lexpr();
		return new(OBIT, Z, e);
	}
	/* try tag ':' lexpr (bitfield named) vs xdecor */
	/* Need 2-token lookahead: LNAME/LTYPE ':' vs xdecor */
	if((yypeek(0) == LNAME || yypeek(0) == LTYPE) && yypeek(1) == ':'){
		Sym *s;
		Node *tg, *e;
		yyget();
		s = yylval.sym;
		yyget(); /* ':' */
		e = parse_lexpr();
		tg = new(ONAME, Z, Z);
		tg->sym = s;
		tg->type = s->type;
		tg->etype = TVOID;
		if(tg->type != T)
			tg->etype = tg->type->etype;
		tg->xoffset = s->offset;
		tg->class = s->class;
		return new(OBIT, tg, e);
	}
	n = parse_xdecor_full();
	lastbit = 0;
	firstbit = 1;
	/* also handle tag ':' lexpr where tag was parsed as xdecor?
	 * Original edecor: xdecor {lastbit=0;...} | tag ':' lexpr | ':' lexpr.
	 * If xdecor was just a tag and next is ':' then it is bitfield.
	 * Our above handles tag ':' directly; if n is ONAME and peek==':',
	 * convert.
	 */
	if(n != Z && n->op == ONAME && yypeek(0) == ':'){
		Node *e;
		yyget();
		e = parse_lexpr();
		return new(OBIT, n, e);
	}
	return n;
}

static Node*
parse_abdecor(void)
{
	if(yypeek(0) == '*' || yypeek(0) == '(' || yypeek(0) == '[')
		return parse_abdecor1();
	return Z;
}

static Node*
parse_abdecor1(void)
{
	if(yypeek(0) == '*'){
		long g = 0;
		yyget();
		while(yypeek(0)==LCONSTNT || yypeek(0)==LVOLATILE || yypeek(0)==LRESTRICT){
			long b = specbit(yypeek(0));
			if(b == BCONSTNT || b == BVOLATILE)
				g = typebitor(g, b);
			yyget();
			if(yypeek(0) != LCONSTNT && yypeek(0) != LVOLATILE && yypeek(0) != LRESTRICT)
				break;
		}
		if(yypeek(0) == '*' || yypeek(0) == '(' || yypeek(0) == '['){
			Node *n = parse_abdecor1();
			Node *m = new(OIND, n, Z);
			m->garb = simpleg(g);
			return m;
		}
		{
			Node *m = new(OIND, Z, Z);
			m->garb = simpleg(g);
			return m;
		}
	}
	return parse_abdecor2();
}

static Node*
parse_abdecor2(void)
{
	Node *n = parse_abdecor3();

	for(;;){
		if(yypeek(0) == '('){
			Node *a;
			yyget();
			/* abdecor2 '(' zarglist ')' ; also '(' ')' (no args) handled
			 * via zarglist empty.
			 */
			a = parse_zarglist();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptosemi();
			}
			n = new(OFUNC, n, a);
			continue;
		}
		if(yypeek(0) == '['){
			Node *e;
			yyget();
			e = parse_zexpr();
			if(yyget() != ']'){
				yyerror("expected ']'");
				skiptosemi();
			}
			n = new(OARRAY, n, e);
			continue;
		}
		break;
	}
	return n;
}

static Node*
parse_abdecor3(void)
{
	if(yypeek(0) == '('){
		yyget();
		if(yypeek(0) == ')'){
			yyget();
			return new(OFUNC, Z, Z);
		}
		/* '(' abdecor1 ')' */
		{
			Node *n = parse_abdecor1();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptosemi();
			}
			return n;
		}
	}
	if(yypeek(0) == '['){
		Node *e;
		yyget();
		e = parse_zexpr();
		if(yyget() != ']'){
			yyerror("expected ']'");
			skiptosemi();
		}
		return new(OARRAY, Z, e);
	}
	/* empty abdecor can occur; caller checks. Return Z if none. */
	return Z;
}

static Node*
parse_init(void)
{
	if(yypeek(0) == '{'){
		Node *il;
		yyget();
		il = parse_ilist();
		if(yyget() != '}'){
			yyerror("expected '}'");
			skiptosemi();
		}
		return new(OINIT, invert(il), Z);
	}
	return parse_expr();
}

static Node*
parse_qual(void)
{
	if(yypeek(0) == '['){
		Node *e;
		yyget();
		e = parse_lexpr();
		if(yyget() != ']'){
			yyerror("expected ']'");
			skiptosemi();
		}
		e = new(OARRAY, e, Z);
		while(yypeek(0) == '='){
			yyget();  /* qual '=' : postfix, no new node (yacc $$=$1) */
		}
		return e;
	}
	if(yypeek(0) == '.'){
		Sym *s;
		Node *n;
		yyget();
		s = parse_ltag();
		n = new(OELEM, Z, Z);
		n->sym = s;
		while(yypeek(0) == '='){
			yyget();  /* qual '=' */
		}
		return n;
	}
	yyerror("expected qualifier");
	skiptosemi();
	return Z;
}

static Node*
parse_ilist(void)
{
	/* ilist: qlist | init | qlist init */
	Node *a, *b;

	if(yypeek(0) == '}')
		return Z;
	/* Try to parse as qlist-like sequence then optional init.
	 * Original ilist is ambiguous; implement as loop collecting
	 * inits and quals with OLIST, handling trailing ','.
	 */
	a = Z;
	for(;;){
		long t = yypeek(0);
		if(t == '}' || t == -1 || t == 0)
			break;
		if(t == '[' || t == '.'){
			b = parse_qual();
			if(a == Z)
				a = b;
			else
				a = new(OLIST, a, b);
			continue;
		}
		/* init, possibly followed by ',' */
		b = parse_init();
		if(a == Z)
			a = b;
		else
			a = new(OLIST, a, b);
		if(yypeek(0) == ','){
			yyget();
			/* qlist init ',' pattern: comma consumed, continue.
			 * If next is '}' then trailing comma, break.
			 */
			if(yypeek(0) == '}')
				break;
			continue;
		}
		/* if next is qual, continue; else break */
		if(yypeek(0) == '[' || yypeek(0) == '.')
			continue;
		break;
	}
	return a;
}

static Node*
parse_zarglist(void)
{
	if(yypeek(0) == ')')
		return Z;
	return invert(parse_arglist());
}

static Node*
parse_arg_one(void)
{
	Node *n;

	if(yypeek(0) == '.' && yypeek(1) == '.' ){
		/* '.' '.' '.' : check three dots */
		yyget();
		if(yyget() != '.' || yyget() != '.'){
			yyerror("expected '...'");
			skiptosemi();
			return new(ODOTDOT, Z, Z);
		}
		n = new(ODOTDOT, Z, Z);
	}else if(is_typestart(yypeek(0))){
		Type *t = parse_tlist();
		/* tlist abdecor vs tlist xdecor: decide by tag presence */
		if(yypeek(0) == LNAME || yypeek(0) == LTYPE){
			Node *x = parse_xdecor_full();
			n = new(OPROTO, x, Z);
			n->type = t;
		}else if(yypeek(0) == '*' || yypeek(0) == '(' || yypeek(0) == '['){
			/* Could be either; heuristic: if '*' chain leads to tag, xdecor */
			/* Peek ahead for tag before ','/')' */
			/* Simplify: try xdecor if tag ahead, else abdecor */
			/* For now, try abdecor if next is ','/')' after stars? */
			/* Consume as abdecor unless tag found. */
			/* Look ahead up to 16 tokens for LNAME/LTYPE at nesting
			 * depth 0. Names inside '[' ... ']' are array sizes,
			 * not tags: e.g. abstract 'uchar[VtScoreSize]' must be
			 * abdecor, not xdecor (xdecor demands a tag). */
			int isx = 0;
			int i;
			for(i=0;i<16;i++){
				long tt = yypeek(i);
				if(tt == LNAME || tt == LTYPE){ isx = 1; break; }
				if(tt == ',' || tt == ')' || tt == -1 || tt == 0) break;
				if(tt == '['){
					/* skip bracketed size expr */
					int depth = 1;
					while(depth > 0){
						i++;
						if(i >= 200)
							break;
						tt = yypeek(i);
						if(tt == -1 || tt == 0)
							break;
						if(tt == '[')
							depth++;
						if(tt == ']')
							depth--;
					}
					if(depth != 0)
						break;
					continue;
				}
				if(tt != '*' && tt != LCONSTNT && tt != LVOLATILE && tt != LRESTRICT && tt != '(') break;
			}
			if(isx){
				Node *x = parse_xdecor_full();
				n = new(OPROTO, x, Z);
				n->type = t;
			}else{
				Node *x = parse_abdecor();
				n = new(OPROTO, x, Z);
				n->type = t;
			}
		}else{
			Node *x = parse_abdecor();
			n = new(OPROTO, x, Z);
			n->type = t;
		}
	}else{
		n = parse_name();
	}
	return n;
}

static Node*
parse_arglist(void)
{
	Node *n = parse_arg_one();

	while(yypeek(0) == ','){
		Node *r;
		yyget();
		r = parse_arg_one();
		n = new(OLIST, n, r);
	}
	return n;
}

/* type helpers */

static int
is_typestart(long t)
{
	return t==LCHAR||t==LSHORT||t==LINT||t==LLONG||t==LSIGNED||
	    t==LUNSIGNED||t==LFLOAT||t==LDOUBLE||t==LVOID||t==LSTRUCT||
	    t==LUNION||t==LENUM||t==LTYPE||t==LAUTO||t==LSTATIC||t==LEXTERN||
	    t==LTYPEDEF||t==LTYPESTR||t==LREGISTER||t==LINLINE||t==LCONSTNT||
	    t==LVOLATILE||t==LRESTRICT;
}

static int
is_specword(long t)
{
	return t==LCHAR||t==LSHORT||t==LINT||t==LLONG||t==LSIGNED||
	    t==LUNSIGNED||t==LFLOAT||t==LDOUBLE||t==LVOID||t==LAUTO||
	    t==LSTATIC||t==LEXTERN||t==LTYPEDEF||t==LTYPESTR||t==LREGISTER||
	    t==LINLINE||t==LCONSTNT||t==LVOLATILE||t==LRESTRICT;
}

static long
specbit(long tok)
{
	switch(tok){
	case LCHAR: return BCHAR;
	case LSHORT: return BSHORT;
	case LINT: return BINT;
	case LLONG: return BLONG;
	case LSIGNED: return BSIGNED;
	case LUNSIGNED: return BUNSIGNED;
	case LFLOAT: return BFLOAT;
	case LDOUBLE: return BDOUBLE;
	case LVOID: return BVOID;
	case LAUTO: return BAUTO;
	case LSTATIC: return BSTATIC;
	case LEXTERN: return BEXTERN;
	case LTYPEDEF: return BTYPEDEF;
	case LTYPESTR: return BTYPESTR;
	case LREGISTER: return BREGISTER;
	case LINLINE: return 0;
	case LCONSTNT: return BCONSTNT;
	case LVOLATILE: return BVOLATILE;
	case LRESTRICT: return 0;
	}
	return 0;
}

static void
parse_zctlist(void)
{
	if(!is_typestart(yypeek(0)) && yypeek(0)!=LSTRUCT && yypeek(0)!=LUNION && yypeek(0)!=LENUM && yypeek(0)!=LTYPE){
		lastclass = CXXX;
		lasttype = types[TINT];
		return;
	}
	parse_ctlist();
}

static void
parse_ctlist(void)
{
	Type *t;
	int c;

	parse_types(&t, &c);
	lasttype = t;
	lastclass = c;
}

static Type*
parse_tlist(void)
{
	Type *t;
	int c;

	parse_types(&t, &c);
	if(c != CXXX)
		diag(Z, "illegal combination of class 4: %s", cnames[c]);
	return t;
}

static void
parse_types(Type **pt, int *pc)
{
	long bits = 0;
	Type *ctype = T;
	int havec = 0;

	/* leading words? Loop consuming specwords and complex */
	for(;;){
		long t = yypeek(0);
		if(t==LSTRUCT||t==LUNION||t==LENUM||t==LTYPE){
			Type *c;
			if(havec)
				break;  /* yacc types has no complex-complex; trailing LTYPE is declarator (e.g. repeat `typedef struct Ieee Ieee;`) */
			c = parse_complex();
			ctype = c;
			havec = 1;
			continue;
		}
		if(is_specword(t)){
			yyget();
			bits = typebitor(bits, specbit(t));
			continue;
		}
		break;
	}
	if(havec){
		int c = simplec(bits);
		Type *tt = garbt(ctype, bits);
		if(bits & ~BCLASS & ~BGARB)
			diag(Z, "duplicate types given: %T and %Q", ctype, bits);
		*pt = tt;
		*pc = c;
		return;
	}
	*pt = simplet(bits);
	*pc = simplec(bits);
	*pt = garbt(*pt, bits);
}

static Type*
parse_complex(void)
{
	long t = yypeek(0);

	if(t == LTYPE){
		Type *tt;
		yyget();
		tt = tcopy(yylval.sym->type);
		return tt;
	}
	if(t == LSTRUCT || t == LUNION){
		int et = (t==LSTRUCT)? TSTRUCT : TUNION;
		Type *tt;
		yyget();
		if(yypeek(0)==LNAME||yypeek(0)==LTYPE){
			Sym *s;
			yyget();
			s = yylval.sym;
			if(yypeek(0) == '{'){
				dotag(s, et, autobn);
				{
					Type *b = parse_sbody();
					tt = s->suetag;
					if(tt->link != T)
						diag(Z, "redeclare tag: %s", s->name);
					tt->link = b;
					sualign(tt);
					return tt;
				}
			}
			dotag(s, et, 0);
			return s->suetag;
		}
		if(yypeek(0) == '{'){
			Type *b = parse_sbody();
			Sym *s;
			taggen++;
			snprint(symb, sizeof symb, "_%d_", taggen);
			s = lookup();
			tt = dotag(s, et, autobn);
			tt->link = b;
			sualign(tt);
			return tt;
		}
		yyerror("expected tag or '{'");
		skiptosemi();
		return types[TINT];
	}
	if(t == LENUM){
		Type *tt;
		yyget();
		if(yypeek(0)==LNAME||yypeek(0)==LTYPE){
			Sym *s;
			yyget();
			s = yylval.sym;
			if(yypeek(0) == '{'){
				yyget();
				dotag(s, TENUM, autobn);
				en.tenum = T;
				en.cenum = T;
				parse_enumrest();
				if(yyget() != '}'){
					yyerror("expected '}'");
					skiptosemi();
				}
				tt = s->suetag;
				if(tt->link != T)
					diag(Z, "redeclare tag: %s", s->name);
				if(en.tenum == T){
					diag(Z, "enum type ambiguous: %s", s->name);
					en.tenum = types[TINT];
				}
				tt->link = en.tenum;
				return en.tenum;
			}
			dotag(s, TENUM, 0);
			tt = s->suetag;
			if(tt->link == T)
				tt->link = types[TINT];
			return tt->link;
		}
		if(yypeek(0) == '{'){
			yyget();
			en.tenum = T;
			en.cenum = T;
			parse_enumrest();
			if(yyget() != '}'){
				yyerror("expected '}'");
				skiptosemi();
			}
			return en.tenum;
		}
		yyerror("expected enum body or tag");
		skiptosemi();
		return types[TINT];
	}
	yyerror("expected type");
	return types[TINT];
}

static Type*
parse_sbody(void)
{
	Type *s1, *s2, *s3;
	int c;

	if(yyget() != '{'){
		yyerror("expected '{'");
		return T;
	}
	s1 = strf; s2 = strl; s3 = lasttype; c = lastclass;
	strf = T; strl = T;
	lastbit = 0; firstbit = 1;
	lastclass = CXXX; lasttype = T;
	parse_edecl();
	if(yyget() != '}'){
		yyerror("expected '}'");
		skiptosemi();
	}
	{
		Type *r = strf;
		strf = s1; strl = s2; lasttype = s3; lastclass = c;
		return r;
	}
}

static void
parse_enumrest(void)
{
	/* enum: LNAME [=expr] (',' ...)* with trailing ',' allowed */
	for(;;){
		long t = yypeek(0);
		if(t != LNAME)
			break;
		yyget();
		{
			Sym *s = yylval.sym;
			if(yypeek(0) == '='){
				Node *e;
				yyget();
				e = parse_expr();
				doenum(s, e);
			}else
				doenum(s, Z);
		}
		if(yypeek(0) != ',')
			break;
		yyget();
		/* trailing comma before '}' allowed: peek '}' -> break */
		if(yypeek(0) == '}')
			break;
	}
}

/* ---------- (b) stmt batch ---------- */

static Node*
parse_block(void)
{
	Node *s;

	if(yyget() != '{'){
		yyerror("expected '{'");
		skiptosemi();
		return new(OLIST, Z, Z);
	}
	s = parse_slist();
	if(yyget() != '}'){
		yyerror("expected '}'");
		skiptosemi();
	}
	{
		Node *r = invert(s);
		if(r == Z)
			r = new(OLIST, Z, Z);
		return r;
	}
}

static Node*
parse_slist(void)
{
	Node *s = Z;

	for(;;){
		long t = yypeek(0);
		if(t == '}' || t == -1 || t == 0)
			break;
		if(is_typestart(t)){
			/* Could be adecl vs stmnt starting with type?
			 * adecl always starts with type; stmnt ulstmnt can
			 * start with type only for ...? Actually ulstmnt
			 * zcexpr can start with LTYPE? No, expr cannot start
			 * with type keywords (except sizeof?). sizeof starts
			 * with LSIZEOF which is type-start! So `sizeof(x);`
			 * as stmnt starts with LSIZEOF. Distinguish: if after
			 * type words we see declarator pattern vs ';'?
			 * Heuristic: try adecl if it looks like decl:
			 * type words followed by declarator or ';'.
			 * For sizeof, after LSIZEOF next is '(' or expr, not
			 * declarator. Our parse_adecl would misparse sizeof.
			 * Better: if peek is LSIZEOF/LSIGNOF, treat as stmnt.
			 */
			if(t == LSIZEOF || t == LSIGNOF){
				Node *st = parse_stmnt();
				s = new(OLIST, s, st);
				continue;
			}
			/* Look ahead: if type-start then check if second token
			 * suggests decl (LNAME/LTYPE/'*'/';'/'['/'(') vs expr.
			 * For decl, after types we expect xdecor or ';'.
			 * For expr stmnt starting with type? Only sizeof case
			 * handled. Others (e.g., `int;`?) are decl.
			 * So treat as adecl.
			 */
			{
				Node *a = parse_adecl();
				s = new(OLIST, s, a);
				continue;
			}
		}
		{
			Node *st = parse_stmnt();
			s = new(OLIST, s, st);
		}
	}
	return s;
}

static Node*
parse_label(void)
{
	if(yypeek(0) == LCASE){
		Node *e;
		yyget();
		e = parse_expr();
		if(yyget() != ':'){
			yyerror("expected ':'");
			skiptosemi();
		}
		return new(OCASE, e, Z);
	}
	if(yypeek(0) == LDEFAULT){
		yyget();
		if(yyget() != ':'){
			yyerror("expected ':'");
			skiptosemi();
		}
		return new(OCASE, Z, Z);
	}
	/* LNAME ':' */
	{
		Sym *s;
		yyget();
		s = yylval.sym;
		if(yyget() != ':'){
			yyerror("expected ':'");
			skiptosemi();
		}
		return new(OLABEL, dcllabel(s, 1), Z);
	}
}

static int
is_labelstart(void)
{
	if(yypeek(0) == LCASE || yypeek(0) == LDEFAULT)
		return 1;
	if(yypeek(0) == LNAME && yypeek(1) == ':')
		return 1;
	return 0;
}

static Node*
parse_stmnt(void)
{
	Node *l, *u;

	if(is_labelstart()){
		l = parse_label();
		/* labels label loop */
		while(is_labelstart()){
			Node *l2 = parse_label();
			l = new(OLIST, l, l2);
		}
		u = parse_ulstmnt();
		if(u == Z && l == Z){
			yyerror("expected statement");
			skiptosemi();
			return Z;
		}
		return new(OLIST, l, u);
	}
	u = parse_ulstmnt();
	return u;
}

static Node*
parse_forexpr(void)
{
	if(is_typestart(yypeek(0)) && yypeek(0) != LSIZEOF && yypeek(0) != LSIGNOF){
		/* ctlist adlist: need to distinguish from expr?
		 * forexpr: zcexpr | ctlist adlist.
		 * If starts with type and next looks like decl, parse decl.
		 */
		parse_ctlist();
		/* if next is ';' then it was `type;'? Actually forexpr
		 * `ctlist adlist' requires adlist (declarator). If next
		 * is ';' then it was empty? Treat as decl with no init?
		 */
		if(yypeek(0) == ';')
			return Z;
		return parse_adlist();
	}
	return parse_zcexpr();
}

static Node*
parse_ulstmnt(void)
{
	long t = yypeek(0);

	if(t == ';' || t == LCONST || t == LLCONST || t == LUCONST ||
	   t == LULCONST || t == LVLCONST || t == LUVLCONST ||
	   t == LDCONST || t == LFCONST || t == LSTRING || t == LLSTRING ||
	   t == LNAME || t == LTYPE || t == '(' || t == '*' || t == '&' ||
	   t == '+' || t == '-' || t == '!' || t == '~' || t == LPP ||
	   t == LMM || t == LSIZEOF || t == LSIGNOF){
		Node *e = parse_zcexpr();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
		return e;
	}
	if(t == '{'){
		Node *b, *r;
		markdcl();
		b = parse_block();
		r = revertdcl();
		if(r)
			return new(OLIST, r, b);
		return b;
	}
	if(t == LIF){
		Node *c, *s1, *s2;
		yyget();
		if(yyget() != '('){
			yyerror("expected '('");
			skiptosemi();
			return Z;
		}
		c = parse_cexpr();
		if(yyget() != ')'){
			yyerror("expected ')'");
			skiptosemi();
		}
		s1 = parse_stmnt();
		if(yypeek(0) == LELSE){
			yyget();
			s2 = parse_stmnt();
			{
				Node *n = new(OIF, c, new(OLIST, s1, s2));
				if(s1 == Z)
					warn(c, "empty if body");
				if(s2 == Z)
					warn(c, "empty else body");
				return n;
			}
		}
		{
			Node *n = new(OIF, c, new(OLIST, s1, Z));
			if(s1 == Z)
				warn(c, "empty if body");
			return n;
		}
	}
	if(t == LFOR){
		Node *f1, *f2, *f3, *body, *r;
		yyget();
		markdcl();
		if(yyget() != '('){
			yyerror("expected '('");
			skiptosemi();
			return Z;
		}
		f1 = parse_forexpr();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
		f2 = parse_zcexpr();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
		f3 = parse_zcexpr_rp();
		if(yyget() != ')'){
			yyerror("expected ')'");
			skiptosemi();
		}
		body = parse_stmnt();
		r = revertdcl();
		if(r){
			if(f1)
				f1 = new(OLIST, r, f1);
			else
				f1 = r;
		}
		return new(OFOR, new(OLIST, f2, new(OLIST, f1, f3)), body);
	}
	if(t == LWHILE){
		Node *c, *s;
		yyget();
		if(yyget() != '('){
			yyerror("expected '('");
			skiptosemi();
			return Z;
		}
		c = parse_cexpr();
		if(yyget() != ')'){
			yyerror("expected ')'");
			skiptosemi();
		}
		s = parse_stmnt();
		return new(OWHILE, c, s);
	}
	if(t == LDO){
		Node *s, *c;
		yyget();
		s = parse_stmnt();
		if(yyget() != LWHILE){
			yyerror("expected 'while'");
			skiptosemi();
			return s;
		}
		if(yyget() != '('){
			yyerror("expected '('");
			skiptosemi();
			return s;
		}
		c = parse_cexpr();
		if(yyget() != ')'){
			yyerror("expected ')'");
			skiptosemi();
		}
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
		return new(ODWHILE, c, s);
	}
	if(t == LRETURN){
		Node *e;
		yyget();
		e = parse_zcexpr();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
		{
			Node *n = new(ORETURN, e, Z);
			n->type = thisfn->link;
			return n;
		}
	}
	if(t == LSWITCH){
		Node *c, *s, *z;
		yyget();
		if(yyget() != '('){
			yyerror("expected '('");
			skiptosemi();
			return Z;
		}
		c = parse_cexpr();
		if(yyget() != ')'){
			yyerror("expected ')'");
			skiptosemi();
		}
		s = parse_stmnt();
		z = new(OCONST, Z, Z);
		z->vconst = 0;
		z->type = types[TINT];
		c = new(OSUB, z, c);
		z = new(OCONST, Z, Z);
		z->vconst = 0;
		z->type = types[TINT];
		c = new(OSUB, z, c);
		return new(OSWITCH, c, s);
	}
	if(t == LBREAK){
		yyget();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
		return new(OBREAK, Z, Z);
	}
	if(t == LCONTINUE){
		yyget();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
		return new(OCONTINUE, Z, Z);
	}
	if(t == LGOTO){
		Sym *s;
		yyget();
		s = parse_ltag();
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
		return new(OGOTO, dcllabel(s, 0), Z);
	}
	if(t == LUSED || t == LSET){
		int op = (t==LUSED)? OUSED : OSET;
		Node *e;
		yyget();
		if(yyget() != '('){
			yyerror("expected '('");
			skiptosemi();
			return Z;
		}
		e = parse_zelist();
		if(yyget() != ')'){
			yyerror("expected ')'");
			skiptosemi();
		}
		if(yyget() != ';'){
			yyerror("expected ';'");
			skiptosemi();
		}
		return new(op, e, Z);
	}
	yyerror("expected statement");
	skiptosemi();
	return Z;
}

static Node*
parse_zcexpr(void)
{
	if(yypeek(0) == ';')
		return Z;
	return parse_cexpr();
}

/*
 * Like parse_zcexpr, but for the third (increment) clause of a
 * `for(init; cond; incr)' statement, which is terminated by ')'
 * rather than ';'. `for(;;)' must be recognized as all three
 * clauses empty -- reusing the ';'-only check here fed a bare ')'
 * to parse_cexpr, which reported "expected expression" and then
 * desynced brace/paren matching for the rest of the file.
 */
static Node*
parse_zcexpr_rp(void)
{
	if(yypeek(0) == ')')
		return Z;
	return parse_cexpr();
}

static Node*
parse_zexpr(void)
{
	if(yypeek(0)==';'||yypeek(0)==']'||yypeek(0)==')'||yypeek(0)=='}'||yypeek(0)==',')
		return Z;
	/* lexpr vs error? lexpr is expr with cast to long; but zexpr is used
	 * for array sizes and abstract array: zexpr: | lexpr.
	 * If next cannot start expr, return Z.
	 */
	return parse_lexpr();
}

static Node*
parse_lexpr(void)
{
	Node *e = parse_expr();
	Node *n = new(OCAST, e, Z);
	n->type = types[TLONG];
	return n;
}

static Node*
parse_cexpr(void)
{
	Node *l = parse_expr();

	while(yypeek(0) == ','){
		Node *r;
		yyget();
		r = parse_expr();
		l = new(OCOMMA, l, r);
	}
	return l;
}

/* ---------- (c) expr batch ---------- */

static int
is_assignop(long t)
{
	return t=='='||t==LPE||t==LME||t==LMLE||t==LDVE||t==LMDE||
	    t==LLSHE||t==LRSHE||t==LANDE||t==LXORE||t==LORE;
}

static int
assignop(long t)
{
	switch(t){
	case '=': return OAS;
	case LPE: return OASADD;
	case LME: return OASSUB;
	case LMLE: return OASMUL;
	case LDVE: return OASDIV;
	case LMDE: return OASMOD;
	case LLSHE: return OASASHL;
	case LRSHE: return OASASHR;
	case LANDE: return OASAND;
	case LXORE: return OASXOR;
	case LORE: return OASOR;
	}
	return OAS;
}

static Node*
parse_expr(void)
{
	Node *l = parse_ternary();
	long t = yypeek(0);

	if(is_assignop(t)){
		Node *r;
		int op;
		yyget();
		op = assignop(t);
		r = parse_expr();
		return new(op, l, r);
	}
	return l;
}

static Node*
parse_ternary(void)
{
	Node *c = parse_loror();

	if(yypeek(0) == '?'){
		Node *m, *e;
		yyget();
		m = parse_cexpr();
		if(yyget() != ':'){
			yyerror("expected ':'");
			skiptosemi();
			return c;
		}
		e = parse_expr();
		return new(OCOND, c, new(OLIST, m, e));
	}
	return c;
}

static Node*
parse_loror(void)
{
	Node *l = parse_landand();

	while(yypeek(0) == LOROR){
		Node *r;
		yyget();
		r = parse_landand();
		l = new(OOROR, l, r);
	}
	return l;
}

static Node*
parse_landand(void)
{
	Node *l = parse_bor();

	while(yypeek(0) == LANDAND){
		Node *r;
		yyget();
		r = parse_bor();
		l = new(OANDAND, l, r);
	}
	return l;
}

static Node*
parse_bor(void)
{
	Node *l = parse_bxor();

	while(yypeek(0) == '|'){
		Node *r;
		yyget();
		r = parse_bxor();
		l = new(OOR, l, r);
	}
	return l;
}

static Node*
parse_bxor(void)
{
	Node *l = parse_band();

	while(yypeek(0) == '^'){
		Node *r;
		yyget();
		r = parse_band();
		l = new(OXOR, l, r);
	}
	return l;
}

static Node*
parse_band(void)
{
	Node *l = parse_eq();

	while(yypeek(0) == '&'){
		Node *r;
		yyget();
		r = parse_eq();
		l = new(OAND, l, r);
	}
	return l;
}

static Node*
parse_eq(void)
{
	Node *l = parse_rel();

	for(;;){
		long t = yypeek(0);
		Node *r;
		if(t != LEQ && t != LNE)
			break;
		yyget();
		r = parse_rel();
		if(t == LEQ)
			l = new(OEQ, l, r);
		else
			l = new(ONE, l, r);
	}
	return l;
}

static Node*
parse_rel(void)
{
	Node *l = parse_shift();

	for(;;){
		long t = yypeek(0);
		Node *r;
		if(t != '<' && t != '>' && t != LLE && t != LGE)
			break;
		yyget();
		r = parse_shift();
		if(t == '<')
			l = new(OLT, l, r);
		else if(t == '>')
			l = new(OGT, l, r);
		else if(t == LLE)
			l = new(OLE, l, r);
		else
			l = new(OGE, l, r);
	}
	return l;
}

static Node*
parse_shift(void)
{
	Node *l = parse_add();

	for(;;){
		long t = yypeek(0);
		Node *r;
		if(t != LLSH && t != LRSH)
			break;
		yyget();
		r = parse_add();
		if(t == LLSH)
			l = new(OASHL, l, r);
		else
			l = new(OASHR, l, r);
	}
	return l;
}

static Node*
parse_add(void)
{
	Node *l = parse_mul();

	for(;;){
		long t = yypeek(0);
		Node *r;
		if(t != '+' && t != '-')
			break;
		yyget();
		r = parse_mul();
		if(t == '+')
			l = new(OADD, l, r);
		else
			l = new(OSUB, l, r);
	}
	return l;
}

static Node*
parse_mul(void)
{
	Node *l = parse_xuexpr();

	for(;;){
		long t = yypeek(0);
		Node *r;
		if(t != '*' && t != '/' && t != '%')
			break;
		yyget();
		r = parse_xuexpr();
		if(t == '*')
			l = new(OMUL, l, r);
		else if(t == '/')
			l = new(ODIV, l, r);
		else
			l = new(OMOD, l, r);
	}
	return l;
}

static Node*
parse_xuexpr(void)
{
	/* '(' tlist abdecor ')' xuexpr (cast) or compound literal */
	if(yypeek(0) == '('){
		long t1 = yypeek(1);
		if(is_typestart(t1) || t1 == LSTRUCT || t1 == LUNION || t1 == LENUM || t1 == LTYPE){
			/* Save lookahead, try type parse */
			int save_nla = nla;
			long savetok[NLA];
			YYSTYPE saveval[NLA];
			int si;
			Type *t;
			Node *ab;
			for(si = 0; si < nla && si < NLA; si++){
				savetok[si] = latok[si];
				saveval[si] = laval[si];
			}
			yyget(); /* '(' */
			t = parse_tlist();
			ab = parse_abdecor();
			if(yypeek(0) == ')'){
				yyget();
				if(yypeek(0) == '{'){
					Node *il;
					yyget();
					il = parse_ilist();
					if(yyget() != '}'){
						yyerror("expected '}'");
						skiptosemi();
					}
					{
						Node *n = new(OSTRUCT, il, Z);
						dodecl(NODECL, CXXX, t, ab);
						n->type = lastdcl;
						return n;
					}
				}
				{
					Node *x = parse_xuexpr();
					Node *n = new(OCAST, x, Z);
					dodecl(NODECL, CXXX, t, ab);
					n->type = lastdcl;
					n->xcast = 1;
					return n;
				}
			}
			/* not a cast: restore and fall through */
			nla = save_nla;
			for(si = 0; si < nla && si < NLA; si++){
				latok[si] = savetok[si];
				laval[si] = saveval[si];
			}
		}
	}
	return parse_uexpr();
}

static Node*
parse_uexpr(void)
{
	long t = yypeek(0);

	if(t == '*'){
		Node *x;
		yyget();
		x = parse_xuexpr();
		return new(OIND, x, Z);
	}
	if(t == '&'){
		Node *x;
		yyget();
		x = parse_xuexpr();
		return new(OADDR, x, Z);
	}
	if(t == '+'){
		Node *x;
		yyget();
		x = parse_xuexpr();
		return new(OPOS, x, Z);
	}
	if(t == '-'){
		Node *x;
		yyget();
		x = parse_xuexpr();
		return new(ONEG, x, Z);
	}
	if(t == '!'){
		Node *x;
		yyget();
		x = parse_xuexpr();
		return new(ONOT, x, Z);
	}
	if(t == '~'){
		Node *x;
		yyget();
		x = parse_xuexpr();
		return new(OCOM, x, Z);
	}
	if(t == LPP){
		Node *x;
		yyget();
		x = parse_xuexpr();
		return new(OPREINC, x, Z);
	}
	if(t == LMM){
		Node *x;
		yyget();
		x = parse_xuexpr();
		return new(OPREDEC, x, Z);
	}
	if(t == LSIZEOF){
		yyget();
		if(yypeek(0) == '(' && (is_typestart(yypeek(1)) || yypeek(1)==LSTRUCT || yypeek(1)==LUNION || yypeek(1)==LENUM || yypeek(1)==LTYPE)){
			Type *ty;
			Node *ab, *n;
			yyget();
			ty = parse_tlist();
			ab = parse_abdecor();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptosemi();
			}
			n = new(OSIZE, Z, Z);
			dodecl(NODECL, CXXX, ty, ab);
			n->type = lastdcl;
			return n;
		}
		{
			Node *x = parse_uexpr();
			return new(OSIZE, x, Z);
		}
	}
	if(t == LSIGNOF){
		yyget();
		if(yypeek(0) == '(' && (is_typestart(yypeek(1)) || yypeek(1)==LSTRUCT || yypeek(1)==LUNION || yypeek(1)==LENUM || yypeek(1)==LTYPE)){
			Type *ty;
			Node *ab, *n;
			yyget();
			ty = parse_tlist();
			ab = parse_abdecor();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptosemi();
			}
			n = new(OSIGN, Z, Z);
			dodecl(NODECL, CXXX, ty, ab);
			n->type = lastdcl;
			return n;
		}
		{
			Node *x = parse_uexpr();
			return new(OSIGN, x, Z);
		}
	}
	return parse_pexpr();
}

static Node*
parse_pexpr(void)
{
	Node *n;
	long t = yypeek(0);

	if(t == '('){
		yyget();
		n = parse_cexpr();
		if(yyget() != ')'){
			yyerror("expected ')'");
			skiptosemi();
		}
		goto postfix;
	}
	if(t == LSIZEOF && yypeek(1) == '('){
		/* LSIZEOF '(' tlist abdecor ')' handled in uexpr, but keep
		 * here for completeness if reached directly.
		 */
		return parse_uexpr();
	}
	if(t == LSIGNOF && yypeek(1) == '('){
		return parse_uexpr();
	}
	if(t == LCONST || t == LLCONST || t == LUCONST || t == LULCONST ||
	   t == LDCONST || t == LFCONST || t == LVLCONST || t == LUVLCONST){
		long tt = yyget();
		Node *c = new(OCONST, Z, Z);
		if(tt == LCONST){ c->type = types[TINT]; c->vconst = yylval.vval; }
		else if(tt == LLCONST){ c->type = types[TLONG]; c->vconst = yylval.vval; }
		else if(tt == LUCONST){ c->type = types[TUINT]; c->vconst = yylval.vval; }
		else if(tt == LULCONST){ c->type = types[TULONG]; c->vconst = yylval.vval; }
		else if(tt == LDCONST){ c->type = types[TDOUBLE]; c->fconst = yylval.dval; }
		else if(tt == LFCONST){ c->type = types[TFLOAT]; c->fconst = yylval.dval; }
		else if(tt == LVLCONST){ c->type = types[TVLONG]; c->vconst = yylval.vval; }
		else { c->type = types[TUVLONG]; c->vconst = yylval.vval; }
		c->cstring = strdup(symb);
		n = c;
		goto postfix;
	}
	if(t == LSTRING){
		n = parse_string();
		goto postfix;
	}
	if(t == LLSTRING){
		n = parse_lstring();
		goto postfix;
	}
	if(t == LNAME){
		n = parse_name();
		goto postfix;
	}
	yyerror("expected expression");
	skiptosemi();
	return Z;

postfix:
	for(;;){
		t = yypeek(0);
		if(t == '('){
			Node *a;
			yyget();
			a = parse_zelist();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptosemi();
			}
			{
				Node *f = new(OFUNC, n, Z);
				if(n->op == ONAME)
				if(n->type == T)
					dodecl(xdecl, CXXX, types[TINT], f);
				f->right = invert(a);
				n = f;
			}
			continue;
		}
		if(t == '['){
			Node *e;
			yyget();
			e = parse_cexpr();
			if(yyget() != ']'){
				yyerror("expected ']'");
				skiptosemi();
			}
			n = new(OIND, new(OADD, n, e), Z);
			continue;
		}
		if(t == LMG){
			Sym *s;
			yyget();
			s = parse_ltag();
			n = new(ODOT, new(OIND, n, Z), Z);
			n->sym = s;
			continue;
		}
		if(t == '.'){
			Sym *s;
			yyget();
			s = parse_ltag();
			n = new(ODOT, n, Z);
			n->sym = s;
			continue;
		}
		if(t == LPP){
			yyget();
			n = new(OPOSTINC, n, Z);
			continue;
		}
		if(t == LMM){
			yyget();
			n = new(OPOSTDEC, n, Z);
			continue;
		}
		break;
	}
	return n;
}

static Node*
parse_string(void)
{
	Node *n;

	yyget();
	n = new(OSTRING, Z, Z);
	n->type = typ(TARRAY, types[TCHAR]);
	n->type->width = yylval.sval.l + 1;
	n->cstring = yylval.sval.s;
	n->sym = symstring;
	n->etype = TARRAY;
	n->class = CSTATIC;
	while(yypeek(0) == LSTRING){
		char *s;
		int w;
		YYSTYPE v;
		yyget();
		v = yylval;
		w = n->type->width - 1;
		s = alloc(w+v.sval.l+MAXALIGN);
		memcpy(s, n->cstring, w);
		memcpy(s+w, v.sval.s, v.sval.l);
		s[w+v.sval.l] = 0;
		n->type->width += v.sval.l;
		n->cstring = s;
	}
	return n;
}

static Node*
parse_lstring(void)
{
	Node *n;

	yyget();
	n = new(OLSTRING, Z, Z);
	n->type = typ(TARRAY, types[TRUNE]);
	n->type->width = yylval.sval.l + sizeof(TRune);
	n->rstring = (TRune*)yylval.sval.s;
	n->sym = symstring;
	n->etype = TARRAY;
	n->class = CSTATIC;
	while(yypeek(0) == LLSTRING){
		char *s;
		int w;
		YYSTYPE v;
		yyget();
		v = yylval;
		w = n->type->width - sizeof(TRune);
		s = alloc(w+v.sval.l+MAXALIGN);
		memcpy(s, n->rstring, w);
		memcpy(s+w, v.sval.s, v.sval.l);
		*(TRune*)(s+w+v.sval.l) = 0;
		n->type->width += v.sval.l;
		n->rstring = (TRune*)s;
	}
	return n;
}

static Node*
parse_zelist(void)
{
	if(yypeek(0) == ')')
		return Z;
	return parse_elist();
}

static Node*
parse_elist(void)
{
	Node *l = parse_expr();

	while(yypeek(0) == ','){
		Node *r;
		yyget();
		/* elist ',' elist right-recursive in yacc; implement loop
		 * with right recursion for faithfulness on `a,b,c`?
		 * Loop left-nests then invert at call site handles order.
		 */
		r = parse_expr();
		l = new(OLIST, l, r);
	}
	return l;
}
