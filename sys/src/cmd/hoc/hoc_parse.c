#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include "hoc.h"
#include "hoc_parse.h"
/*
 * Hand-written recursive-descent replacement for hoc.y (LALR).
 * See header comment in previous version for precedence-map.
 * This file reimplements yyparse with 2-token lookahead for asgn detection.
 */
#define code2(c1,c2) code(c1); code(c2)
#define code3(c1,c2,c3) code(c1); code(c2); code(c3)
YYSTYPE yylval;
static Inst* parse_expr(void);
static Inst* parse_or(void);
static Inst* parse_and(void);
static Inst* parse_cmp(void);
static Inst* parse_add(void);
static Inst* parse_mul(void);
static Inst* parse_unary(void);
static Inst* parse_pow(void);
static Inst* parse_primary(void);
static Inst* parse_stmt(void);
static Inst* parse_prlist(void);
static Inst* parse_stmtlist(void);
static Inst* parse_cond(void);
static Inst* parse_begin(void);
static Inst* parse_end(void);
static Formal* parse_formals(void);
static Symbol* parse_procname(void);
static int parse_arglist(void);
static void parse_defn(void);
int yylex(void);
void yyerror(char*);
static int yypeek(void);
static int yypeek2(void);
static int yyget(void);
static int nbuf;
static int buf0, buf1;
static YYSTYPE val0, val1;
static int
yypeek(void)
{
	if(nbuf >= 1)
		return buf0;
	buf0 = yylex();
	val0 = yylval;
	nbuf = 1;
	return buf0;
}
static int
yypeek2(void)
{
	if(nbuf == 2)
		return buf1;
	if(nbuf == 0)
		yypeek();
	buf1 = yylex();
	val1 = yylval;
	nbuf = 2;
	return buf1;
}
static int
yyget(void)
{
	int t;
	if(nbuf > 0){
		t = buf0;
		yylval = val0;
		if(nbuf == 2){
			buf0 = buf1;
			val0 = val1;
			nbuf = 1;
		} else
			nbuf = 0;
		return t;
	}
	return yylex();
}
static Inst*
parse_begin(void)
{
	return progp;
}
static Inst*
parse_end(void)
{
	code(STOP);
	return progp;
}
static Inst*
parse_cond(void)
{
	Inst *e;
	e = parse_expr();
	code(STOP);
	return e;
}
static Formal*
parse_formals(void)
{
	Symbol *s;
	Formal *rest;
	if(yypeek() != VAR)
		return 0;
	yyget();
	s = yylval.sym;
	if(yypeek() == ','){
		yyget();
		rest = parse_formals();
		return formallist(s, rest);
	}
	return formallist(s, 0);
}
static Symbol*
parse_procname(void)
{
	int t;
	t = yyget();
	if(t != VAR && t != FUNCTION && t != PROCEDURE)
		yyerror("syntax error");
	return yylval.sym;
}
static int
parse_arglist(void)
{
	int n;
	if(yypeek() == ')')
		return 0;
	parse_expr();
	n = 1;
	while(yypeek() == ','){
		yyget();
		parse_expr();
		n++;
	}
	return n;
}
static Inst*
parse_primary(void)
{
	int t;
	Symbol *s;
	Inst *e, *b;
	int n;
	t = yyget();
	switch(t){
	case NUMBER:
		s = yylval.sym;
		e = code(constpush);
		code((Inst)s);
		return e;
	case VAR:
		s = yylval.sym;
		if(yypeek() == INC){
			yyget();
			e = code(postinc);
			code((Inst)s);
			return e;
		}
		if(yypeek() == DEC){
			yyget();
			e = code(postdec);
			code((Inst)s);
			return e;
		}
		e = code(varpush);
		code((Inst)s);
		code(eval);
		return e;
	case FUNCTION:
		s = yylval.sym;
		b = parse_begin();
		if(yyget() != '(')
			yyerror("syntax error");
		n = parse_arglist();
		if(yyget() != ')')
			yyerror("syntax error");
		e = b;
		code(call);
		code((Inst)s);
		code((Inst)n);
		return e;
	case READ:
		if(yyget() != '(')
			yyerror("syntax error");
		if(yyget() != VAR)
			yyerror("syntax error");
		s = yylval.sym;
		if(yyget() != ')')
			yyerror("syntax error");
		e = code(varread);
		code((Inst)s);
		return e;
	case BLTIN:
		s = yylval.sym;
		if(yyget() != '(')
			yyerror("syntax error");
		e = parse_expr();
		if(yyget() != ')')
			yyerror("syntax error");
		code(bltin);
		code((Inst)s->u.ptr);
		return e;
	case '(':
		e = parse_expr();
		if(yyget() != ')')
			yyerror("syntax error");
		return e;
	default:
		yyerror("syntax error");
		return 0;
	}
}
static Inst*
parse_unary(void)
{
	int t;
	Symbol *s;
	Inst *e;
	t = yypeek();
	if(t == '-'){
		yyget();
		e = parse_unary();
		code(negate);
		return e;
	}
	if(t == NOT){
		yyget();
		e = parse_unary();
		code(not);
		return e;
	}
	if(t == INC){
		yyget();
		if(yyget() != VAR)
			yyerror("syntax error");
		s = yylval.sym;
		e = code(preinc);
		code((Inst)s);
		return e;
	}
	if(t == DEC){
		yyget();
		if(yyget() != VAR)
			yyerror("syntax error");
		s = yylval.sym;
		e = code(predec);
		code((Inst)s);
		return e;
	}
	return parse_pow();
}
static Inst*
parse_pow(void)
{
	Inst *e;
	e = parse_primary();
	if(yypeek() == '^'){
		yyget();
		parse_unary();
		code(power);
	}
	return e;
}
static Inst*
parse_mul(void)
{
	int t;
	Inst *e;
	e = parse_unary();
	for(;;){
		t = yypeek();
		if(t != '*' && t != '/' && t != '%')
			break;
		yyget();
		parse_unary();
		if(t == '*')
			code(mul);
		else if(t == '/')
			code(div);
		else
			code(mod);
	}
	return e;
}
static Inst*
parse_add(void)
{
	int t;
	Inst *e;
	e = parse_mul();
	for(;;){
		t = yypeek();
		if(t != '+' && t != '-')
			break;
		yyget();
		parse_mul();
		if(t == '+')
			code(add);
		else
			code(sub);
	}
	return e;
}
static Inst*
parse_cmp(void)
{
	int t;
	Inst *e;
	e = parse_add();
	for(;;){
		t = yypeek();
		if(t != GT && t != GE && t != LT && t != LE && t != EQ && t != NE)
			break;
		yyget();
		parse_add();
		if(t == GT)
			code(gt);
		else if(t == GE)
			code(ge);
		else if(t == LT)
			code(lt);
		else if(t == LE)
			code(le);
		else if(t == EQ)
			code(eq);
		else
			code(ne);
	}
	return e;
}
static Inst*
parse_and(void)
{
	Inst *e;
	e = parse_cmp();
	while(yypeek() == AND){
		yyget();
		parse_cmp();
		code(and);
	}
	return e;
}
static Inst*
parse_or(void)
{
	Inst *e;
	e = parse_and();
	while(yypeek() == OR){
		yyget();
		parse_and();
		code(or);
	}
	return e;
}
static Inst*
parse_expr(void)
{
	Symbol *s;
	int t;
	Inst *e;
	if(yypeek() == VAR){
		yypeek2();
		if(buf1 == '=' || buf1 == ADDEQ || buf1 == SUBEQ || buf1 == MULEQ || buf1 == DIVEQ || buf1 == MODEQ){
			yyget();
			s = yylval.sym;
			t = yyget();
			e = parse_expr();
			if(t == '=')
				code(varpush), code((Inst)s), code(assign);
			else if(t == ADDEQ)
				code(varpush), code((Inst)s), code(addeq);
			else if(t == SUBEQ)
				code(varpush), code((Inst)s), code(subeq);
			else if(t == MULEQ)
				code(varpush), code((Inst)s), code(muleq);
			else if(t == DIVEQ)
				code(varpush), code((Inst)s), code(diveq);
			else
				code(varpush), code((Inst)s), code(modeq);
			return e;
		}
	}
	return parse_or();
}
static Inst*
parse_prlist(void)
{
	Inst *first;
	int t;
	t = yypeek();
	if(t == STRING){
		yyget();
		first = code(prstr);
		code((Inst)yylval.sym);
	} else {
		first = parse_expr();
		code(prexpr);
	}
	while(yypeek() == ','){
		yyget();
		t = yypeek();
		if(t == STRING){
			yyget();
			code(prstr);
			code((Inst)yylval.sym);
		} else {
			parse_expr();
			code(prexpr);
		}
	}
	return first;
}
static Inst*
parse_stmtlist(void)
{
	Inst *e;
	e = progp;
	while(yypeek() == '\n')
		yyget();
	while(yypeek() != '}' && yypeek() != 0){
		parse_stmt();
		while(yypeek() == '\n')
			yyget();
	}
	return e;
}
static Inst*
parse_stmt(void)
{
	int t;
	Inst *e, *w, *body, *end, *body2, *end2;
	Symbol *s;
	int n;
	t = yypeek();
	if(t == RETURN){
		yyget();
		t = yypeek();
		if(t == NUMBER || t == VAR || t == FUNCTION || t == READ || t == BLTIN || t == '(' || t == '-' || t == NOT || t == INC || t == DEC){
			defnonly("return");
			e = parse_expr();
			code(funcret);
			return e;
		}
		defnonly("return");
		code(procret);
		return progp;
	}
	if(t == PRINT){
		yyget();
		e = parse_prlist();
		return e;
	}
	if(t == WHILE){
		yyget();
		w = code(whilecode);
		code(STOP);
		code(STOP);
		if(yyget() != '(')
			yyerror("syntax error");
		parse_cond();
		if(yyget() != ')')
			yyerror("syntax error");
		body = parse_stmt();
		end = parse_end();
		(w)[1] = (Inst)body;
		(w)[2] = (Inst)end;
		return w;
	}
	if(t == FOR){
		Inst *c1, *c2, *c3;
		yyget();
		w = code(forcode);
		code(STOP);
		code(STOP);
		code(STOP);
		code(STOP);
		if(yyget() != '(')
			yyerror("syntax error");
		c1 = parse_cond();
		(void)c1;
		if(yyget() != ';')
			yyerror("syntax error");
		c2 = parse_cond();
		if(yyget() != ';')
			yyerror("syntax error");
		c3 = parse_cond();
		if(yyget() != ')')
			yyerror("syntax error");
		body = parse_stmt();
		end = parse_end();
		(w)[1] = (Inst)c2;
		(w)[2] = (Inst)c3;
		(w)[3] = (Inst)body;
		(w)[4] = (Inst)end;
		return w;
	}
	if(t == IF){
		yyget();
		w = code(ifcode);
		code(STOP);
		code(STOP);
		code(STOP);
		if(yyget() != '(')
			yyerror("syntax error");
		parse_cond();
		if(yyget() != ')')
			yyerror("syntax error");
		body = parse_stmt();
		end = parse_end();
		if(yypeek() == ELSE){
			yyget();
			body2 = parse_stmt();
			end2 = parse_end();
			(w)[1] = (Inst)body;
			(w)[2] = (Inst)body2;
			(w)[3] = (Inst)end2;
			return w;
		}
		(w)[1] = (Inst)body;
		(w)[3] = (Inst)end;
		return w;
	}
	if(t == '{'){
		yyget();
		e = parse_stmtlist();
		if(yyget() != '}')
			yyerror("syntax error");
		return e;
	}
	if(t == PROCEDURE){
		Inst *beg;
		yyget();
		s = yylval.sym;
		beg = parse_begin();
		if(yyget() != '(')
			yyerror("syntax error");
		n = parse_arglist();
		if(yyget() != ')')
			yyerror("syntax error");
		e = beg;
		code(call);
		code((Inst)s);
		code((Inst)n);
		return e;
	}
	e = parse_expr();
	code(xpop);
	return e;
}
static void
parse_defn(void)
{
	int t;
	Symbol *name;
	Formal *f;
	t = yyget();
	if(t != FUNC && t != PROC)
		yyerror("syntax error");
	if(t == FUNC){
		name = parse_procname();
		name->type = FUNCTION;
		indef = 1;
		if(yyget() != '(')
			yyerror("syntax error");
		f = parse_formals();
		if(yyget() != ')')
			yyerror("syntax error");
		parse_stmt();
		code(procret);
		define(name, f);
		indef = 0;
	} else {
		name = parse_procname();
		name->type = PROCEDURE;
		indef = 1;
		if(yyget() != '(')
			yyerror("syntax error");
		f = parse_formals();
		if(yyget() != ')')
			yyerror("syntax error");
		parse_stmt();
		code(procret);
		define(name, f);
		indef = 0;
	}
}
int
yyparse(void)
{
	int t;
	nbuf = 0;
	for(;;){
		t = yypeek();
		if(t == 0)
			return 0;
		if(t == '\n'){
			yyget();
			continue;
		}
		if(t == FUNC || t == PROC){
			parse_defn();
			if(yypeek() == '\n')
				yyget();
			else if(yypeek() != 0)
				yyerror("syntax error");
			continue;
		}
		if(t == VAR && (yypeek2() == '=' || yypeek2() == ADDEQ || yypeek2() == SUBEQ || yypeek2() == MULEQ || yypeek2() == DIVEQ || yypeek2() == MODEQ)){
			parse_expr();
			if(yypeek() == '\n')
				yyget();
			else
				yyerror("syntax error");
			code(xpop);
			code(STOP);
			return 1;
		}
		if(t == RETURN || t == PRINT || t == WHILE || t == FOR || t == IF || t == '{' || t == PROCEDURE){
			parse_stmt();
			if(yypeek() == '\n')
				yyget();
			else if(yypeek() != 0)
				yyerror("syntax error");
			code(STOP);
			return 1;
		}
		parse_expr();
		if(yypeek() == '\n')
			yyget();
		else
			yyerror("syntax error");
		code(printtop);
		code(STOP);
		return 1;
	}
}

