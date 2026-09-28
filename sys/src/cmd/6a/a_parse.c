#include "a.h"
#include "a_parse.h"

YYSTYPE yylval;
/*
 * Hand-written recursive-descent replacement for a.y (LALR, 6a/amd64).
 *
 * Original yacc grammar (precedence low -> high):
 *	%left '|'
 *	%left '^'
 *	%left '&'
 *	%left '<' '>'
 *	%left '+' '-'
 *	%left '*' '/' '%'
 * prog: | prog line
 * line: LLAB ':' line | LNAME ':' line | ';' | inst ';' | error ';'
 * inst: LNAME '=' expr | LVAR '=' expr
 *      | LTYPE0 nonnon | LTYPE1 nonrem | LTYPE2 rimnon
 *      | LTYPE3 rimrem | LTYPE4 remrim | LTYPER nonrel
 *      | LTYPED spec1 | LTYPET spec2 | LTYPEC spec3 | LTYPEN spec4
 *      | LTYPES spec5 | LTYPEM spec6 | LTYPEI spec7 | LTYPEXC spec8
 *      | LTYPEX spec9 | LTYPEG spec10 | LTYPEY spec11 | LTYPERT spec12
 * con/expr as in other assemblers (see below).
 *
 * Precedence-map (hand parser):
 *	level 1 (lowest, left): '|'
 *	level 2 (left): '^'
 *	level 3 (left): '&'
 *	level 4 (left): '<' '<' and '>' '>' (two single-char tokens)
 *	level 5 (left): '+' '-'
 *	level 6 (left): '*' '/' '%'
 *	level 7 (tightest): unary '-' '+' '~' in con, '(' expr ')'
 * Call chain: parse_expr -> parse_or -> parse_xor -> parse_and
 *              -> parse_shift -> parse_add -> parse_mul -> parse_con.
 *
 * Error-recovery-map: original had `error ;' in line; any syntax
 * error inside line/inst calls yyerror("syntax error") (which prints
 * "syntax error, last name: %s" and counts nerrors) then skips to
 * ';' or EOF (0/-1) and continues with next line, preserving
 * pass-1/pass-2 pc counting via outcode.  Semantic yyerror calls
 * inside actions (redeclaration, scale, undefined label, etc.) do
 * not skip; parsing continues normally.
 */

static long yypeek(int n);
static long yyget(void);
static void yysyntax_error(void);
static void yyexpect(long t);
static void yyskip_to_semi(void);
static int isconstart(long t);
static int isregtok(long t);
static vlong parse_expr(void);
static vlong parse_or(void);
static vlong parse_xor(void);
static vlong parse_and(void);
static vlong parse_shift(void);
static vlong parse_add(void);
static vlong parse_mul(void);
static vlong parse_con(void);
static vlong parse_offset(void);
static vlong parse_pointer(void);
static Gen parse_reg(void);
static Gen parse_nam(void);
static Gen parse_omem(void);
static Gen parse_omem_rest(vlong c);
static Gen parse_nmem(void);
static Gen parse_mem(void);
static Gen parse_imm(void);
static Gen parse_rel(void);
static Gen parse_rem(void);
static Gen parse_rim(void);
static Gen parse_rom(void);
static Gen2 parse_nonnon(void);
static Gen2 parse_rimrem(void);
static Gen2 parse_remrim(void);
static Gen2 parse_rimnon(void);
static Gen2 parse_nonrem(void);
static Gen2 parse_nonrel(void);
static Gen2 parse_spec1(void);
static Gen2 parse_spec2(void);
static Gen2 parse_spec3(void);
static Gen2 parse_spec4(void);
static Gen2 parse_spec5(void);
static Gen2 parse_spec6(void);
static Gen2 parse_spec7(void);
static Gen2 parse_spec8(void);
static Gen2 parse_spec9(void);
static Gen2 parse_spec10(void);
static Gen2 parse_spec11(void);
static Gen2 parse_spec12(void);
static void parse_inst(void);
static void parse_line(void);

