#include "a.h"
#include "a_parse.h"

YYSTYPE yylval;
/*
 * Hand-written recursive-descent replacement for a.y (LALR, 7a/arm64).
 *
 * Original yacc grammar (precedence low -> high):
 *	%left '|'
 *	%left '^'
 *	%left '&'
 *	%left '<' '>'
 *	%left '+' '-'
 *	%left '*' '/' '%'
 * prog: | prog line
 * line: LLAB ':' line | LNAME ':' line | LNAME '=' expr ';'
 *     | LVAR '=' expr ';' | ';' | inst ';' | error ';'
 * inst: LTYPE0 comma | LTYPE1 imsr ',' spreg ',' reg
 *     | LTYPE1 imsr ',' spreg ',' | LTYPE1 imsr ',' reg
 *     | LTYPE2 imsr ',' reg | LTYPE3 gen ',' gen | LMOVK imm ',' reg
 *     | LMOVK imm '<' '<' con ',' reg | LTYPE4 comma rel
 *     | LTYPE4 comma nireg | LTYPE5 comma rel | LTYPE6 comma gen
 *     | LTYPE6 | LTYPE7 imsr ',' spreg comma | LTYPE8 reg ',' rel
 *     | LTYPER cond ',' reg | LTYPES cond ',' reg ',' reg ',' reg
 *     | LTYPES cond ',' reg ',' reg | LTYPET imm ',' reg ',' rel
 *     | LTYPEU cond ',' imsr ',' reg ',' imm comma | LTYPEV rel ',' reg
 *     | LTYPEV '$' name ',' reg | LTYPEY imm ',' imm ',' spreg ',' reg
 *     | LTYPEP imm ',' reg ',' spreg ',' reg | LTYPEA comma | LTYPEA reg
 *     | LTYPEQ comma | LTYPEQ reg comma | LTYPEQ freg comma
 *     | LTYPEQ ',' reg | LTYPEQ ',' freg | LTYPEB name ',' imm
 *     | LTYPEB name ',' con ',' imm | LTYPEC name '/' con ',' ximm
 *     | LTYPED reg ',' reg | LTYPEH comma ximm | LTYPEI freg ',' freg
 *     | LTYPEK frcon ',' freg | LTYPEK frcon ',' freg ',' freg
 *     | LTYPEL frcon ',' freg comma | LTYPEF cond ',' freg ',' freg ',' imm comma
 *     | LTYPE9 freg ',' freg ',' freg ',' freg comma
 *     | LFCSEL cond ',' freg ',' freg ',' freg
 *     | LTYPEW vgen ',' vgen | LTYPEW vgen ',' vgen ',' vgen
 *     | LTYPEJ gen ',' sreg ',' gen | LTYPEM reg ',' reg ',' sreg ',' reg
 *     | LTYPEN sysarg | LTYPEN reg ',' sysarg | LTYPEO sysarg ',' reg
 *     | LDMB imm | LSTXR reg ',' gen ',' sreg | LTYPEE comma
 * con/expr/offset/pointer as in other assemblers.
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
 * error calls yyerror("syntax error") then skips to ';' or EOF (0/-1)
 * and continues.  Semantic yyerror calls in actions do not skip.
 */

static long yypeek(int n);
static long yyget(void);
static void yysyntax_error(void);
static void yyexpect(long t);
static void yyskip_to_semi(void);
static void parse_comma(void);
static int isconstart(long t);
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
static vlong parse_sreg(void);
static vlong parse_spreg(void);
static vlong parse_scon(void);
static vlong parse_indexreg(void);
static vlong parse_vset(void);
static vlong parse_vreglist(void);
static Gen parse_cond(void);
static Gen parse_sysarg(void);
static Gen parse_rel(void);
static Gen parse_ximm(void);
static Gen parse_fcon(void);
static Gen parse_gen(void);
static Gen parse_nireg(void);
static Gen parse_oreg(void);
static Gen parse_ioreg(void);
static Gen parse_imsr(void);
static Gen parse_imm(void);
static Gen parse_reg(void);
static Gen parse_shiftop(void);
static Gen parse_extreg(void);
static Gen parse_spr(void);
static Gen parse_frcon(void);
static Gen parse_freg(void);
static Gen parse_vgen(void);
static Gen parse_vlane(void);
static Gen parse_vreg(void);
static Gen parse_name(void);
static void parse_inst(void);
static void parse_line(void);

/* lookahead buffer over yylex() */
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

static void
parse_comma(void)
{
	while(yypeek(0) == ',')
		yyget();
}

static int
isconstart(long t)
{
	return t == LCONST || t == LVAR || t == '-' || t == '+' || t == '~' || t == '(';
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
	case LSP:
	case LFP:
		return yylval.lval;
	default:
		yysyntax_error();
		return 0;
	}
}

static vlong
parse_sreg(void)
{
	long t;
	vlong v;

	t = yypeek(0);
	if(t == LREG){
		yyget();
		return yylval.lval;
	}
	if(t == LR){
		yyget();
		yyexpect('(');
		v = parse_expr();
		yyexpect(')');
		if(v < 0 || v >= NREG)
			print("register value out of range\n");
		return v;
	}
	yysyntax_error();
	return 0;
}

