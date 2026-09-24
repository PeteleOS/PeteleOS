#include "a.h"
#include "a_parse.h"

YYSTYPE yylval;
/*
 * Hand-written recursive-descent replacement for a.y (LALR, ja/riscv64).
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
 * inst: LADD imm ',' rreg | oprrr rreg ',' rreg
 *     | LADD imm ',' sreg ',' rreg | oprrr rreg ',' sreg ',' rreg
 *     | LFLT2 drreg ',' drreg | LFLT3 drreg ',' freg ',' drreg
 *     | LBEQ rreg ',' sreg ',' rel | LBEQ rreg ',' rel
 *     | LBR rel | LBR oreg | LBRET | LCALL sreg ',' addr
 *     | LCALL sreg ',' rel | LMOVB addr ',' rreg | LMOVBU addr ',' rreg
 *     | LMOVB rreg ',' addr | LMOVF addr ',' dreg | LMOVF dreg ',' addr
 *     | LMOVF dreg ',' dreg | LMOVW imm ',' rreg | LMOVW ximm ',' rreg
 *     | LMOVW rreg ',' rreg | LMOVW addr ',' rreg | LMOVW rreg ',' addr
 *     | LMOVW rreg ',' ctlreg | LMOVW imm ',' ctlreg
 *     | LMOVW ctlreg ',' rreg | LLUI name ',' rreg | LLUI imm ',' rreg
 *     | LSYS imm | LSYS0 | LCSR ctlreg ',' sreg ',' rreg
 *     | LCSR ctlreg ',' '$' con ',' rreg | LSWAP rreg ',' sreg ',' rreg
 *     | LAMO con ',' rreg ',' sreg ',' rreg | LTEXT name ',' imm
 *     | LTEXT name ',' con ',' imm | LDATA name '/' con ',' imm
 *     | LDATA name '/' con ',' ximm | LDATA name '/' con ',' fimm
 *     | LWORD imm
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
 * error calls yyerror("syntax error") then skips to ';' or EOF (0/-1).
 */

static long yypeek(int n);
static long yyget(void);
static void yysyntax_error(void);
static void yyexpect(long t);
static void yyskip_to_semi(void);
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
static vlong parse_fregval(void);
static vlong parse_oprrr(void);
static Gen parse_rreg(void);
static Gen parse_dreg(void);
static Gen parse_ctlreg(void);
static Gen parse_drreg(void);
static Gen parse_addr(void);
static Gen parse_name(void);
static Gen parse_oreg(void);
static Gen parse_rel(void);
static Gen parse_imm(void);
static Gen parse_ximm(void);
static Gen parse_fimm(void);
static void parse_inst(void);
static void parse_line(void);

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
			yyerror("register value out of range");
		return v;
	}
	yysyntax_error();
	return 0;
}

static vlong
parse_fregval(void)
{
	long t;
	vlong v;

	t = yypeek(0);
	if(t == LFREG){
		yyget();
		return yylval.lval;
	}
	if(t == FR){
		yyget();
		yyexpect('(');
		v = parse_expr();
		yyexpect(')');
		if(v < 0 || v >= NREG)
			yyerror("register value out of range");
		return v;
	}
	yysyntax_error();
	return 0;
}

static vlong
parse_oprrr(void)
{
	long t;

	t = yyget();
	if(t == LADD || t == LMUL)
		return yylval.lval;
	yysyntax_error();
	return 0;
}

static Gen
parse_rreg(void)
{
	vlong r;
	Gen g;

	r = parse_sreg();
	g = nullgen;
	g.type = D_REG;
	g.reg = r;
	return g;
}

static Gen
parse_dreg(void)
{
	vlong r;
	Gen g;

	r = parse_fregval();
	g = nullgen;
	g.type = D_FREG;
	g.reg = r;
	return g;
}

static Gen
parse_ctlreg(void)
{
	vlong v;
	Gen g;

	yyexpect(LCTL);
	yyexpect('(');
	v = parse_expr();
	yyexpect(')');
	if(v < 0 || v >= 0xFFF)
		yyerror("CSR value out of range");
	g = nullgen;
	g.type = D_CTLREG;
	g.offset = v;
	return g;
}