/* lookahead buffer over yylex() (2-token for labels/shifts, more for comma counting) */
static int yyhave;
static long yytok[256];
static YYSTYPE yyval[256];
static jmp_buf yyrec;

static long
yypeek(int n)
{
	if(n < 0 || n >= 256)
		yysyntax_error();
	while(yyhave <= n){
		yytok[yyhave] = yylex();
		yyval[yyhave] = yylval;
		yyhave++;
	}
	return yytok[n];
}

static long
yyget(void)
{
	long t;
	int i;

	yypeek(0);
	t = yytok[0];
	yylval = yyval[0];
	for(i = 1; i < yyhave; i++){
		yytok[i-1] = yytok[i];
		yyval[i-1] = yyval[i];
	}
	yyhave--;
	return t;
}

static void
yysyntax_error(void)
{
	yyerror("syntax error");
	longjmp(yyrec, 1);
}

static void
yyexpect(long t)
{
	if(yyget() != t)
		yysyntax_error();
}

static void
yyskip_to_semi(void)
{
	long t;

	for(;;){
		t = yyget();
		if(t == ';' || t == 0 || t == EOF)
			break;
	}
}

static int
isconstart(long t)
{
	return t == LCONST || t == LVAR || t == '-' || t == '+' || t == '~' || t == '(';
}

static int
isregtok(long t)
{
	return t == LBREG || t == LFREG || t == LLREG || t == LMREG
		|| t == LSP || t == LSREG || t == LXREG || t == LYREG;
}

static vlong
parse_or(void)
{
	vlong l, r;

	l = parse_xor();
	while(yypeek(0) == '|'){
		yyget();
		r = parse_xor();
		l = l | r;
	}
	return l;
}

static vlong
parse_xor(void)
{
	vlong l, r;

	l = parse_and();
	while(yypeek(0) == '^'){
		yyget();
		r = parse_and();
		l = l ^ r;
	}
	return l;
}

static vlong
parse_and(void)
{
	vlong l, r;

	l = parse_shift();
	while(yypeek(0) == '&'){
		yyget();
		r = parse_shift();
		l = l & r;
	}
	return l;
}

static vlong
parse_shift(void)
{
	vlong l, r;

	l = parse_add();
	for(;;){
		if(yypeek(0) == '<' && yypeek(1) == '<'){
			yyget();
			yyget();
			r = parse_add();
			l = l << r;
			continue;
		}
		if(yypeek(0) == '>' && yypeek(1) == '>'){
			yyget();
			yyget();
			r = parse_add();
			l = l >> r;
			continue;
		}
		break;
	}
	return l;
}

static vlong
parse_add(void)
{
	vlong l, r;
	long t;

	l = parse_mul();
	for(;;){
		t = yypeek(0);
		if(t != '+' && t != '-')
			break;
		yyget();
		r = parse_mul();
		if(t == '+')
			l = l + r;
		else
			l = l - r;
	}
	return l;
}

static vlong
parse_mul(void)
{
	vlong l, r;
	long t;

	l = parse_con();
	for(;;){
		t = yypeek(0);
		if(t != '*' && t != '/' && t != '%')
			break;
		yyget();
		r = parse_con();
		if(t == '*')
			l = l * r;
		else if(t == '/')
			l = l / r;
		else
			l = l % r;
	}
	return l;
}

static vlong
parse_expr(void)
{
	return parse_or();
}

static vlong
parse_con(void)
{
	long t;
	vlong v;
	Sym *s;

	t = yyget();
	switch(t){
	case LCONST:
		return yylval.lval;
	case LVAR:
		s = yylval.sym;
		return s->value;
	case '-':
		v = parse_con();
		return -v;
	case '+':
		v = parse_con();
		return v;
	case '~':
		v = parse_con();
		return ~v;
	case '(':
		v = parse_expr();
		yyexpect(')');
		return v;
	default:
		yysyntax_error();
		return 0;
	}
}