/* lexer and helpers copied verbatim from hoc.y tail */
char	*progname;
int	lineno = 1;
jmp_buf	begin;
int	indef;
char	*infile;
Biobuf	*bin;
Biobuf	binbuf;
char	**gargv;
int	gargc;

int c = '\n';

int	backslash(int), follow(int, int, int);
void	defnonly(char*), run(void);
void	warning(char*, char*);

yylex(void)
{
	while ((c=Bgetc(bin)) == ' ' || c == '\t')
		;
	if (c < 0)
		return 0;
	if (c == '\\') {
		c = Bgetc(bin);
		if (c == '\n') {
			lineno++;
			return yylex();
		}
	}
	if (c == '#') {
		while ((c=Bgetc(bin)) != '\n' && c >= 0)
			;
		if (c == '\n')
			lineno++;
		return c;
	}
	if (c == '.' || isdigit(c)) {
		double d;
		Bungetc(bin);
		Bgetd(bin, &d);
		yylval.sym = install("", NUMBER, d);
		return NUMBER;
	}
	if (isalpha(c) || c == '_' || c >= 0x80) {
		Symbol *s;
		char sbuf[100], *p = sbuf;
		do {
			if (p >= sbuf + sizeof(sbuf) - 1) {
				*p = '\0';
				execerror("name too long", sbuf);
			}
			*p++ = c;
		} while ((c=Bgetc(bin)) >= 0 && (isalnum(c) || c == '_' || c >= 0x80));
		Bungetc(bin);
		*p = '\0';
		if ((s=lookup(sbuf)) == 0)
			s = install(sbuf, UNDEF, 0.0);
		yylval.sym = s;
		return s->type == UNDEF ? VAR : s->type;
	}
	if (c == '"') {
		char sbuf[100], *p;
		for (p = sbuf; (c=Bgetc(bin)) != '"'; p++) {
			if (c == '\n' || c == Beof)
				execerror("missing quote", "");
			if (p >= sbuf + sizeof(sbuf) - 1) {
				*p = '\0';
				execerror("string too long", sbuf);
			}
			*p = backslash(c);
		}
		*p = 0;
		yylval.sym = (Symbol *)emalloc(strlen(sbuf)+1);
		strcpy((char*)yylval.sym, sbuf);
		return STRING;
	}
	switch (c) {
	case '+':	return follow('+', INC, follow('=', ADDEQ, '+'));
	case '-':	return follow('-', DEC, follow('=', SUBEQ, '-'));
	case '*':	return follow('=', MULEQ, '*');
	case '/':	return follow('=', DIVEQ, '/');
	case '%':	return follow('=', MODEQ, '%');
	case '>':	return follow('=', GE, GT);
	case '<':	return follow('=', LE, LT);
	case '=':	return follow('=', EQ, '=');
	case '!':	return follow('=', NE, NOT);
	case '|':	return follow('|', OR, '|');
	case '&':	return follow('&', AND, '&');
	case '\n':	lineno++; return '\n';
	default:	return c;
	}
}