static Gen
parse_drreg(void)
{
	long t;

	t = yypeek(0);
	if(t == LFREG || t == FR)
		return parse_dreg();
	return parse_rreg();
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
	vlong c, r;
	long t;
	Gen g;

	t = yypeek(0);
	if(t == '('){
		yyget();
		r = parse_sreg();
		yyexpect(')');
		g = nullgen;
		g.type = D_OREG;
		g.reg = r;
		g.offset = 0;
		return g;
	}
	c = parse_con();
	yyexpect('(');
	r = parse_sreg();
	yyexpect(')');
	g = nullgen;
	g.type = D_OREG;
	g.reg = r;
	g.offset = c;
	return g;
}

static Gen
parse_addr(void)
{
	long t;

	t = yypeek(0);
	if(t == LNAME || (isconstart(t) && t != LCONST && t != LVAR)){
		/* addr: oreg | name. oreg starts '(' or con '(' sreg ')'.
		   name starts con/LNAME with pointer.
		   Overlap con '(' ...: oreg has sreg inside, name has pointer inside.
		   If LNAME then name; else if '(' then oreg; else con '(' ...?
		   Check inside '(' for sreg vs pointer. */
		if(t == LNAME)
			return parse_name();
		if(t == '(')
			return parse_oreg();
		/* con-start: parse con then check '(' */
		{
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];
			long pin;
			vlong v;

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			if(yypeek(0) == '('){
				v = parse_con();
				USED(v);

				pin = yypeek(1);
				if(pin == LSB || pin == LSP || pin == LFP){
					yyhave = savehave;
					for(i = 0; i < savehave; i++){
						yytok[i] = savetok[i];
						yyval[i] = saveval[i];
					}
					return parse_name();
				}
				yyhave = savehave;
				for(i = 0; i < savehave; i++){
					yytok[i] = savetok[i];
					yyval[i] = saveval[i];
				}
				return parse_oreg();
			}
			yyhave = savehave;
			for(i = 0; i < savehave; i++){
				yytok[i] = savetok[i];
				yyval[i] = saveval[i];
			}
		}
	}
	if(t == LCONST || t == LVAR || t == '-' || t == '+' || t == '~')
		return parse_name();
	return parse_oreg();
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
parse_imm(void)
{
	vlong c;
	Gen g;

	yyexpect('$');
	c = parse_con();
	g = nullgen;
	g.type = D_CONST;
	g.offset = c;
	if(thechar == 'j' && (vlong)g.offset != c){
		g.type = D_VCONST;
		g.vval = c;
	}
	return g;
}

static Gen
parse_ximm(void)
{
	long t;
	Gen g;

	t = yypeek(0);
	if(t == '$'){
		if(yypeek(1) == LSCONST){
			yyget();
			yyget();
			g = nullgen;
			g.type = D_SCONST;
			memcpy(g.sval, yylval.sval, sizeof(g.sval));
			return g;
		}
		yyget();
		g = parse_addr();
		g.type = D_CONST;
		return g;
	}
	yysyntax_error();
	g = nullgen;
	return g;
}