static vlong
parse_offset(void)
{
	long t;
	vlong v;

	t = yypeek(0);
	if(t == '+'){
		yyget();
		v = parse_con();
		return v;
	}
	if(t == '-'){
		yyget();
		v = parse_con();
		return -v;
	}
	return 0;
}

static vlong
parse_pointer(void)
{
	long t;

	t = yyget();
	switch(t){
	case LSB:
		return yylval.lval;
	case LSP:
		return D_AUTO;
	case LFP:
		return yylval.lval;
	default:
		yysyntax_error();
		return 0;
	}
}

static Gen
parse_reg(void)
{
	long t;
	Gen g;

	t = yyget();
	switch(t){
	case LBREG:
	case LFREG:
	case LLREG:
	case LMREG:
	case LSREG:
	case LXREG:
	case LYREG:
		g = nullgen;
		g.type = yylval.lval;
		return g;
	case LSP:
		g = nullgen;
		g.type = D_SP;
		return g;
	default:
		yysyntax_error();
		g = nullgen;
		return g;
	}
}

static Gen
parse_nam(void)
{
	long t;
	Sym *sym;
	vlong off;
	vlong ptr;
	Gen g;

	t = yyget();
	if(t != LNAME)
		yysyntax_error();
	sym = yylval.sym;
	if(yypeek(0) == '<'){
		yyget();
		yyexpect('>');
		off = parse_offset();
		yyexpect('(');
		t = yyget();
		if(t != LSB)
			yysyntax_error();
		yyexpect(')');
		g = nullgen;
		g.type = D_STATIC;
		g.sym = sym;
		g.offset = off;
		return g;
	}
	off = parse_offset();
	yyexpect('(');
	ptr = parse_pointer();
	yyexpect(')');
	g = nullgen;
	g.type = ptr;
	g.sym = sym;
	g.offset = off;
	return g;
}

static Gen
parse_omem_rest(vlong c)
{
	long t;
	vlong c2;
	long r1, r2;
	Gen g;

	if(yypeek(0) != '('){
		g = nullgen;
		g.type = D_INDIR+D_NONE;
		g.offset = c;
		return g;
	}
	yyget();
	t = yypeek(0);
	if(t == LSP){
		yyget();
		yyexpect(')');
		g = nullgen;
		g.type = D_INDIR+D_SP;
		g.offset = c;
		return g;
	}
	if(t == LSREG){
		yyget();
		r1 = yylval.lval;
		yyexpect(')');
		g = nullgen;
		g.type = D_INDIR+r1;
		g.offset = c;
		return g;
	}
	if(t != LLREG)
		yysyntax_error();
	yyget();
	r1 = yylval.lval;
	t = yypeek(0);
	if(t == '*'){
		yyget();
		c2 = parse_con();
		yyexpect(')');
		g = nullgen;
		g.type = D_INDIR+D_NONE;
		g.offset = c;
		g.index = r1;
		g.scale = c2;
		checkscale(g.scale);
		return g;
	}
	if(t != ')')
		yysyntax_error();
	yyget();
	if(yypeek(0) == '('){
		yyget();
		t = yyget();
		if(t != LLREG)
			yysyntax_error();
		r2 = yylval.lval;
		yyexpect('*');
		c2 = parse_con();
		yyexpect(')');
		g = nullgen;
		g.type = D_INDIR+r1;
		g.offset = c;
		g.index = r2;
		g.scale = c2;
		checkscale(g.scale);
		return g;
	}
	g = nullgen;
	g.type = D_INDIR+r1;
	g.offset = c;
	return g;
}