backslash(int c)
{
	static char transtab[] = "b\bf\fn\nr\rt\t";
	if (c != '\\')
		return c;
	c = Bgetc(bin);
	if (islower(c) && strchr(transtab, c))
		return strchr(transtab, c)[1];
	return c;
}

follow(int expect, int ifyes, int ifno)
{
	int c = Bgetc(bin);

	if (c == expect)
		return ifyes;
	Bungetc(bin);
	return ifno;
}

void
yyerror(char* s)
{
	execerror(s, (char *)0);
}

void
execerror(char* s, char* t)
{
	warning(s, t);
	Bseek(bin, 0L, 2);
	restoreall();
	longjmp(begin, 0);
}

void
fpecatch(void)
{
	execerror("floating point exception", (char *) 0);
}

void
intcatch(void)
{
	execerror("interrupt", 0);
}

void
run(void)
{
	setjmp(begin);
	for (initcode(); yyparse(); initcode())
		execute(progbase);
}

void
main(int argc, char* argv[])
{
	static int first = 1;
#ifdef YYDEBUG
	extern int yydebug;
	yydebug=3;
#endif
	progname = argv[0];
	init();
	if (argc == 1) {
		static char *stdinonly[] = { "-" };

		gargv = stdinonly;
		gargc = 1;
	} else if (first) {
		first = 0;
		gargv = argv+1;
		gargc = argc-1;
	}
	Binit(&binbuf, 0, OREAD);
	bin = &binbuf;
	while (moreinput())
		run();
	exits(0);
}