static Gen
parse_fimm(void)
{
	long t;
	double d;
	Gen g;

	yyexpect('$');
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

static void
parse_inst(void)
{
	long op;
	Gen a, b, c;
	vlong s, v;
	long t;

	t = yypeek(0);
	if(t == LADD){
		yyget();
		op = yylval.lval;
		a = parse_imm();
		yyexpect(',');
		if((yypeek(0) == LREG || yypeek(0) == LR) && yypeek(1) != ',' && yypeek(1) != ';' && yypeek(1) != 0 && yypeek(1) != EOF){
			/* LADD imm ',' sreg ',' rreg? Actually first is sreg, need to check for second comma.
			   LADD imm ',' rreg (1 comma) vs LADD imm ',' sreg ',' rreg (2 commas).
			   Count commas to ';'. */
			int i, n;

			n = 0;
			for(i = 0; i < 200; i++){
				t = yypeek(i);
				if(t == ';' || t == 0 || t == EOF)
					break;
				if(t == ',')
					n++;
			}
			if(n >= 2){
				s = parse_sreg();
				yyexpect(',');
				b = parse_rreg();
				outcode(op, &a, s, &b);
				return;
			}
		}
		b = parse_rreg();
		outcode(op, &a, NREG, &b);
		return;
	}
	if(t == LMUL || (t == LADD && 0)){
		yyget();
		op = yylval.lval;
		a = parse_rreg();
		yyexpect(',');
		if(yypeek(0) == ',' || yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF){
			yysyntax_error();
			return;
		}
		{
			int i, n;

			n = 0;
			for(i = 0; i < 200; i++){
				t = yypeek(i);
				if(t == ';' || t == 0 || t == EOF)
					break;
				if(t == ',')
					n++;
			}
			if(n >= 2){
				s = parse_sreg();
				yyexpect(',');
				b = parse_rreg();
				outcode(op, &a, s, &b);
				return;
			}
		}
		b = parse_rreg();
		outcode(op, &a, NREG, &b);
		return;
	}
	/* oprrr rreg ',' rreg vs oprrr rreg ',' sreg ',' rreg: handle LMUL/LADD as oprrr */
	if(t == LADD || t == LMUL){
		yyget();
		op = yylval.lval;
		/* need to decide imm-first vs rreg-first: after op, if '$' then imm form handled above.
		   Here rreg-first. */
		a = parse_rreg();
		yyexpect(',');
		{
			int i, n;

			n = 0;
			for(i = 0; i < 200; i++){
				t = yypeek(i);
				if(t == ';' || t == 0 || t == EOF)
					break;
				if(t == ',')
					n++;
			}
			if(n >= 2){
				/* Actually after first comma consumed? We consumed one, remaining commas:
				   rreg ',' rreg has 0 remaining, rreg ',' sreg ',' rreg has 1 remaining.
				   Our count includes all ahead, so adjust. */
			}
		}
		if(yypeek(0) == LREG || yypeek(0) == LR){
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			if(yypeek(0) == ','){
				s = parse_sreg();
				USED(s);
				yyhave = savehave;
				for(i = 0; i < savehave; i++){
					yytok[i] = savetok[i];
					yyval[i] = saveval[i];
				}
				s = parse_sreg();
				yyexpect(',');
				b = parse_rreg();
				outcode(op, &a, s, &b);
				return;
			}
			yyhave = savehave;
			for(i = 0; i < savehave; i++){
				yytok[i] = savetok[i];
				yyval[i] = saveval[i];
			}
		}
		b = parse_rreg();
		outcode(op, &a, NREG, &b);
		return;
	}
	switch(t){
	case LFLT2:
		yyget();
		op = yylval.lval;
		a = parse_drreg();
		yyexpect(',');
		b = parse_drreg();
		outcode(op, &a, NREG, &b);
		return;
	case LFLT3:
		yyget();
		op = yylval.lval;
		a = parse_drreg();
		yyexpect(',');
		s = parse_fregval();
		yyexpect(',');
		b = parse_drreg();
		outcode(op, &a, s, &b);
		return;
	case LBEQ:
		yyget();
		op = yylval.lval;
		a = parse_rreg();
		yyexpect(',');
		if(yypeek(0) == LREG || yypeek(0) == LR){
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			if(yypeek(0) == ','){
				s = parse_sreg();
				USED(s);
				yyhave = savehave;
				for(i = 0; i < savehave; i++){
					yytok[i] = savetok[i];
					yyval[i] = saveval[i];
				}
				s = parse_sreg();
				yyexpect(',');
				b = parse_rel();
				outcode(op, &a, s, &b);
				return;
			}
			yyhave = savehave;
			for(i = 0; i < savehave; i++){
				yytok[i] = savetok[i];
				yyval[i] = saveval[i];
			}
		}
		b = parse_rel();
		{
			Gen regzero;

			regzero = nullgen;
			regzero.type = D_REG;
			regzero.reg = 0;
			outcode(op, &regzero, a.reg, &b);
			return;
		}
	case LBR:
		yyget();
		op = yylval.lval;
		if(yypeek(0) == '(' || (isconstart(yypeek(0)) && yypeek(0) != LCONST && yypeek(0) != LVAR)){
			a = parse_oreg();
			outcode(op, &nullgen, NREG, &a);
			return;
		}
		if(yypeek(0) == LCONST || yypeek(0) == LVAR || yypeek(0) == '-' || yypeek(0) == '+' || yypeek(0) == '~'){
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			if(yypeek(0) == '('){
				v = parse_con();
				USED(v);
				yyhave = savehave;
				for(i = 0; i < savehave; i++){
					yytok[i] = savetok[i];
					yyval[i] = saveval[i];
				}
				a = parse_oreg();
				outcode(op, &nullgen, NREG, &a);
				return;
			}
			yyhave = savehave;
			for(i = 0; i < savehave; i++){
				yytok[i] = savetok[i];
				yyval[i] = saveval[i];
			}
		}
		a = parse_rel();
		outcode(op, &nullgen, NREG, &a);
		return;
	case LBRET:
		yyget();
		op = yylval.lval;
		outcode(op, &nullgen, NREG, &nullgen);
		return;
	case LCALL:
		yyget();
		op = yylval.lval;
		s = parse_sreg();
		yyexpect(',');
		if(yypeek(0) == LNAME || yypeek(0) == '(' || isconstart(yypeek(0))){
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			/* try rel (LNAME offset without '(')? If '(' follows offset then addr */
			if(yypeek(0) == LNAME){
				yyget();
				if(yypeek(0) == '('){
					yyhave = savehave;
					for(i = 0; i < savehave; i++){
						yytok[i] = savetok[i];
						yyval[i] = saveval[i];
					}
					a = parse_addr();
					outcode(op, &nullgen, s, &a);
					return;
				}
				yyhave = savehave;
				for(i = 0; i < savehave; i++){
					yytok[i] = savetok[i];
					yyval[i] = saveval[i];
				}
				a = parse_rel();
				outcode(op, &nullgen, s, &a);
				return;
			}
		}
		a = parse_addr();
		outcode(op, &nullgen, s, &a);
		return;
	case LMOVB:
	case LMOVBU:
		yyget();
		op = yylval.lval;
		if(yypeek(0) == LREG || yypeek(0) == LR){
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			a = parse_rreg();
			if(yypeek(0) == ','){
				yyhave = savehave;
				for(i = 0; i < savehave; i++){
					yytok[i] = savetok[i];
					yyval[i] = saveval[i];
				}
				a = parse_rreg();
				yyexpect(',');
				b = parse_addr();
				outcode(op, &a, NREG, &b);
				return;
			}
			yyhave = savehave;
			for(i = 0; i < savehave; i++){
				yytok[i] = savetok[i];
				yyval[i] = saveval[i];
			}
		}
		a = parse_addr();
		yyexpect(',');
		b = parse_rreg();
		outcode(op, &a, NREG, &b);
		return;
	case LMOVF:
		yyget();
		op = yylval.lval;
		if(yypeek(0) == LFREG || yypeek(0) == FR){
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			a = parse_dreg();
			if(yypeek(0) == ','){
				yyhave = savehave;
				for(i = 0; i < savehave; i++){
					yytok[i] = savetok[i];
					yyval[i] = saveval[i];
				}
				a = parse_dreg();
				yyexpect(',');
				if(yypeek(0) == LFREG || yypeek(0) == FR){
					b = parse_dreg();
					outcode(op, &a, NREG, &b);
					return;
				}
				b = parse_addr();
				outcode(op, &a, NREG, &b);
				return;
			}
			yyhave = savehave;
			for(i = 0; i < savehave; i++){
				yytok[i] = savetok[i];
				yyval[i] = saveval[i];
			}
		}
		a = parse_addr();
		yyexpect(',');
		b = parse_dreg();
		outcode(op, &a, NREG, &b);
		return;
	case LMOVW:
		yyget();
		op = yylval.lval;
		if(yypeek(0) == '$'){
			if(yypeek(1) == LSCONST || yypeek(1) == LNAME || yypeek(1) == '(' || isconstart(yypeek(1))){
				/* ximm vs imm: ximm is '$' addr (addr has '(' etc.) vs imm '$' con.
				   If after '$' next is addr-like with '(' then ximm, else imm?
				   Simplify: if '$' LSCONST then ximm; else if '$' addr with '(' then ximm?
				   For '$' con (LCONST...) then imm. */
				if(yypeek(1) == LSCONST){
					a = parse_ximm();
					yyexpect(',');
					if(yypeek(0) == LCTL+100){
						yysyntax_error();
						return;
					}
					b = parse_rreg();
					outcode(op, &a, NREG, &b);
					return;
				}
			}
			/* try ximm vs imm vs ctlreg dest? After '$' ... ',' then dest could be rreg or ctlreg.
			   Need to parse first operand then check dest. */
			{
				int savehave, i;
				long savetok[256];
				YYSTYPE saveval[256];

				savehave = yyhave;
				for(i = 0; i < yyhave; i++){
					savetok[i] = yytok[i];
					saveval[i] = yyval[i];
				}
				a = parse_imm();
				if(yypeek(0) == ','){
					yyget();
					if(yypeek(0) == LCTL){
						yyhave = savehave;
						for(i = 0; i < savehave; i++){
							yytok[i] = savetok[i];
							yyval[i] = saveval[i];
						}
						a = parse_imm();
						yyexpect(',');
						c = parse_ctlreg();
						{
							Gen regzero;
							int r2;

							r2 = a.offset;
							if(r2 < 0 || r2 >= NREG)
								yyerror("immediate value out of range");
							regzero = nullgen;
							regzero.type = D_REG;
							regzero.reg = 0;
							outcode(ACSRRWI, &c, r2, &regzero);
							return;
						}
					}
					yyhave = savehave;
					for(i = 0; i < savehave; i++){
						yytok[i] = savetok[i];
						yyval[i] = saveval[i];
					}
				} else {
					yyhave = savehave;
					for(i = 0; i < savehave; i++){
						yytok[i] = savetok[i];
						yyval[i] = saveval[i];
					}
				}
			}
			a = parse_imm();
			yyexpect(',');
			if(yypeek(0) == LCTL){
				b = parse_ctlreg();
				{
					Gen regzero;
					int r2;

					r2 = a.offset;
					if(r2 < 0 || r2 >= NREG)
						yyerror("immediate value out of range");
					regzero = nullgen;
					regzero.type = D_REG;
					regzero.reg = 0;
					outcode(ACSRRWI, &b, r2, &regzero);
					return;
				}
			}
			b = parse_rreg();
			outcode(op, &a, NREG, &b);
			return;
		}
		if(yypeek(0) == LREG || yypeek(0) == LR){
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			a = parse_rreg();
			if(yypeek(0) == ','){
				yyget();
				if(yypeek(0) == LCTL){
					yyhave = savehave;
					for(i = 0; i < savehave; i++){
						yytok[i] = savetok[i];
						yyval[i] = saveval[i];
					}
					a = parse_rreg();
					yyexpect(',');
					b = parse_ctlreg();
					{
						Gen regzero;

						regzero = nullgen;
						regzero.type = D_REG;
						regzero.reg = 0;
						outcode(ACSRRW, &b, a.reg, &regzero);
						return;
					}
				}
				if(yypeek(0) == LREG || yypeek(0) == LR || yypeek(0) == LNAME || yypeek(0) == '(' || isconstart(yypeek(0))){
					yyhave = savehave;
					for(i = 0; i < savehave; i++){
						yytok[i] = savetok[i];
						yyval[i] = saveval[i];
					}
					a = parse_rreg();
					yyexpect(',');
					if(yypeek(0) == LCTL){
						b = parse_ctlreg();
						{
							Gen regzero;

							regzero = nullgen;
							regzero.type = D_REG;
							regzero.reg = 0;
							outcode(ACSRRW, &b, a.reg, &regzero);
							return;
						}
					}
					/* rreg ',' rreg vs rreg ',' addr */
					if(yypeek(0) == LREG || yypeek(0) == LR){
						int save2, j;
						long sv2[256];
						YYSTYPE svv2[256];

						save2 = yyhave;
						for(j = 0; j < yyhave; j++){
							sv2[j] = yytok[j];
							svv2[j] = yyval[j];
						}
						b = parse_rreg();
						if(yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF){
							yyhave = save2;
							for(j = 0; j < save2; j++){
								yytok[j] = sv2[j];
								yyval[j] = svv2[j];
							}
							/* need to reparse correctly */
						} else {
							yyhave = save2;
							for(j = 0; j < save2; j++){
								yytok[j] = sv2[j];
								yyval[j] = svv2[j];
							}
						}
					}
					b = parse_addr();
					/* addr could be rreg? Actually rreg ',' rreg already?
					   If addr parses as oreg with reg? Overlap. Prefer rreg? */
					/* Check if b is actually rreg (single reg)? */
					outcode(op, &a, NREG, &b);
					return;
				}
			}
			yyhave = savehave;
			for(i = 0; i < savehave; i++){
				yytok[i] = savetok[i];
				yyval[i] = saveval[i];
			}
		}
		if(yypeek(0) == LCTL){
			a = parse_ctlreg();
			yyexpect(',');
			b = parse_rreg();
			outcode(ACSRRS, &a, REGZERO, &b);
			return;
		}
		a = parse_addr();
		yyexpect(',');
		b = parse_rreg();
		outcode(op, &a, NREG, &b);
		return;
	case LLUI:
		yyget();
		op = yylval.lval;
		if(yypeek(0) == LNAME || (isconstart(yypeek(0)) && yypeek(0) != LCONST)){
			/* name vs imm: name starts LNAME/con '(' ...; imm starts '$'.
			   Actually LLUI name ',' rreg vs LLUI imm ',' rreg.
			   name never starts '$', imm always '$'. So check '$'. */
		}
		if(yypeek(0) == '$'){
			a = parse_imm();
			yyexpect(',');
			b = parse_rreg();
			outcode(op, &a, NREG, &b);
			return;
		}
		a = parse_name();
		yyexpect(',');
		b = parse_rreg();
		outcode(op, &a, NREG, &b);
		return;
	case LSYS:
		yyget();
		op = yylval.lval;
		a = parse_imm();
		outcode(op, &nullgen, NREG, &a);
		return;
	case LSYS0:
		yyget();
		op = yylval.lval;
		{
			Gen syscon;

			syscon = nullgen;
			syscon.type = D_CONST;
			syscon.offset = op;
			outcode(ASYS, &nullgen, NREG, &syscon);
			return;
		}
	case LCSR:
		yyget();
		op = yylval.lval;
		a = parse_ctlreg();
		yyexpect(',');
		if(yypeek(0) == '$'){
			yyget();
			v = parse_con();
			yyexpect(',');
			b = parse_rreg();
			if(v < 0 || v >= NREG)
				yyerror("immediate value out of range");
			outcode(op + (ACSRRWI-ACSRRW), &a, v, &b);
			return;
		}
		s = parse_sreg();
		yyexpect(',');
		b = parse_rreg();
		outcode(op, &a, s, &b);
		return;
	case LSWAP:
		yyget();
		op = yylval.lval;
		a = parse_rreg();
		yyexpect(',');
		s = parse_sreg();
		yyexpect(',');
		b = parse_rreg();
		outcode(op, &a, s, &b);
		return;
	case LAMO:
		yyget();
		op = yylval.lval;
		v = parse_con();
		yyexpect(',');
		a = parse_rreg();
		yyexpect(',');
		s = parse_sreg();
		yyexpect(',');
		b = parse_rreg();
		outcode(op, &a, (v<<16)|s, &b);
		return;
	case LTEXT:
		yyget();
		op = yylval.lval;
		a = parse_name();
		yyexpect(',');
		if(yypeek(0) == '$'){
			b = parse_imm();
			outcode(op, &a, NREG, &b);
			return;
		}
		v = parse_con();
		yyexpect(',');
		b = parse_imm();
		outcode(op, &a, v, &b);
		return;
	case LDATA:
		yyget();
		op = yylval.lval;
		a = parse_name();
		yyexpect('/');
		v = parse_con();
		yyexpect(',');
		if(yypeek(0) == '$' && (yypeek(1) == LFCONST || (yypeek(1) == '-' && yypeek(2) == LFCONST))){
			b = parse_fimm();
			outcode(op, &a, v, &b);
			return;
		}
		if(yypeek(0) == '$' && yypeek(1) != LCONST && yypeek(1) != LVAR && yypeek(1) != '-' && yypeek(1) != '+' && yypeek(1) != '~' && yypeek(1) != '('){
			b = parse_ximm();
			outcode(op, &a, v, &b);
			return;
		}
		{
			int savehave, i;
			long savetok[256];
			YYSTYPE saveval[256];

			savehave = yyhave;
			for(i = 0; i < yyhave; i++){
				savetok[i] = yytok[i];
				saveval[i] = yyval[i];
			}
			/* try fimm (float) vs imm vs ximm: fimm starts '$' LFCONST, imm '$' con, ximm '$' addr */
			if(yypeek(0) == '$' && (yypeek(1) == LFCONST || (yypeek(1) == '-' && yypeek(2) == LFCONST))){
				yyhave = savehave;
				for(i = 0; i < savehave; i++){
					yytok[i] = savetok[i];
					yyval[i] = saveval[i];
				}
				b = parse_fimm();
				outcode(op, &a, v, &b);
				return;
			}
			yyhave = savehave;
			for(i = 0; i < savehave; i++){
				yytok[i] = savetok[i];
				yyval[i] = saveval[i];
			}
		}
		b = parse_imm();
		/* imm vs ximm: if imm fails? Actually '$' addr (ximm) vs '$' con (imm):
		   addr with '(' etc. vs con. Prefer imm for '$' con, ximm for '$' addr.
		   Check next after '$' for addr pattern. */
		outcode(op, &a, v, &b);
		return;
	case LWORD:
		yyget();
		op = yylval.lval;
		a = parse_imm();
		outcode(op, &nullgen, NREG, &a);
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