static Gen
parse_omem(void)
{
	long t;
	long r1, r2;
	vlong c;
	Gen g;

	if(yypeek(0) == '(' && (yypeek(1) == LLREG || yypeek(1) == LSP)){
		yyget();
		t = yypeek(0);
		if(t == LSP){
			yyget();
			yyexpect(')');
			g = nullgen;
			g.type = D_INDIR+D_SP;
			return g;
		}
		t = yyget();
		if(t != LLREG)
			yysyntax_error();
		r1 = yylval.lval;
		t = yypeek(0);
		if(t == '*'){
			yyget();
			c = parse_con();
			yyexpect(')');
			g = nullgen;
			g.type = D_INDIR+D_NONE;
			g.index = r1;
			g.scale = c;
			checkscale(g.scale);
			return g;
		}
		if(t != ')')
			yysyntax_error();
		yyget();
		if(yypeek(0) == '('){
			yyget();
			t = yyget();
			if(t != LLREG)
				yysyntax_error();
			r2 = yylval.lval;
			yyexpect('*');
			c = parse_con();
			yyexpect(')');
			g = nullgen;
			g.type = D_INDIR+r1;
			g.index = r2;
			g.scale = c;
			checkscale(g.scale);
			return g;
		}
		g = nullgen;
		g.type = D_INDIR+r1;
		return g;
	}
	if(!isconstart(yypeek(0)))
		yysyntax_error();
	c = parse_con();
	return parse_omem_rest(c);
}

static Gen
parse_nmem(void)
{
	long t;
	long r;
	vlong c;
	Gen g;

	if(yypeek(0) != LNAME)
		yysyntax_error();
	g = parse_nam();
	if(yypeek(0) != '(')
		return g;
	yyget();
	t = yyget();
	if(t != LLREG)
		yysyntax_error();
	r = yylval.lval;
	yyexpect('*');
	c = parse_con();
	yyexpect(')');
	g.index = r;
	g.scale = c;
	checkscale(g.scale);
	return g;
}

static Gen
parse_mem(void)
{
	if(yypeek(0) == LNAME)
		return parse_nmem();
	return parse_omem();
}

static Gen
parse_imm(void)
{
	long t;
	vlong c;
	double d;
	Gen g;
	Gen n;

	yyexpect('$');
	t = yypeek(0);
	if(t == LSCONST){
		yyget();
		g = nullgen;
		g.type = D_SCONST;
		memcpy(g.sval, yylval.sval, sizeof(g.sval));
		return g;
	}
	if(t == LFCONST){
		yyget();
		d = yylval.dval;
		g = nullgen;
		g.type = D_FCONST;
		g.dval = d;
		return g;
	}
	if(t == '(' && yypeek(1) == LFCONST){
		yyget();
		yyget();
		d = yylval.dval;
		yyexpect(')');
		g = nullgen;
		g.type = D_FCONST;
		g.dval = d;
		return g;
	}
	if(t == '-' && yypeek(1) == LFCONST){
		yyget();
		yyget();
		d = yylval.dval;
		g = nullgen;
		g.type = D_FCONST;
		g.dval = -d;
		return g;
	}
	if(t == LNAME){
		n = parse_nam();
		g = n;
		g.index = n.type;
		g.type = D_ADDR;
		return g;
	}
	c = parse_con();
	g = nullgen;
	g.type = D_CONST;
	g.offset = c;
	return g;
}

static Gen
parse_rel(void)
{
	long t;
	Sym *s;
	vlong c, off;
	Gen g;

	t = yypeek(0);
	if(t == LLAB){
		yyget();
		s = yylval.sym;
		off = parse_offset();
		g = nullgen;
		g.type = D_BRANCH;
		g.sym = s;
		g.offset = s->value + off;
		return g;
	}
	if(t == LNAME){
		yyget();
		s = yylval.sym;
		off = parse_offset();
		if(pass == 2)
			yyerror("undefined label: %s", s->name);
		g = nullgen;
		g.type = D_BRANCH;
		g.sym = s;
		g.offset = off;
		return g;
	}
	c = parse_con();
	yyexpect('(');
	t = yyget();
	if(t != LPC)
		yysyntax_error();
	yyexpect(')');
	g = nullgen;
	g.type = D_BRANCH;
	g.offset = c + pc;
	return g;
}