static vlong
parse_spreg(void)
{
	long t;

	t = yypeek(0);
	if(t == LSP){
		yyget();
		return REGSP;
	}
	return parse_sreg();
}

static vlong
parse_scon(void)
{
	vlong v;

	v = parse_con();
	if(v < 0 || v >= 64)
		yyerror("shift value out of range");
	return v & 0x3F;
}

static vlong
parse_indexreg(void)
{
	vlong r, e;

	r = parse_sreg();
	if(yypeek(0) == LEXT){
		yyget();
		e = yylval.lval;
		return (e << 8) | r;
	}
	return (3 << 8) | r;
}

static Gen
parse_cond(void)
{
	long t;
	Gen g;

	t = yyget();
	if(t != LCOND)
		yysyntax_error();
	g = nullgen;
	g.type = D_COND;
	g.reg = yylval.lval;
	return g;
}

static Gen
parse_sysarg(void)
{
	vlong a, b, c, d;
	Gen g;

	if(yypeek(0) == '$' || yypeek(0) == LFCONST || yypeek(0) == LSCONST || yypeek(0) == LNAME || yypeek(0) == '('){
		/* could be imm (which starts '$' or is fcon) vs con ',' ...?
		   sysarg: con ',' con ',' con ',' con | imm.
		   imm starts '$' (ximm/fcon/imm all start '$').
		   con never starts '$'. So if '$' then imm, else con-tuple. */
		if(yypeek(0) == '$')
			return parse_imm();
	}
	if(yypeek(0) == '$')
		return parse_imm();
	/* try con-tuple: need 3 commas; imm has no commas. Count commas to ';'. */
	{
		int i, n;
		long t2;

		n = 0;
		for(i = 0; i < 200; i++){
			t2 = yypeek(i);
			if(t2 == ';' || t2 == 0 || t2 == EOF)
				break;
			if(t2 == ',')
				n++;
		}
		if(n < 3)
			return parse_imm();
	}
	a = parse_con();
	yyexpect(',');
	b = parse_con();
	yyexpect(',');
	c = parse_con();
	yyexpect(',');
	d = parse_con();
	g = nullgen;
	g.type = D_CONST;
	g.offset = SYSARG4(a, b, c, d);
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
parse_fcon(void)
{
	long t;
	double d;
	Gen g;

	t = yypeek(0);
	if(t == '$'){
		yyget();
		t = yypeek(0);
		if(t == LFCONST){
			yyget();
			d = yylval.dval;
			g = nullgen;
			g.type = D_FCONST;
			g.dval = d;
			return g;
		}
		if(t == '-'){
			yyget();
			t = yyget();
			if(t != LFCONST)
				yysyntax_error();
			d = yylval.dval;
			g = nullgen;
			g.type = D_FCONST;
			g.dval = -d;
			return g;
		}
		yysyntax_error();
		g = nullgen;
		return g;
	}
	yysyntax_error();
	g = nullgen;
	return g;
}

static Gen
parse_ximm(void)
{
	long t;
	vlong c;
	Gen g;
	Gen o;

	t = yypeek(0);
	if(t == '$'){
		/* '$' con | '$' oreg | '$' '*' '$' oreg | '$' LSCONST | fcon */
		/* fcon starts '$' LFCONST or '$' '-' LFCONST */
		if(yypeek(1) == LFCONST || (yypeek(1) == '-' && yypeek(2) == LFCONST))
			return parse_fcon();
		if(yypeek(1) == LSCONST){
			yyget();
			yyget();
			g = nullgen;
			g.type = D_SCONST;
			memmove(g.sval, yylval.sval, sizeof(g.sval));
			return g;
		}
		if(yypeek(1) == '*' ){
			yyget();
			yyget();
			yyexpect('$');
			o = parse_oreg();
			g = o;
			g.type = D_OCONST;
			return g;
		}
		/* '$' oreg vs '$' con: oreg starts with LNAME/con/'('/name patterns?
		   oreg: name | name '(' sreg ')' | ioreg.
		   name starts LNAME or con '(' pointer ')' (con-start).
		   con starts LCONST/LVAR/... So overlap on con-start and LNAME?
		   '$' LNAME ... could be oreg (name) vs con? con never starts LNAME,
		   so LNAME => oreg. '$' '(' ... could be oreg ioreg '(' sreg ')' vs
		   con '(' expr ')'? ioreg '(' sreg ')' has sreg (LREG/LR) inside,
		   con '(' expr ')' has expr inside. Distinguish by inner: if '(' sreg ')'?
		   Simplify: if after '$' next is LNAME then oreg; else if '(' then need
		   deeper check; else con. */
		if(yypeek(1) == LNAME){
			yyget();
			o = parse_oreg();
			g = o;
			g.type = D_CONST;
			return g;
		}
		if(yypeek(1) == '('){
			/* peek inside: '(' sreg ')' vs '(' expr ')'?
			   sreg is LREG or LR '(' ...; expr starts LCONST/LVAR/...?
			   If yypeek(2) is LREG/LR/LSP then likely ioreg. */
			if(yypeek(2) == LREG || yypeek(2) == LR || yypeek(2) == LSP){
				yyget();
				o = parse_oreg();
				g = o;
				g.type = D_CONST;
				return g;
			}
			/* check for con '(' sreg ')' form? That's oreg with name '(' sreg ')'?
			   Actually oreg includes name '(' sreg ')' where name may be con...
			   Complex. Fall back: try oreg if it parses with '(' sreg ')'? */
		}
		/* default: try oreg when it looks like name/oreg, else con */
		/* If next after '$' is con-start and the token after con is '(' sreg ')'?
		   We handle by attempting oreg parse with save/restore? Simpler:
		   if yypeek(1) is '(' or con-start and yypeek further suggests oreg,
		   prefer oreg? Yacc would shift. For hand parser, try oreg first if
		   it contains '(' sreg ')' pattern; else con. */
		/* Conservative: if '$' followed by LCONST/LVAR and then '(' then oreg,
		   else con. Check yypeek(2)=='('? */
		yyget();
		/* now at token after '$': decide oreg vs con by scanning for '(' sreg ')'? */
		/* Use comma/semicolon lookahead: oreg may contain '(' ... ')' but con may too.
		   Easiest: attempt oreg with trial that does not longjmp to line level.
		   Instead, inline: if next is '(' or LNAME or con-start with '(' after, do oreg. */
		/* For simplicity, if next starts oreg-like (LNAME or '(' with sreg), do oreg. */
		t = yypeek(0);
		if(t == LNAME || t == '('){
			/* need to distinguish '(' sreg ')' (ioreg) vs '(' expr ')' (con).
			   If '(' then check inner. */
			if(t == '('){
				long inner;

				inner = yypeek(1);
				if(inner == LREG || inner == LR || inner == LSP){
					o = parse_oreg();
					g = o;
					g.type = D_CONST;
					return g;
				}
				/* '(' con ')'? Could be oreg's name '(' sreg ')' where name is con?
				   name: con '(' pointer ')' - inner would be con-start, not sreg.
				   Hard. Fall through to con. */
			} else {
				o = parse_oreg();
				g = o;
				g.type = D_CONST;
				return g;
			}
		}
		c = parse_con();
		g = nullgen;
		g.type = D_CONST;
		g.offset = c;
		return g;
	}
	return parse_fcon();
}

static Gen
parse_imm(void)
{
	vlong c;
	Gen g;

	yyexpect('$');
	c = parse_con();
	g = nullgen;
	g.type = D_CONST;
	g.offset = c;
	return g;
}

static Gen
parse_reg(void)
{
	long t;
	vlong r;
	Gen g;

	t = yypeek(0);
	if(t == LSP){
		yyget();
		g = nullgen;
		g.type = D_SP;
		g.reg = REGSP;
		return g;
	}
	r = parse_sreg();
	g = nullgen;
	g.type = D_REG;
	g.reg = r;
	return g;
}

static Gen
parse_shiftop(void)
{
	vlong r, s;
	long t;
	Gen g;

	r = parse_sreg();
	t = yypeek(0);
	if(t == '<'){
		yyget();
		yyexpect('<');
		s = parse_scon();
		g = nullgen;
		g.type = D_SHIFT;
		g.offset = (r << 16) | (s << 10) | (0 << 22);
		return g;
	}
	if(t == '>'){
		yyget();
		t = yyget();
		if(t != '>')
			yysyntax_error();
		s = parse_scon();
		g = nullgen;
		g.type = D_SHIFT;
		g.offset = ((r&0x1F) << 16) | (s << 10) | (1 << 22);
		return g;
	}
	if(t == '-'){
		yyget();
		yyexpect('>');
		s = parse_scon();
		g = nullgen;
		g.type = D_SHIFT;
		g.offset = (r << 16) | (s << 10) | (2 << 22);
		return g;
	}
	if(t == LAT){
		yyget();
		yyexpect('>');
		s = parse_scon();
		g = nullgen;
		g.type = D_SHIFT;
		g.offset = (r << 16) | (s << 10) | (3 << 22);
		return g;
	}
	yysyntax_error();
	g = nullgen;
	return g;
}

static Gen
parse_extreg(void)
{
	vlong r, e, s;
	Gen g;

	r = parse_sreg();
	if(yypeek(0) != LEXT){
		g = nullgen;
		g.type = D_REG;
		g.reg = r;
		return g;
	}
	yyget();
	e = yylval.lval;
	if(yypeek(0) != '<'){
		g = nullgen;
		g.type = D_EXTREG;
		g.reg = r;
		g.offset = (r << 16) | (e << 13);
		return g;
	}
	yyget();
	yyexpect('<');
	s = parse_con();
	if(s < 0 || s > 4)
		yyerror("shift value out of range");
	g = nullgen;
	g.type = D_EXTREG;
	g.reg = r;
	g.offset = (r << 16) | (e << 13) | (s << 10);
	return g;
}

static Gen
parse_imsr(void)
{
	long t;

	t = yypeek(0);
	if(t == '$')
		return parse_imm();
	/* shift starts sreg '<'/'>'/'-'/LAT; extreg starts sreg [LEXT].
	   Both start sreg. Distinguish: after sreg, if '<'/'>'/'-'/LAT/LEXT then
	   shift/extreg, else plain? imsr: imm | shift | extreg. extreg can be
	   plain sreg (D_REG). shift always has operator. So if sreg followed by
	   operator then shift/extreg-with-ext, else extreg-plain (D_REG). */
	/* Peek: sreg is LREG or LR '(' ... ')'. Need to find token after sreg.
	   For LREG case, next token decides. For LR '(' ... ')' case, need to skip. */
	if(t == LREG || t == LR){
		/* scan ahead past sreg */
		int idx;

		idx = 0;
		if(t == LREG)
			idx = 1;
		else{
			/* LR '(' expr ')' : find matching ')' */
			int depth, j;

			depth = 0;
			for(j = 0; j < 200; j++){
				t = yypeek(j);
				if(t == ';' || t == 0 || t == EOF)
					break;
				if(t == '(')
					depth++;
				if(t == ')'){
					depth--;
					if(depth == 0){
						idx = j+1;
						break;
					}
				}
			}
			if(idx == 0)
				yysyntax_error();
		}
		t = yypeek(idx);
		if(t == '<' || t == '>' || t == '-' || t == LAT)
			return parse_shiftop();
		return parse_extreg();
	}
	yysyntax_error();
	{
		Gen g;

		g = nullgen;
		return g;
	}
}

static Gen
parse_spr(void)
{
	long t;
	vlong c;
	Gen g;

	t = yypeek(0);
	if(t == LSPREG){
		yyget();
		g = nullgen;
		g.type = D_SPR;
		g.offset = yylval.lval;
		return g;
	}
	yyget();
	if(t != LSPR)
		yysyntax_error();
	yyexpect('(');
	c = parse_con();
	yyexpect(')');
	g = nullgen;
	g.type = t;
	g.offset = c;
	return g;
}

static Gen
parse_freg(void)
{
	long t;
	vlong c;
	Gen g;

	t = yypeek(0);
	if(t == LFREG){
		yyget();
		g = nullgen;
		g.type = D_FREG;
		g.reg = yylval.lval;
		return g;
	}
	yyget();
	if(t != LF)
		yysyntax_error();
	yyexpect('(');
	c = parse_con();
	yyexpect(')');
	g = nullgen;
	g.type = D_FREG;
	g.reg = c;
	return g;
}

static Gen
parse_frcon(void)
{
	if(yypeek(0) == LFREG || yypeek(0) == LF)
		return parse_freg();
	return parse_fcon();
}

static Gen
parse_vreg(void)
{
	long t;
	vlong c;
	Gen g;

	t = yypeek(0);
	if(t == LVREG){
		yyget();
		g = nullgen;
		g.type = D_VREG;
		g.reg = yylval.lval;
		return g;
	}
	yyget();
	if(t != LV)
		yysyntax_error();
	yyexpect('(');
	c = parse_con();
	yyexpect(')');
	g = nullgen;
	g.type = D_VREG;
	g.reg = c;
	return g;
}

static vlong
parse_vreglist(void)
{
	Gen a, b;
	vlong v, w;
	int i;

	a = parse_vreg();
	if(yypeek(0) == '-'){
		yyget();
		b = parse_vreg();
		v = 0;
		for(i = a.reg; i <= b.reg; i++)
			v |= 1<<i;
		for(i = b.reg; i <= a.reg; i++)
			v |= 1<<i;
		return v;
	}
	if(yypeek(0) == ','){
		parse_comma();
		w = parse_vreglist();
		return (1<<a.reg) | w;
	}
	return 1 << a.reg;
}

static vlong
parse_vset(void)
{
	vlong v;

	yyexpect('{');
	v = parse_vreglist();
	yyexpect('}');
	return v;
}

static Gen
parse_vlane(void)
{
	vlong v, off;
	Gen g;

	if(yypeek(0) == LVREG || yypeek(0) == LV){
		v = parse_vreg().reg;
		yyexpect('[');
		off = parse_con();
		yyexpect(']');
		g = nullgen;
		g.type = D_VLANE;
		g.reg = v;
		g.offset = off;
		return g;
	}
	v = parse_vset();
	yyexpect('[');
	off = parse_con();
	yyexpect(']');
	g = nullgen;
	g.type = D_VLANE;
	g.offset = off;
	g.reg = v;
	return g;
}

static Gen
parse_vgen(void)
{
	long t;
	vlong v;
	Gen g;

	t = yypeek(0);
	if(t == LVREG || t == LV){
		/* vreg vs vlane (vreg '[' ...)? Check for '[' after vreg. */
		/* vreg is LVREG or LV '(' con ')'. vlane adds '[' con ']'.
		   Need to detect '[' after vreg without consuming. Scan past vreg. */
		int idx;

		if(t == LVREG)
			idx = 1;
		else{
			int depth, j;

			depth = 0;
			idx = 0;
			for(j = 0; j < 200; j++){
				t = yypeek(j);
				if(t == ';' || t == 0 || t == EOF)
					break;
				if(t == '(')
					depth++;
				if(t == ')'){
					depth--;
					if(depth == 0){
						idx = j+1;
						break;
					}
				}
			}
			if(idx == 0)
				yysyntax_error();
		}
		if(yypeek(idx) == '[')
			return parse_vlane();
		return parse_vreg();
	}
	if(t == '{')
		return parse_vlane();
	/* oreg starts LNAME/con/'(' */
	return parse_oreg();
}

static Gen
parse_name(void)
{
	Sym *s;
	vlong c, off, ptr;
	long t;
	Gen g;

	t = yypeek(0);
	if(t == LNAME && yypeek(1) == '<'){
		yyget();
		s = yylval.sym;
		yyget();
		yyexpect('>');
		off = parse_offset();
		yyexpect('(');
		t = yyget();
		if(t != LSB)
			yysyntax_error();
		yyexpect(')');
		g = nullgen;
		g.type = D_OREG;
		g.name = D_STATIC;
		g.sym = s;
		g.offset = off;
		return g;
	}
	if(t == LNAME){
		yyget();
		s = yylval.sym;
		off = parse_offset();
		yyexpect('(');
		ptr = parse_pointer();
		yyexpect(')');
		g = nullgen;
		g.type = D_OREG;
		g.name = ptr;
		g.sym = s;
		g.offset = off;
		return g;
	}
	c = parse_con();
	yyexpect('(');
	ptr = parse_pointer();
	yyexpect(')');
	g = nullgen;
	g.type = D_OREG;
	g.name = ptr;
	g.sym = S;
	g.offset = c;
	return g;
}

static Gen
parse_oreg(void)
{
	long t;

	t = yypeek(0);
	if(t == LNAME || isconstart(t)){
		/* oreg: name | name '(' sreg ')' | ioreg.
		   ioreg starts '(' sreg ')' etc.
		   name starts con/LNAME...
		   If '(' sreg ')' pattern at start then ioreg, else name (maybe with suffix). */
		if(t == '('){
			/* check if '(' sreg ')'... */
			long inner;

			inner = yypeek(1);
			if(inner == LREG || inner == LR || inner == LSP){
				/* could be ioreg '(' sreg ')' or '(' sreg ')' '!' etc.
				   Parse as ioreg. */
				return parse_ioreg();
			}
			/* '(' con ...? Could be name's con '(' pointer ')' where con is '(' expr ')'?
			   Fall through to name. */
		}
		/* try name with optional '(' sreg ')' suffix */
		{
			Gen n;
			vlong r;

			n = parse_name();
			if(yypeek(0) == '('){
				/* name '(' sreg ')' : need to distinguish from next operand's '('?
				   In oreg context, '(' after name can only be this suffix
				   (oreg followed by ',' ';' etc., not '('). So consume. */
				/* Peek inside: '(' sreg ')'? */
				long a1, a2;

				a1 = yypeek(1);
				if(a1 == LREG || a1 == LR || a1 == LSP){
					yyget();
					r = parse_spreg();
					yyexpect(')');
					n.type = D_OREG;
					n.reg = r;
					return n;
				}
			}
			return n;
		}
	}
	return parse_ioreg();
}

static Gen
parse_ioreg(void)
{
	vlong c, r, x;
	long t;
	Gen g;

	if(isconstart(yypeek(0)) && (yypeek(0) != '(' || (yypeek(1) != LREG && yypeek(1) != LR && yypeek(1) != LSP))){
		c = parse_con();
		yyexpect('(');
		r = parse_spreg();
		yyexpect(')');
		if(yypeek(0) == '!'){
			yyget();
			g = nullgen;
			g.type = D_XPRE;
			g.reg = r;
			g.offset = c;
			return g;
		}
		g = nullgen;
		g.type = D_OREG;
		g.reg = r;
		g.offset = c;
		return g;
	}
	t = yypeek(0);
	if(t != '(')
		yysyntax_error();
	yyget();
	r = parse_spreg();
	t = yypeek(0);
	if(t == ')'){
		yyget();
		/* After '(' sreg ')' check for trailers */
		if(yypeek(0) == '('){
			yyget();
			x = parse_indexreg();
			yyexpect(')');
			g = nullgen;
			g.type = D_ROFF;
			g.reg = r;
			g.xreg = x & 0x1f;
			g.offset = x;
			return g;
		}
		if(yypeek(0) == '['){
			yyget();
			x = parse_indexreg();
			yyexpect(']');
			g = nullgen;
			g.type = D_ROFF;
			g.reg = r;
			g.xreg = x & 0x1f;
			g.offset = x | (1<<16);
			return g;
		}
		/* check for con '!' (XPOST): con starts LCONST/...? */
		if(isconstart(yypeek(0))){
			c = parse_con();
			t = yyget();
			if(t != '!')
				yysyntax_error();
			g = nullgen;
			g.type = D_XPOST;
			g.reg = r;
			g.offset = c;
			return g;
		}
		g = nullgen;
		g.type = D_OREG;
		g.reg = r;
		g.offset = 0;
		return g;
	}
	yysyntax_error();
	g = nullgen;
	return g;
}

static Gen
parse_gen(void)
{
	long t;

	t = yypeek(0);
	if(t == LFCR){
		yyget();
		{
			Gen g;

			g = nullgen;
			g.type = D_SPR;
			g.offset = yylval.lval;
			return g;
		}
	}
	if(t == '$')
		return parse_ximm();
	if(t == LREG || t == LR || t == LSP)
		return parse_reg();
	if(t == LFREG || t == LF)
		return parse_freg();
	if(t == LVREG || t == LV || t == '{')
		return parse_vreg();
	if(t == LSPREG || t == LSPR)
		return parse_spr();
	if(t == LNAME || t == '(' || isconstart(t)){
		/* gen: con (D_OREG) vs oreg. con is LCONST/LVAR/...; oreg is name/ioreg.
		   oreg's name includes con '(' pointer ')' etc.
		   Overlap: con-start could be oreg's name prefix.
		   Distinguish: if after con there is '(' pointer ')' then oreg (name),
		   else con (D_OREG). Need to parse con then check '('.
		   For '(' start, need to distinguish '(' sreg ')' (ioreg/oreg) vs '(' expr ')' (con).
		   If '(' sreg ')' pattern then oreg, else con. */
		if(t == '('){
			long inner;

			inner = yypeek(1);
			if(inner == LREG || inner == LR || inner == LSP)
				return parse_oreg();
		}
		if(t == LNAME)
			return parse_oreg();
		/* con-start: parse con, check for '(' pointer ')'? */
		{
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];
			vlong c;

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			/* try to see if con '(' pointer ')' follows? */
			/* Instead, parse con then peek '(' */
			c = parse_con();
			if(yypeek(0) == '('){
				long pin;

				pin = yypeek(1);
				if(pin == LSB || pin == LSP || pin == LFP || pin == LREG || pin == LR){
					yyhave = savehave;
					for(i = 0; i < savehave; i++){
						yytok[i] = savetok[i];
						yyval[i] = saveval[i];
					}
					return parse_oreg();
				}
			}
			/* it was plain con (D_OREG) */
			{
				Gen g;

				g = nullgen;
				g.type = D_OREG;
				g.offset = c;
				return g;
			}
		}
	}
	yysyntax_error();
	{
		Gen g;

		g = nullgen;
		return g;
	}
}