moreinput(void)
{
	char *expr;
	static char buf[64];
	int fd;
	static Biobuf b;

	if (gargc-- <= 0)
		return 0;
	if (bin && bin != &binbuf)
		Bterm(bin);
	infile = *gargv++;
	lineno = 1;
	if (strcmp(infile, "-") == 0) {
		bin = &binbuf;
		infile = 0;
		return 1;
	}
	if(strncmp(infile, "-e", 2) == 0) {
		if(infile[2]==0){
			if(gargc == 0){
				fprint(2, "%s: no argument for -e\n", progname);
				return 0;
			}
			gargc--;
			expr = *gargv++;
		}else
			expr = infile+2;
		sprint(buf, "/tmp/hocXXXXXXX");
		infile = mktemp(buf);
		fd = create(infile, ORDWR|ORCLOSE, 0600);
		if(fd < 0){
			fprint(2, "%s: can't create temp. file: %r\n", progname);
			return 0;
		}
		fprint(fd, "%s\n", expr);
		bin = &b;
		seek(fd, 0, 0);
		Binit(bin, fd, OREAD);
	} else {
		bin=Bopen(infile, OREAD);
		if (bin == 0) {
			fprint(2, "%s: can't open %s\n", progname, infile);
			return moreinput();
		}
	}
	return 1;
}

void
warning(char* s, char* t)
{
	fprint(2, "%s: %s", progname, s);
	if (t)
		fprint(2, " %s", t);
	if (infile)
		fprint(2, " in %s", infile);
	fprint(2, " near line %d\n", lineno);
	while (c != '\n' && c != Beof)
		if((c = Bgetc(bin)) == '\n')
			lineno++;
}

void
defnonly(char *s)
{
	if (!indef)
		execerror(s, "used outside definition");
}