static Gen
parse_rem(void)
{
	if(isregtok(yypeek(0)))
		return parse_reg();
	return parse_mem();
}

static Gen
parse_rim(void)
{
	if(yypeek(0) == '$')
		return parse_imm();
	return parse_rem();
}

static Gen
parse_rom(void)
{
	long t, t2;
	Sym *sym;
	vlong c, off, ptr;
	long r1;
	Gen g;

	t = yypeek(0);
	if(t == '*'){
		yyget();
		t2 = yypeek(0);
		if(isregtok(t2)){
			g = parse_reg();
			return g;
		}
		g = parse_omem();
		return g;
	}
	if(t == '$')
		return parse_imm();
	if(isregtok(t))
		return parse_reg();
	if(t == LNAME){
		yyget();
		sym = yylval.sym;
		if(yypeek(0) == '<'){
			yyget();
			yyexpect('>');
			off = parse_offset();
			yyexpect('(');
			t = yyget();
			if(t != LSB)
				yysyntax_error();
			yyexpect(')');
			g = nullgen;
			g.type = D_STATIC;
			g.sym = sym;
			g.offset = off;
			if(yypeek(0) == '('){
				yyget();
				t = yyget();
				if(t != LLREG)
					yysyntax_error();
				r1 = yylval.lval;
				yyexpect('*');
				c = parse_con();
				yyexpect(')');
				g.index = r1;
				g.scale = c;
				checkscale(g.scale);
			}
			return g;
		}
		off = parse_offset();
		if(yypeek(0) == '(' && yypeek(1) != LPC){
			yyget();
			ptr = parse_pointer();
			yyexpect(')');
			g = nullgen;
			g.type = ptr;
			g.sym = sym;
			g.offset = off;
			if(yypeek(0) == '('){
				yyget();
				t = yyget();
				if(t != LLREG)
					yysyntax_error();
				r1 = yylval.lval;
				yyexpect('*');
				c = parse_con();
				yyexpect(')');
				g.index = r1;
				g.scale = c;
				checkscale(g.scale);
			}
			return g;
		}
		if(pass == 2)
			yyerror("undefined label: %s", sym->name);
		g = nullgen;
		g.type = D_BRANCH;
		g.sym = sym;
		g.offset = off;
		return g;
	}
	if(t == LLAB)
		return parse_rel();
	if(isconstart(t)){
		if(t == '(' && (yypeek(1) == LLREG || yypeek(1) == LSP))
			return parse_omem();
		c = parse_con();
		if(yypeek(0) == '(' && yypeek(1) == LPC){
			yyget();
			yyget();
			yyexpect(')');
			g = nullgen;
			g.type = D_BRANCH;
			g.offset = c + pc;
			return g;
		}
		return parse_omem_rest(c);
	}
	yysyntax_error();
	g = nullgen;
	return g;
}

static Gen2
parse_nonnon(void)
{
	Gen2 g;

	if(yypeek(0) == ',')
		yyget();
	g.from = nullgen;
	g.to = nullgen;
	return g;
}

static Gen2
parse_rimrem(void)
{
	Gen a, b;
	Gen2 g;

	a = parse_rim();
	yyexpect(',');
	b = parse_rem();
	g.from = a;
	g.to = b;
	return g;
}

static Gen2
parse_remrim(void)
{
	Gen a, b;
	Gen2 g;

	a = parse_rem();
	yyexpect(',');
	b = parse_rim();
	g.from = a;
	g.to = b;
	return g;
}

static Gen2
parse_rimnon(void)
{
	Gen a;
	Gen2 g;

	a = parse_rim();
	if(yypeek(0) == ',')
		yyget();
	g.from = a;
	g.to = nullgen;
	return g;
}