static Gen
parse_nireg(void)
{
	long t;
	vlong r;
	Gen g;

	t = yypeek(0);
	if(t == '('){
		yyget();
		r = parse_spreg();
		yyexpect(')');
		g = nullgen;
		g.type = D_OREG;
		g.reg = r;
		g.offset = 0;
		return g;
	}
	return parse_name();
}

static void
parse_inst(void)
{
	long op;
	Gen a, b, c, d;
	vlong cval, r;
	long s;

	op = yypeek(0);
	switch(op){
	case LTYPE0:
		yyget();
		op = yylval.lval;
		parse_comma();
		outcode(op, &nullgen, NREG, &nullgen);
		return;
	case LTYPE1:
		yyget();
		op = yylval.lval;
		a = parse_imsr();
		yyexpect(',');
		b = parse_reg();
		if(yypeek(0) != ','){
			outcode(op, &a, NREG, &b);
			return;
		}
		yyget();
		if(yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF){
			outcode(op, &a, b.reg, &nullgen);
			return;
		}
		c = parse_reg();
		outcode(op, &a, b.reg, &c);
		return;
	case LTYPE2:
		yyget();
		op = yylval.lval;
		a = parse_imsr();
		yyexpect(',');
		b = parse_reg();
		outcode(op, &a, NREG, &b);
		return;
	case LTYPE3:
		yyget();
		op = yylval.lval;
		a = parse_gen();
		yyexpect(',');
		b = parse_gen();
		outcode(op, &a, NREG, &b);
		return;
	case LMOVK:
		yyget();
		op = yylval.lval;
		a = parse_imm();
		if(yypeek(0) == '<'){
			yyget();
			yyexpect('<');
			cval = parse_con();
			yyexpect(',');
			b = parse_reg();
			{
				Gen g;

				g = nullgen;
				g.type = D_CONST;
				g.offset = cval;
				outcode4(op, &a, NREG, &g, &b);
				return;
			}
		}
		yyexpect(',');
		b = parse_reg();
		outcode(op, &a, NREG, &b);
		return;
	case LTYPE4:
		yyget();
		op = yylval.lval;
		parse_comma();
		if(yypeek(0) == '('){
			if(yypeek(1) == LREG || yypeek(1) == LR || yypeek(1) == LSP)
				b = parse_nireg();
			else
				b = parse_rel();
		} else if(yypeek(0) == LNAME){
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];
			int isnireg;

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			yyget();
			yylval.sym;
			parse_offset();
			if(yypeek(0) == '(' || yypeek(0) == '<')
				isnireg = 1;
			else
				isnireg = 0;
			yyhave = savehave;
			for(i = 0; i < savehave; i++){
				yytok[i] = savetok[i];
				yyval[i] = saveval[i];
			}
			if(isnireg)
				b = parse_nireg();
			else
				b = parse_rel();
		} else
			b = parse_rel();
		outcode(op, &nullgen, NREG, &b);
		return;
	case LTYPE5:
		yyget();
		op = yylval.lval;
		parse_comma();
		a = parse_rel();
		outcode(op, &nullgen, NREG, &a);
		return;
	case LTYPE6:
		yyget();
		op = yylval.lval;
		parse_comma();
		if(yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF){
			outcode(op, &nullgen, NREG, &nullgen);
			return;
		}
		a = parse_gen();
		outcode(op, &nullgen, NREG, &a);
		return;
	case LTYPE7:
		yyget();
		op = yylval.lval;
		a = parse_imsr();
		yyexpect(',');
		r = parse_spreg();
		parse_comma();
		outcode(op, &a, r, &nullgen);
		return;
	case LTYPE8:
		yyget();
		op = yylval.lval;
		a = parse_reg();
		yyexpect(',');
		b = parse_rel();
		outcode(op, &a, NREG, &b);
		return;
	case LTYPER:
		yyget();
		op = yylval.lval;
		a = parse_cond();
		yyexpect(',');
		b = parse_reg();
		outcode(op, &a, NREG, &b);
		return;
	case LTYPES:
		yyget();
		op = yylval.lval;
		a = parse_cond();
		yyexpect(',');
		b = parse_reg();
		yyexpect(',');
		c = parse_reg();
		if(yypeek(0) == ','){
			yyget();
			d = parse_reg();
			outcode4(op, &a, c.reg, &b, &d);
			return;
		}
		outcode(op, &a, b.reg, &c);
		return;
	case LTYPET:
		yyget();
		op = yylval.lval;
		a = parse_imm();
		yyexpect(',');
		b = parse_reg();
		yyexpect(',');
		c = parse_rel();
		outcode(op, &a, b.reg, &c);
		return;
	case LTYPEU:
		yyget();
		op = yylval.lval;
		a = parse_cond();
		yyexpect(',');
		b = parse_imsr();
		yyexpect(',');
		c = parse_reg();
		yyexpect(',');
		d = parse_imm();
		parse_comma();
		outcode4(op, &a, c.reg, &b, &d);
		return;
	case LTYPEV:
		yyget();
		op = yylval.lval;
		if(yypeek(0) == '$'){
			yyget();
			a = parse_name();
			yyexpect(',');
			b = parse_reg();
			outcode(op, &a, NREG, &b);
			return;
		}
		a = parse_rel();
		yyexpect(',');
		b = parse_reg();
		outcode(op, &a, NREG, &b);
		return;
	case LTYPEY:
		yyget();
		op = yylval.lval;
		a = parse_imm();
		yyexpect(',');
		b = parse_imm();
		yyexpect(',');
		r = parse_spreg();
		yyexpect(',');
		c = parse_reg();
		outcode4(op, &a, r, &b, &c);
		return;
	case LTYPEP:
		yyget();
		op = yylval.lval;
		a = parse_imm();
		yyexpect(',');
		b = parse_reg();
		yyexpect(',');
		r = parse_spreg();
		yyexpect(',');
		c = parse_reg();
		outcode4(op, &a, r, &b, &c);
		return;
	case LTYPEA:
		yyget();
		op = yylval.lval;
		parse_comma();
		if(yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF){
			outcode(op, &nullgen, NREG, &nullgen);
			return;
		}
		a = parse_reg();
		outcode(op, &nullgen, NREG, &a);
		return;
	case LTYPEQ:
		yyget();
		op = yylval.lval;
		if(yypeek(0) == ','){
			yyget();
			parse_comma();
			if(yypeek(0) == LFREG || yypeek(0) == LF){
				a = parse_freg();
				parse_comma();
				outcode(op, &nullgen, NREG, &a);
				return;
			}
			a = parse_reg();
			parse_comma();
			outcode(op, &nullgen, NREG, &a);
			return;
		}
		parse_comma();
		if(yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF){
			outcode(op, &nullgen, NREG, &nullgen);
			return;
		}
		if(yypeek(0) == LFREG || yypeek(0) == LF){
			a = parse_freg();
			parse_comma();
			outcode(op, &a, NREG, &nullgen);
			return;
		}
		a = parse_reg();
		parse_comma();
		outcode(op, &a, NREG, &nullgen);
		return;
	case LTYPEB:
		yyget();
		op = yylval.lval;
		a = parse_name();
		yyexpect(',');
		if(yypeek(0) == '$'){
			b = parse_imm();
			outcode(op, &a, NREG, &b);
			return;
		}
		cval = parse_con();
		yyexpect(',');
		b = parse_imm();
		outcode(op, &a, cval, &b);
		return;
	case LTYPEC:
		yyget();
		op = yylval.lval;
		a = parse_name();
		yyexpect('/');
		cval = parse_con();
		yyexpect(',');
		b = parse_ximm();
		outcode(op, &a, cval, &b);
		return;
	case LTYPED:
		yyget();
		op = yylval.lval;
		a = parse_reg();
		yyexpect(',');
		b = parse_reg();
		outcode(op, &a, NREG, &b);
		return;
	case LTYPEH:
		yyget();
		op = yylval.lval;
		parse_comma();
		a = parse_ximm();
		outcode(op, &nullgen, NREG, &a);
		return;
	case LTYPEI:
		yyget();
		op = yylval.lval;
		a = parse_freg();
		yyexpect(',');
		b = parse_freg();
		outcode(op, &a, NREG, &b);
		return;
	case LTYPEK:
		yyget();
		op = yylval.lval;
		a = parse_frcon();
		yyexpect(',');
		b = parse_freg();
		if(yypeek(0) == ','){
			yyget();
			c = parse_freg();
			outcode(op, &a, b.reg, &c);
			return;
		}
		outcode(op, &a, NREG, &b);
		return;
	case LTYPEL:
		yyget();
		op = yylval.lval;
		a = parse_frcon();
		yyexpect(',');
		b = parse_freg();
		parse_comma();
		outcode(op, &a, b.reg, &nullgen);
		return;
	case LTYPEF:
		yyget();
		op = yylval.lval;
		a = parse_cond();
		yyexpect(',');
		b = parse_freg();
		yyexpect(',');
		c = parse_freg();
		yyexpect(',');
		d = parse_imm();
		parse_comma();
		outcode4(op, &a, c.reg, &b, &d);
		return;
	case LTYPE9:
		yyget();
		op = yylval.lval;
		a = parse_freg();
		yyexpect(',');
		b = parse_freg();
		yyexpect(',');
		c = parse_freg();
		yyexpect(',');
		d = parse_freg();
		parse_comma();
		outcode4(op, &a, b.reg, &c, &d);
		return;
	case LFCSEL:
		yyget();
		op = yylval.lval;
		a = parse_cond();
		yyexpect(',');
		b = parse_freg();
		yyexpect(',');
		c = parse_freg();
		yyexpect(',');
		d = parse_freg();
		outcode4(op, &a, c.reg, &b, &d);
		return;
	case LTYPEW:
		yyget();
		op = yylval.lval;
		a = parse_vgen();
		yyexpect(',');
		b = parse_vgen();
		if(yypeek(0) == ','){
			yyget();
			c = parse_vgen();
			outcode(op, &a, b.reg, &c);
			return;
		}
		outcode(op, &a, NREG, &b);
		return;
	case LTYPEJ:
		yyget();
		op = yylval.lval;
		a = parse_gen();
		yyexpect(',');
		s = parse_sreg();
		yyexpect(',');
		b = parse_gen();
		outcode(op, &a, s, &b);
		return;
	case LTYPEM:
		yyget();
		op = yylval.lval;
		a = parse_reg();
		yyexpect(',');
		b = parse_reg();
		yyexpect(',');
		s = parse_sreg();
		yyexpect(',');
		c = parse_reg();
		outcode4(op, &a, s, &b, &c);
		return;
	case LTYPEN:
		yyget();
		op = yylval.lval;
		if((yypeek(0) == LREG || yypeek(0) == LR || yypeek(0) == LSP) && yypeek(1) != ';' && yypeek(1) != 0 && yypeek(1) != EOF){
			/* could be LTYPEN reg ',' sysarg vs LTYPEN sysarg (imm starting '$')?
			   sysarg starting '$' vs reg starting LREG/LR/LSP: distinct?
			   sysarg imm starts '$', reg starts LREG/LR/LSP. No overlap except
			   sysarg con-tuple starting con (LCONST...), which never starts reg.
			   So reg-start means second form. Check for ',' after reg. */
			{
				int savehave, i;
				long savetok[256];
				YYSTYPE saveval[256];
				Gen rgen;

				savehave = yyhave;
				for(i = 0; i < yyhave; i++){
					savetok[i] = yytok[i];
					saveval[i] = yyval[i];
				}
				rgen = parse_reg();
				if(yypeek(0) == ','){
					yyhave = savehave;
					for(i = 0; i < savehave; i++){
						yytok[i] = savetok[i];
						yyval[i] = saveval[i];
					}
					a = parse_reg();
					yyexpect(',');
					b = parse_sysarg();
					outcode(op, &b, a.reg, &nullgen);
					return;
				}
				yyhave = savehave;
				for(i = 0; i < savehave; i++){
					yytok[i] = savetok[i];
					yyval[i] = saveval[i];
				}
			}
		}
		a = parse_sysarg();
		outcode(op, &a, NREG, &nullgen);
		return;
	case LTYPEO:
		yyget();
		op = yylval.lval;
		a = parse_sysarg();
		yyexpect(',');
		b = parse_reg();
		outcode(op, &a, NREG, &b);
		return;
	case LDMB:
		yyget();
		op = yylval.lval;
		a = parse_imm();
		outcode(op, &a, NREG, &nullgen);
		return;
	case LSTXR:
		yyget();
		op = yylval.lval;
		a = parse_reg();
		yyexpect(',');
		b = parse_gen();
		yyexpect(',');
		s = parse_sreg();
		outcode(op, &a, s, &b);
		return;
	case LTYPEE:
		yyget();
		op = yylval.lval;
		parse_comma();
		outcode(op, &nullgen, NREG, &nullgen);
		return;
	default:
		/* LTYPEN reg ',' sysarg is headed by reg? No, headed by LTYPEN.
		   Second LTYPEN form: LTYPEN reg ',' sysarg. Need to handle:
		   after LTYPEN, if next is reg ',' then second form. */
		yysyntax_error();
		return;
	}
}

static void
parse_line(void)
{
	long t;
	Sym *s;
	vlong v;

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
	if(t == LNAME && yypeek(1) == '='){
		yyget();
		s = yylval.sym;
		yyget();
		v = parse_expr();
		yyexpect(';');
		s->type = LVAR;
		s->value = v;
		return;
	}
	if(t == LVAR && yypeek(1) == '='){
		yyget();
		s = yylval.sym;
		yyget();
		v = parse_expr();
		yyexpect(';');
		if(s->value != v)
			yyerror("redeclaration of %s", s->name);
		s->value = v;
		return;
	}
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