static Gen2
parse_nonrem(void)
{
	Gen a;
	Gen2 g;

	if(yypeek(0) == ','){
		yyget();
		a = parse_rem();
		g.from = nullgen;
		g.to = a;
		return g;
	}
	a = parse_rem();
	g.from = nullgen;
	g.to = a;
	return g;
}

static Gen2
parse_nonrel(void)
{
	Gen a;
	Gen2 g;

	if(yypeek(0) == ','){
		yyget();
		a = parse_rel();
		g.from = nullgen;
		g.to = a;
		return g;
	}
	a = parse_rel();
	g.from = nullgen;
	g.to = a;
	return g;
}

static Gen2
parse_spec1(void)
{
	Gen a, b;
	vlong c;
	Gen2 g;

	a = parse_nam();
	yyexpect('/');
	c = parse_con();
	yyexpect(',');
	b = parse_imm();
	g.from = a;
	g.from.scale = c;
	g.to = b;
	return g;
}

static Gen2
parse_spec2(void)
{
	Gen a, b;
	vlong c;
	Gen2 g;

	a = parse_mem();
	yyexpect(',');
	if(yypeek(0) == '$'){
		b = parse_imm();
		g.from = a;
		g.to = b;
		return g;
	}
	c = parse_con();
	yyexpect(',');
	b = parse_imm();
	g.from = a;
	g.from.scale = c;
	g.to = b;
	return g;
}

static Gen2
parse_spec3(void)
{
	Gen a;
	Gen2 g;

	if(yypeek(0) == ','){
		yyget();
		a = parse_rom();
		g.from = nullgen;
		g.to = a;
		return g;
	}
	a = parse_rom();
	g.from = nullgen;
	g.to = a;
	return g;
}

static Gen2
parse_spec4(void)
{
	long t, t2;

	t = yypeek(0);
	if(t == ';' || t == 0 || t == EOF)
		return parse_nonnon();
	if(t == ','){
		t2 = yypeek(1);
		if(t2 == ';' || t2 == 0 || t2 == EOF)
			return parse_nonnon();
		if(isregtok(t2) || t2 == LNAME || isconstart(t2) || t2 == '(')
			return parse_nonrem();
		return parse_nonnon();
	}
	if(t == '$')
		return parse_nonnon();
	if(isregtok(t) || t == LNAME || isconstart(t) || t == '(')
		return parse_nonrem();
	return parse_nonnon();
}

static Gen2
parse_spec5(void)
{
	Gen a, b;
	long r;
	Gen2 g;

	a = parse_rim();
	yyexpect(',');
	b = parse_rem();
	g.from = a;
	g.to = b;
	if(yypeek(0) == ':'){
		yyget();
		r = yyget();
		if(r != LLREG)
			yysyntax_error();
		r = yylval.lval;
		if(g.from.index != D_NONE)
			yyerror("dp shift with lhs index");
		g.from.index = r;
	}
	return g;
}

static Gen2
parse_spec6(void)
{
	Gen a, b;
	long r;
	Gen2 g;

	a = parse_rim();
	yyexpect(',');
	b = parse_rem();
	g.from = a;
	g.to = b;
	if(yypeek(0) == ':'){
		yyget();
		r = yyget();
		if(r != LSREG)
			yysyntax_error();
		r = yylval.lval;
		if(g.to.index != D_NONE)
			yyerror("dp move with lhs index");
		g.to.index = r;
	}
	return g;
}

static Gen2
parse_spec7(void)
{
	Gen a, b;
	Gen2 g;

	a = parse_rim();
	if(yypeek(0) != ','){
		g.from = a;
		g.to = nullgen;
		return g;
	}
	yyget();
	if(yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF){
		g.from = a;
		g.to = nullgen;
		return g;
	}
	b = parse_rem();
	g.from = a;
	g.to = b;
	return g;
}

static int
count_commas_to_semi(void)
{
	int i, n;
	long t;

	n = 0;
	for(i = 0; i < 200; i++){
		t = yypeek(i);
		if(t == ';' || t == 0 || t == EOF)
			break;
		if(t == ',')
			n++;
	}
	return n;
}

static Gen2
parse_spec8(void)
{
	Gen a, b, c;
	vlong o;
	Gen2 g;

	/* spec8: reg ',' rem ',' con (2 commas)
	   vs reg ',' reg ',' rem ',' con (3 commas, VCMPPS/VCMPPD).
	   Raw ',' never appears inside reg/rem/con, so count commas to ';'. */
	if(count_commas_to_semi() >= 3){
		a = parse_reg();
		yyexpect(',');
		b = parse_reg();
		yyexpect(',');
		c = parse_rem();
		yyexpect(',');
		o = parse_con();
		g.from = a;
		if(!isxyreg(b.type))
			yyerror("second source operand must be X/Y register");
		g.from.index = b.type;
		g.to = c;
		g.from.offset = o;
		return g;
	}
	a = parse_reg();
	yyexpect(',');
	b = parse_rem();
	yyexpect(',');
	o = parse_con();
	g.from = a;
	g.to = b;
	g.from.offset = o;
	return g;
}

static Gen2
parse_spec9(void)
{
	Gen a, b, c, d;
	Gen2 g;

	/* spec9: imm ',' rem ',' reg (2 commas)
	   vs imm ',' rem ',' reg ',' reg (3 commas, VEX).
	   Raw ',' never inside imm/rem/reg, so count commas. */
	if(count_commas_to_semi() >= 3){
		a = parse_imm();
		yyexpect(',');
		b = parse_rem();
		yyexpect(',');
		c = parse_reg();
		yyexpect(',');
		d = parse_reg();
		g.from = b;
		g.to = d;
		if(a.type != D_CONST)
			yyerror("illegal constant");
		g.to.offset = a.offset;
		if(!isxyreg(c.type))
			yyerror("second source operand must be X/Y register");
		g.to.index = c.type;
		return g;
	}
	a = parse_imm();
	yyexpect(',');
	b = parse_rem();
	yyexpect(',');
	c = parse_reg();
	g.from = b;
	g.to = c;
	if(a.type != D_CONST)
		yyerror("illegal constant");
	g.to.offset = a.offset;
	return g;
}

static Gen2
parse_spec10(void)
{
	Gen a, b;
	vlong c;
	Gen2 g;

	a = parse_mem();
	yyexpect(',');
	if(yypeek(0) == '$'){
		b = parse_imm();
		g.from = a;
		g.to = b;
		return g;
	}
	c = parse_con();
	yyexpect(',');
	b = parse_imm();
	g.from = a;
	g.from.scale = c;
	g.to = b;
	return g;
}

static Gen2
parse_spec11(void)
{
	Gen a, b, c;
	Gen2 g;

	/* spec11: rimrem (rim ',' rem, 1 comma)
	   vs rim ',' reg ',' rem (2 commas, VEX 3-operand).
	   Raw ',' never inside rim/reg/rem, so count commas to ';'. */
	if(count_commas_to_semi() >= 2){
		a = parse_rim();
		yyexpect(',');
		b = parse_reg();
		yyexpect(',');
		c = parse_rem();
		g.from = a;
		g.to = c;
		if(isxyreg(b.type)){
			if(isxyreg(a.type))
				g.from.index = b.type;
			else if(isxyreg(c.type))
				g.to.index = b.type;
		} else
			yyerror("second source operand must be X or Y register");
		return g;
	}
	a = parse_rim();
	yyexpect(',');
	b = parse_rem();
	g.from = a;
	g.to = b;
	return g;
}

static Gen2
parse_spec12(void)
{
	Gen a;
	Gen2 g;

	if(yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF){
		g.from = nullgen;
		g.to = nullgen;
		return g;
	}
	a = parse_imm();
	g.from = a;
	g.to = nullgen;
	return g;
}

static void
parse_inst(void)
{
	long t;
	Sym *s;
	vlong v;
	Gen2 g;

	t = yypeek(0);
	switch(t){
	case LNAME:
		yyget();
		s = yylval.sym;
		yyexpect('=');
		v = parse_expr();
		s->type = LVAR;
		s->value = v;
		return;
	case LVAR:
		yyget();
		s = yylval.sym;
		yyexpect('=');
		v = parse_expr();
		if(s->value != v)
			yyerror("redeclaration of %s", s->name);
		s->value = v;
		return;
	case LTYPE0:
		yyget();
		t = yylval.lval;
		g = parse_nonnon();
		outcode(t, &g);
		return;
	case LTYPE1:
		yyget();
		t = yylval.lval;
		g = parse_nonrem();
		outcode(t, &g);
		return;
	case LTYPE2:
		yyget();
		t = yylval.lval;
		g = parse_rimnon();
		outcode(t, &g);
		return;
	case LTYPE3:
		yyget();
		t = yylval.lval;
		g = parse_rimrem();
		outcode(t, &g);
		return;
	case LTYPE4:
		yyget();
		t = yylval.lval;
		g = parse_remrim();
		outcode(t, &g);
		return;
	case LTYPER:
		yyget();
		t = yylval.lval;
		g = parse_nonrel();
		outcode(t, &g);
		return;
	case LTYPED:
		yyget();
		t = yylval.lval;
		g = parse_spec1();
		outcode(t, &g);
		return;
	case LTYPET:
		yyget();
		t = yylval.lval;
		g = parse_spec2();
		outcode(t, &g);
		return;
	case LTYPEC:
		yyget();
		t = yylval.lval;
		g = parse_spec3();
		outcode(t, &g);
		return;
	case LTYPEN:
		yyget();
		t = yylval.lval;
		g = parse_spec4();
		outcode(t, &g);
		return;
	case LTYPES:
		yyget();
		t = yylval.lval;
		g = parse_spec5();
		outcode(t, &g);
		return;
	case LTYPEM:
		yyget();
		t = yylval.lval;
		g = parse_spec6();
		outcode(t, &g);
		return;
	case LTYPEI:
		yyget();
		t = yylval.lval;
		g = parse_spec7();
		outcode(t, &g);
		return;
	case LTYPEXC:
		yyget();
		t = yylval.lval;
		g = parse_spec8();
		outcode(t, &g);
		return;
	case LTYPEX:
		yyget();
		t = yylval.lval;
		g = parse_spec9();
		outcode(t, &g);
		return;
	case LTYPEG:
		yyget();
		t = yylval.lval;
		g = parse_spec10();
		outcode(t, &g);
		return;
	case LTYPEY:
		yyget();
		t = yylval.lval;
		g = parse_spec11();
		outcode(t, &g);
		return;
	case LTYPERT:
		yyget();
		t = yylval.lval;
		g = parse_spec12();
		outcode(t, &g);
		return;
	default:
		yysyntax_error();
		return;
	}
}

static void
parse_line(void)
{
	long t;
	Sym *s;

	if(setjmp(yyrec)){
		yyskip_to_semi();
		return;
	}
	for(;;){
		if(yypeek(0) == LLAB && yypeek(1) == ':'){
			yyget();
			s = yylval.sym;
			yyget();
			if(s->value != pc)
				yyerror("redeclaration of %s", s->name);
			s->value = pc;
			continue;
		}
		if(yypeek(0) == LNAME && yypeek(1) == ':'){
			yyget();
			s = yylval.sym;
			yyget();
			s->type = LLAB;
			s->value = pc;
			continue;
		}
		break;
	}
	t = yypeek(0);
	if(t == ';'){
		yyget();
		return;
	}
	if(t == 0 || t == EOF)
		return;
	parse_inst();
	t = yyget();
	if(t != ';')
		yysyntax_error();
}

int
yyparse(void)
{
	long t;

	yyhave = 0;
	for(;;){
		t = yypeek(0);
		if(t == 0 || t == EOF)
			break;
		parse_line();
	}
	return 0;
}
