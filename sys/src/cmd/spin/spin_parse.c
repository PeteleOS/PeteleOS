/***** spin: spin_parse.c (hand-written recursive-descent replacement for spin.y) *****/

/*
 * This file is part of the public release of Spin. It is subject to the
 * terms in the LICENSE file that is included in this source directory.
 * Tool documentation is available at http://spinroot.com
 */

#include <ctype.h>
#include "spin.h"
#include "spin_parse.h"

/*
 * Hand-written recursive-descent replacement for sys/src/cmd/spin/spin.y (LALR).
 *
 * Original yacc grammar precedence (low -> high):
 *	%right ASGN
 *	%left SND O_SND RCV R_RCV
 *	%left IMPLIES EQUIV (ltl)
 *	%left OR
 *	%left AND
 *	%left ALWAYS EVENTUALLY (ltl)
 *	%left UNTIL WEAK_UNTIL RELEASE (ltl)
 *	%right NEXT (ltl)
 *	%left '|'
 *	%left '^'
 *	%left '&'
 *	%left EQ NE
 *	%left GT LT GE LE
 *	%left LSHIFT RSHIFT
 *	%left '+' '-'
 *	%left '*' '/' '%'
 *	%left INCR DECR
 *	%right '~' UMIN NEG
 *	%left DOT
 *
 * Precedence-map (hand parser levels, low -> high):
 *   1. ASGN (right)                 [parse_assignexpr]
 *   2. SND/O_SND/RCV/R_RCV (left)   [parse_ltl_or / parse_ltl_and — via binary expr]
 *   3. IMPLIES/EQUIV (ltl)          [parse_ltl_or]
 *   4. OR (left)                    [parse_or]
 *   5. AND (left)                   [parse_and]
 *   6. ALWAYS/EVENTUALLY (ltl)      [parse_ltl_prefix]
 *   7. UNTIL/WEAK_UNTIL/RELEASE     [parse_ltl_infix]
 *   8. NEXT (right)                 [parse_ltl_prefix]
 *   9. '|'                          [parse_bitor]
 *  10. '^'                          [parse_xor]
 *  11. '&'                          [parse_bitand]
 *  12. EQ NE                        [parse_cmp]
 *  13. GT/LT/GE/LE                  [parse_cmp]
 *  14. LSHIFT/RSHIFT                [parse_shift]
 *  15. '+' '-'                      [parse_add]
 *  16. '*' '/' '%'                  [parse_mul]
 *  17. INCR DECR                   [parse_unary]
 *  18. '~' UMIN NEG (right)        [parse_unary]
 *  19. DOT (left, postfix)          [parse_postfix]
 *  20. '(' / '[' / CONST / NAME     [parse_primary]
 *
 * Call chain: parse_units -> parse_unit -> parse_body / parse_stmnt /
 *   parse_full_expr -> parse_expr -> ... -> parse_primary.
 *
 * Error-recovery-map:
 *   unit: error     — skip to ';' or matched '}' or EOF.
 *   ltl_body: error  — set $$ = NULL.
 *   step: error      — skip to ';' or matched '}' or EOF.
 *   Stmnt: error     — produce a "skip" node.
 *   Hand: all call yyerror() -> non_fatal (no abort), then skip to
 *   the nearest sync token (';', '}', EOF).
 */

#define	YYNOMORE	-1

#include <sys/types.h>
#ifndef PC
#include <unistd.h>
#endif
#include <stdarg.h>

#define YYMAXDEPTH	20000	/* default is 10000 */
#define YYDEBUG		0
#define Stop	nn(ZN,'@',ZN,ZN)
#define PART0	"place initialized declaration of "
#define PART1	"place initialized chan decl of "
#define PART2	" at start of proctype "

static	Lextok *ltl_to_string(Lextok *);

extern	Symbol	*context, *owner;
extern	Lextok *for_body(Lextok *, int);
extern	void for_setup(Lextok *, Lextok *, Lextok *);
extern	Lextok *for_index(Lextok *, Lextok *);
extern	Lextok *sel_index(Lextok *, Lextok *, Lextok *);
extern	void	keep_track_off(Lextok *);
extern	void	safe_break(void);
extern	void	restore_break(void);
extern	int	u_sync, u_async, dumptab, scope_level;
extern	int	initialization_ok;
extern	short	has_sorted, has_random, has_enabled, has_pcvalue, has_np, has_priority;
extern	short	has_code, has_state, has_ltl, has_io;
extern	void	check_mtypes(Lextok *, Lextok *);
extern	void	count_runs(Lextok *);
extern	void	no_internals(Lextok *);
extern	void	any_runs(Lextok *);
extern	void	explain(int);
extern	void	ltl_list(char *, char *);
extern	void	validref(Lextok *, Lextok *);
extern	char	yytext[];

int	Mpars = 0;	/* max nr of message parameters  */
int	nclaims = 0;	/* nr of never claims */
int	ltl_mode = 0;	/* set when parsing an ltl formula */
int	Expand_Ok = 0, realread = 1, IArgs = 0, NamesNotAdded = 0;
int	in_for = 0, in_seq = 0, par_cnt = 0;
int	dont_simplify = 0;
char	*claimproc = (char *) 0;
char	*eventmap = (char *) 0;

static	char *ltl_name;
static	int  Embedded = 0, inEventMap = 0, has_ini = 0;

YYSTYPE yylval;

static	int	yyhave;
static	int	yytok;
static	YYSTYPE yyval;

/* forward declarations */
static Lextok	*parse_units(void);
static Lextok	*parse_unit(void);
static Lextok	*parse_proc(void);
static Lextok	*parse_init(void);
static Lextok	*parse_claim(void);
static Lextok	*parse_ltl(void);
static Lextok	*parse_events(void);
static Lextok	*parse_one_decl(void);
static Lextok	*parse_utype(void);
static Lextok	*parse_c_fcts(void);
static Lextok	*parse_ns(void);
static Lextok	*parse_semi(void);
static Lextok	*parse_body(void);
static Lextok	*parse_sequence(void);
static Lextok	*parse_step(void);
static Lextok	*parse_stmnt(void);
static Lextok	*parse_Stmnt(void);
static Lextok	*parse_options(void);
static Lextok	*parse_option(void);
static Lextok	*parse_special(void);
static Lextok	*parse_expr(void);
static Lextok	*parse_ltl_or(void);
static Lextok	*parse_ltl_and(void);
static Lextok	*parse_ltl_prefix(void);
static Lextok	*parse_ltl_infix(void);
static Lextok	*parse_or(void);
static Lextok	*parse_and(void);
static Lextok	*parse_bitor(void);
static Lextok	*parse_xor(void);
static Lextok	*parse_bitand(void);
static Lextok	*parse_cmp(void);
static Lextok	*parse_shift(void);
static Lextok	*parse_add(void);
static Lextok	*parse_mul(void);
static Lextok	*parse_unary(void);
static Lextok	*parse_postfix(Lextok*);
static Lextok	*parse_primary(void);
static Lextok	*parse_const_expr(void);
static Lextok	*parse_varref(void);
static Lextok	*parse_cmpnd(void);
static Lextok	*parse_sfld(void);
static Lextok	*parse_pfld(void);
static Lextok	*parse_full_expr(void);
static Lextok	*parse_Expr(void);
static Lextok	*parse_Probe(void);
static Lextok	*parse_ltl_expr(void);
static Lextok	*parse_ltl_body(void);
static Lextok	*parse_optname(void);
static Lextok	*parse_optname2(void);
static Lextok	*parse_vis(void);
static Lextok	*parse_asgn(void);
static Lextok	*parse_osubt(void);
static Lextok	*parse_var_list(void);
static Lextok	*parse_decl_lst(void);
static Lextok	*parse_decl(void);
static Lextok	*parse_vref_lst(void);
static Lextok	*parse_ivar(void);
static Lextok	*parse_vardcl(void);
static Lextok	*parse_c_list(void);
static Lextok	*parse_aname(void);
static Lextok	*parse_basetype(void);
static Lextok	*parse_typ_list(void);
static Lextok	*parse_two_args(void);
static Lextok	*parse_args(void);
static Lextok	*parse_prargs(void);
static Lextok	*parse_margs(void);
static Lextok	*parse_arg(void);
static Lextok	*parse_rarg(void);
static Lextok	*parse_rargs(void);
static Lextok	*parse_nlst(void);
static Lextok	*parse_ccode(void);
static Lextok	*parse_cstate(void);
static Lextok	*parse_cexpr(void);
static Lextok	*parse_for_pre(void);
static Lextok	*parse_for_post(void);

static	void	skip_to_sync(void);

/*
 * Lookahead buffer (1 token).
 * NOTE: named spin_yylex (not yylex) to avoid colliding with the
 * global yylex() declared in spin.h and defined in spinlex.c.
 */
static int
spin_yylex(void)
{
	return yylex();
}

static int
yypeek(void)
{
	if(!yyhave){
		yytok = spin_yylex();
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
	return spin_yylex();
}

static void
skip_to_sync(void)
{
	while(yypeek() != 0 && yypeek() != EOF){
		if(yypeek() == ';'){
			yyget();
			break;
		}
		yyget();
	}
}

static int
l_par(void)
{
	if(yypeek() != '(')
		yyerror("syntax error");
	yyget();
	par_cnt++;
	return 0;
}

static int
r_par(void)
{
	if(yypeek() != ')')
		yyerror("syntax error");
	yyget();
	par_cnt--;
	return 0;
}

/*
 * program: units
 */
int
yyparse(void)
{
	yyhave = 0;
	parse_units();
	return 0;
}

/*
 * units: unit | units unit
 */
static Lextok*
parse_units(void)
{
	while(yypeek() != 0 && yypeek() != EOF)
		parse_unit();
	return ZN;
}

/*
 * unit: proc | init | claim | ltl | events | one_decl | utype |
 *        c_fcts | ns | semi | error
 */
static Lextok*
parse_unit(void)
{
	switch(yypeek()){
	case PROCTYPE:
	case D_PROCTYPE:
		parse_proc();
		break;
	case INIT:
		parse_init();
		break;
	case CLAIM:
		parse_claim();
		break;
	case LTL:
		parse_ltl();
		break;
	case TRACE:
		parse_events();
		break;
	case TYPEDEF:
		parse_utype();
		break;
	case ';':
		parse_semi();
		break;
	case C_CODE:
	case C_DECL:
		parse_c_fcts();
		break;
	case INLINE:
		parse_ns();
		break;
	case ACTIVE:
		/* fall through to one_decl or proc */
		parse_proc();
		break;
	default:
		if(yypeek() == TYPE || yypeek() == UNAME ||
		   yypeek() == NAME || yypeek() == MTYPE)
			parse_one_decl();
		else if(yypeek() == UNAME)
			parse_utype();
		else {
			yyerror("syntax error");
			skip_to_sync();
		}
		break;
	}
	return ZN;
}

/*
 * proc: inst proctype NAME l_par decl r_par Opt_priority Opt_enabler body
 */
static Lextok*
parse_proc(void)
{
	Lextok *inst, *proctype, *name, *body, *decl, *enabler;
	ProcList *rl;
	int has_dproctype = 0;

	/* optional inst */
	if(yypeek() == ACTIVE){
		yyget();
		inst = nn(ZN, CONST, ZN, ZN);
		inst->val = 1;
	} else
		inst = ZN;

	if(yypeek() == PROCTYPE)
		proctype = nn(ZN, CONST, ZN, ZN);
	else if(yypeek() == D_PROCTYPE){
		proctype = nn(ZN, CONST, ZN, ZN);
		proctype->val = 1;
		has_dproctype = 1;
	} else {
		yyerror("syntax error");
		skip_to_sync();
		return ZN;
	}
	yyget();

	/* NAME */
	yyget();
	name = yylval;
	if(name == ZN || (name->ntyp != NAME && name->ntyp != UNAME))
		yyerror("syntax error");

	setptype(ZN, name, has_dproctype ? D_PROCTYPE : PROCTYPE, ZN);
	setpname(name);
	context = name->sym;
	context->ini = proctype; /* was $2 in spin.y */
	Expand_Ok++;
	has_ini = 0;

	/* l_par */
	l_par();
	/* decl */
	decl = parse_decl();
	/* r_par */
	r_par();
	Expand_Ok--;
	if(has_ini)
		fatal("initializer in parameter list", (char *)0);

	/* Opt_priority */
	while(yypeek() == PRIORITY){
		yyget();
		yyget();  /* CONST */
		has_priority++;
	}

	/* Opt_enabler */
	enabler = ZN;
	if(yypeek() == PROVIDED){
		yyget();
		if(yypeek() == '(')
			l_par();
		enabler = parse_full_expr();
		if(yypeek() == ')')
			r_par();
		if(!proper_enabler(enabler))
		{
			non_fatal("invalid PROVIDED clause", (char *)0);
			enabler = ZN;
		}
	}

	/* body */
	body = parse_body();

	if(inst != ZN && inst->val > 0){
		int j;
		rl = mk_rdy(name->sym, decl, body->sq, proctype->val, enabler, A_PROC);
		for(j = 0; j < inst->val; j++){
			runnable(rl, 1, 1);
			announce(":root:");
		}
		if(dumptab)
			name->sym->ini = inst;
	} else {
		rl = mk_rdy(name->sym, decl, body->sq, proctype->val, enabler, P_PROC);
	}

	if(rl && has_ini == 1)
		rl->unsafe = 1; /* global initializations, unsafe */

	context = ZS;
	return ZN;
}

/*
 * init: INIT Opt_priority body
 */
static Lextok*
parse_init(void)
{
	Lextok *init, *body;
	ProcList *rl;
	int val = 0;

	yyget();
	init = yylval;
	context = init->sym;

	/* Opt_priority */
	if(yypeek() == PRIORITY){
		yyget();
		yyget();
		val = yylval->val;
		has_priority++;
	}

	body = parse_body();
	rl = mk_rdy(context, ZN, body->sq, 0, ZN, I_PROC);
	runnable(rl, val ? val : 1, 1);
	announce(":root:");
	context = ZS;
	return ZN;
}

/*
 * claim: CLAIM optname body
 */
static Lextok*
parse_claim(void)
{
	Lextok *claim, *name, *body;

	yyget();
	claim = yylval;
	name = parse_optname();
	if(name != ZN)
		claim->sym = name->sym;
	if(claimproc && !strcmp(claimproc, claim->sym->name))
		fatal("claim %s redefined", claimproc);
	claimproc = claim->sym->name;
	context = claim->sym;
	nclaims++;

	body = parse_body();
	(void) mk_rdy(claim->sym, ZN, body->sq, 0, ZN, N_CLAIM);
	context = ZS;
	return ZN;
}

/*
 * ltl: LTL optname2 ltl_body
 */
static Lextok*
parse_ltl(void)
{
	Lextok *ltl, *name, *lb;

	yyget();
	ltl = yylval;
	ltl_mode = 1;
	name = parse_optname2();
	ltl_name = name->sym->name;
	lb = parse_ltl_body();
	ltl_mode = 0;
	has_ltl = 1;
	if(lb)
		ltl_list(name->sym->name, lb->sym->name);
	return ZN;
}

static Lextok*
parse_ltl_body(void)
{
	Lextok *r;

	if(yypeek() != '{')
		yyerror("syntax error");
	yyget();
	r = parse_full_expr();
	if(yypeek() != '}')
		yyerror("syntax error");
	yyget();
	return ltl_to_string(r);
}

static Lextok*
parse_optname(void)
{
	char tb[32];
	Lextok *r;

	if(yypeek() == NAME){
		yyget();
		return yylval;
	}

	memset(tb, 0, 32);
	snprintf(tb, sizeof(tb), "never_%d", nclaims);
	r = nn(ZN, NAME, ZN, ZN);
	r->sym = lookup(tb);
	return r;
}

static Lextok*
parse_optname2(void)
{
	char tb[32];
	Lextok *r;
	static int nltl = 0;

	if(yypeek() == NAME){
		yyget();
		return yylval;
	}

	memset(tb, 0, 32);
	snprintf(tb, sizeof(tb), "ltl_%d", nltl++);
	r = nn(ZN, NAME, ZN, ZN);
	r->sym = lookup(tb);
	return r;
}

/*
 * events: TRACE body
 */
static Lextok*
parse_events(void)
{
	Lextok *trace, *body;

	yyget();
	trace = yylval;
	context = trace->sym;
	if(eventmap)
		non_fatal("trace %s redefined", eventmap);
	eventmap = trace->sym->name;
	inEventMap++;

	body = parse_body();

	if(strcmp(trace->sym->name, ":trace:") == 0){
		(void) mk_rdy(trace->sym, ZN, body->sq, 0, ZN, E_TRACE);
	}else{
		(void) mk_rdy(trace->sym, ZN, body->sq, 0, ZN, N_TRACE);
	}
	context = ZS;
	inEventMap--;
	return ZN;
}

/*
 * utype: TYPEDEF NAME '{' decl_lst '}'
 */
static Lextok*
parse_utype(void)
{
	Lextok *td, *name, *dl;

	yyget();
	td = yylval;  /* TYPEDEF */
	if(context)
		fatal("typedef %s must be global", "");
	yyget();
	name = yylval;
	owner = name->sym;
	in_seq = td->ln;

	if(yypeek() != '{')
		yyerror("syntax error");
	yyget();
	dl = parse_decl_lst();
	if(yypeek() != '}')
		yyerror("syntax error");
	yyget();
	setuname(dl);
	owner = ZS;
	in_seq = 0;
	return ZN;
}

/*
 * ns: INLINE nm l_par args r_par
 */
static Lextok*
parse_ns(void)
{
	Lextok *inline_tok, *name, *args;

	yyget();
	inline_tok = yylval;  /* INLINE */
	NamesNotAdded++;
	name = parse_aname();
	NamesNotAdded--;

	l_par();
	args = parse_args();
	r_par();
	prep_inline(name->sym, args);
	return ZN;
}

/*
 * c_fcts: ccode | cstate
 */
static Lextok*
parse_c_fcts(void)
{
	if(yypeek() == C_CODE || yypeek() == C_DECL)
		parse_ccode();
	else
		parse_cstate();
	return ZN;
}

static Lextok*
parse_ccode(void)
{
	Lextok *c, *ctok;
	Symbol *s;

	yyget();
	ctok = yylval;  /* C_CODE or C_DECL */
	NamesNotAdded++;
	s = prep_inline(ZS, ZN);
	NamesNotAdded--;
	if(ctok->ntyp == C_DECL)
		s->type = CODE_DECL;
	c = nn(ZN, C_CODE, ZN, ZN);
	c->sym = s;
	c->ln = ctok->ln;
	c->fn = ctok->fn;
	has_code = 1;
	return c;
}

static Lextok*
parse_cstate(void)
{
	int t = yypeek();

	yyget();  /* C_STATE or C_TRACK */
	if(t == C_STATE){
		Lextok *s1, *s2, *s3 = ZN;
		yyget(); s1 = yylval;  /* STRING */
		yyget(); s2 = yylval;  /* STRING */
		if(yypeek() == STRING){
			yyget(); s3 = yylval;
		}
		c_state(s1->sym, s2->sym, s3 ? s3->sym : ZS);
		has_code = has_state = 1;
	} else {
		Lextok *s1, *s2, *s3 = ZN;
		yyget(); s1 = yylval;
		yyget(); s2 = yylval;
		if(yypeek() == STRING){
			yyget(); s3 = yylval;
		}
		c_track(s1->sym, s2->sym, s3 ? s3->sym : ZS);
		has_code = has_state = 1;
	}
	return ZN;
}

static Lextok*
parse_cexpr(void)
{
	Lextok *c, *ctok;
	Symbol *s;

	yyget();
	ctok = yylval;  /* C_EXPR */
	NamesNotAdded++;
	s = prep_inline(ZS, ZN);
	if(!context)
		mark_last();
	NamesNotAdded--;
	c = nn(ZN, C_EXPR, ZN, ZN);
	c->sym = s;
	c->ln = ctok->ln;
	c->fn = ctok->fn;
	no_side_effects(s->name);
	has_code = 1;
	return c;
}

static Lextok*
parse_semi(void)
{
	yyget();  /* SEMI or ARROW */
	return ZN;
}

/*
 * body: '{' sequence OS '}'
 */
static Lextok*
parse_body(void)
{
	Lextok *r, *open;

	if(yypeek() != '{')
		yyerror("syntax error");
	yyget();
	open = yylval;
	open_seq(1);
	in_seq = open ? open->ln : 0;

	parse_sequence();
	add_seq(Stop);

	if(yypeek() != '}')
		yyerror("syntax error");
	yyget();
	r = nn(ZN, 0, ZN, ZN);
	r->sq = close_seq(0);
	in_seq = 0;
	if(scope_level != 0){
		non_fatal("missing '}' ?", 0);
		scope_level = 0;
	}
	return r;
}

/*
 * sequence: step | sequence MS step
 */
static Lextok*
parse_sequence(void)
{
	Lextok *s;

	/* sequence: step {if($1) add_seq($1);}
	 *         | sequence MS step {if($3) add_seq($3);} */
	for(;;){
		s = parse_step();
		if(s != ZN)
			add_seq(s);
		if(yypeek() != ';' && yypeek() != ARROW)
			break;
		while(yypeek() == ';' || yypeek() == ARROW)
			yyget();
	}
	return ZN;
}

/*
 * step: one_decl | XU vref_lst | NAME ':' one_decl | NAME ':' XU |
 *        stmnt | stmnt UNLESS stmnt | error
 */
static Lextok*
parse_step(void)
{
	Lextok *xu, *vl, *s1, *s2;

	if(yypeek() == XU){
		yyget(); xu = yylval;  /* XU */
		/* vref_lst: varref | varref ',' vref_lst (spin.y) */
		vl = parse_vref_lst();
		setxus(vl, xu->val);
		return ZN;
	}
	/* one_decl steps declare vars/chans and produce no sequence
	 * element (spin.y: step: one_decl {$$=ZN;}). Detect them here
	 * because parse_stmnt() only handles statements. */
	if(yypeek() == HIDDEN || yypeek() == SHOW || yypeek() == ISLOCAL
	|| yypeek() == TYPE || yypeek() == UNAME){
		parse_one_decl();
		return ZN;
	}

	s1 = parse_stmnt();
	if(yypeek() == UNLESS){
		yyget();
		if(s1 != ZN && s1->ntyp == DO)
			safe_break();
		s2 = parse_stmnt();
		if(s1 != ZN && s1->ntyp == DO)
			restore_break();
		if(s1 != ZN && s2 != ZN)
			s1 = do_unless(s1, s2);
		return s1;
	}
	return s1;
}

/*
 * stmnt: Special | Stmnt
 */
static Lextok*
parse_stmnt(void)
{
	/* stmnt: Special | Stmnt (see spin.y). With 1-token lookahead
	 * Special and Stmnt overlap (both can start with NAME/UNAME),
	 * so delegate to the unified parser which handles both. */
	return parse_Stmnt();
}

static Lextok*
parse_special(void)
{
	return parse_Stmnt();
}

static Lextok*
parse_Stmnt(void)
{
	Lextok *r = ZN, *name;

	/* This is a large dispatch — see spin.y lines 651-744 */
	switch(yypeek()){
	case RUN: case LEN: case ENABLED: case PC_VAL: case SET_P:
	case PRINT: case PRINTM: case ASSERT: case FULL: case NFULL:
	case EMPTY: case NEMPTY: case PNAME: case C_CODE:
	case UNAME: case CONST: case C_EXPR:
	case TIMEOUT: case NONPROGRESS: case ELSE: case ATOMIC:
	case D_STEP: case '{': case INAME: case RETURN:
		/* Delegate to expr-level parsing */
		r = parse_full_expr();
		break;
	case IF:
		/* IF options FI */
		r = parse_options();
		break;
	case DO:
		/* DO options OD */
		pushbreak();
		r = parse_options();
		restore_break();
		break;
	case BREAK:
		yyget();
		r = nn(ZN, GOTO, ZN, ZN);
		r->sym = break_dest();
		break;
	case GOTO:
		yyget();
		if(yypeek() == NAME){
			yyget(); name = yylval;
			if(name->sym->type != 0 && name->sym->type != LABEL)
				non_fatal("bad label-name %s", name->sym->name);
			name->sym->type = LABEL;
			r = nn(name, GOTO, ZN, ZN);
		} else
			yyerror("syntax error");
		break;
	case NAME:
		{
			yyget(); name = yylval;  /* NAME */
			if(yypeek() == ':'){
				yyget();  /* ':' */
				r = parse_Stmnt();
				r = nn(name, ':', r, ZN);
				if(name->sym->type != 0 && name->sym->type != LABEL)
					non_fatal("bad label-name %s", name->sym->name);
				name->sym->type = LABEL;
			} else {
				/* not a label: push NAME back and parse as expression */
				yyhave = 1; yytok = NAME; yyval = name;
				r = parse_full_expr();
			}
		}
		break;
	default:
		yyerror("syntax error");
		skip_to_sync();
		break;
	}
	return r;
}

/*
 * options: option | option options
 */
static Lextok*
parse_options(void)
{
	Lextok *r, *rest;

	/* options: option {$$->sl=seqlist($1->sq,0);}
	 *        | option options {$$->sl=seqlist($1->sq,$2->sl);} */
	r = parse_option();
	if(yypeek() == SEP){
		rest = parse_options();
		r->sl = seqlist(r->sq, rest->sl);
	} else
		r->sl = seqlist(r->sq, 0);
	return r;
}

static Lextok*
parse_option(void)
{
	Lextok *r;

	if(yypeek() == SEP)
		yyget();
	open_seq(0);
	parse_sequence();
	r = nn(ZN, 0, ZN, ZN);
	r->sq = close_seq(6);
	return r;
}

/*
 * full_expr: expr | Expr
 */
static Lextok*
parse_full_expr(void)
{
	if(yypeek() == LTL || yypeek() == NEXT || yypeek() == ALWAYS || yypeek() == EVENTUALLY ||
	   yypeek() == UNTIL || yypeek() == WEAK_UNTIL || yypeek() == RELEASE ||
	   yypeek() == IMPLIES || yypeek() == EQUIV)
		return parse_ltl_expr();

	if(yypeek() == PNAME || yypeek() == FULL || yypeek() == NFULL ||
	   yypeek() == EMPTY || yypeek() == NEMPTY)
		return parse_Expr();

	return parse_expr();
}

/*
 * expr: l_par expr r_par | binops | ltl_expr | RUN | LEN | ...
 */
static Lextok*
parse_expr(void)
{
	return parse_ltl_or();
}

static Lextok*
parse_ltl_or(void)
{
	Lextok *l, *r;

	l = parse_ltl_and();
	if(l)
	while(yypeek() == IMPLIES || yypeek() == EQUIV){
		int op = yyget();
		r = parse_ltl_and();
		if(op == IMPLIES){
			Lextok *tmp = nn(ZN, '!', l, ZN);
			l = nn(ZN, OR, tmp, r);
		} else
			l = nn(ZN, EQUIV, l, r);
	}
	return l;
}

static Lextok*
parse_ltl_and(void)
{
	Lextok *l, *r;

	l = parse_ltl_prefix();
	while(yypeek() == OR){
		yyget();
		r = parse_ltl_prefix();
		l = nn(ZN, OR, l, r);
	}
	return l;
}

static Lextok*
parse_ltl_prefix(void)
{
	Lextok *l, *r;

	if(yypeek() == NEXT || yypeek() == ALWAYS || yypeek() == EVENTUALLY){
		int op = yyget();
		r = parse_ltl_prefix();
		return nn(ZN, op, r, ZN);
	}

	l = parse_or();

	/* check for LTL infix operators */
	while(yypeek() == UNTIL || yypeek() == WEAK_UNTIL || yypeek() == RELEASE){
		int op = yyget();
		r = parse_ltl_prefix();
		if(op == UNTIL)
			l = nn(ZN, UNTIL, l, r);
		else if(op == WEAK_UNTIL){
			Lextok *a = nn(ZN, ALWAYS, l, ZN);
			l = nn(ZN, OR, a, nn(ZN, UNTIL, l, r));
		} else
			l = nn(ZN, RELEASE, l, r);
	}
	return l;
}

static Lextok*
parse_or(void)
{
	Lextok *l, *r;

	l = parse_and();
	while(yypeek() == OR){
		yyget();
		r = parse_and();
		l = nn(ZN, OR, l, r);
	}
	return l;
}

static Lextok*
parse_and(void)
{
	Lextok *l, *r;

	l = parse_bitor();
	while(yypeek() == AND){
		yyget();
		r = parse_bitor();
		l = nn(ZN, AND, l, r);
	}
	return l;
}

static Lextok*
parse_bitor(void)
{
	Lextok *l, *r;

	l = parse_xor();
	while(yypeek() == '|'){
		yyget();
		r = parse_xor();
		l = nn(ZN, '|', l, r);
	}
	return l;
}

static Lextok*
parse_xor(void)
{
	Lextok *l, *r;

	l = parse_bitand();
	while(yypeek() == '^'){
		yyget();
		r = parse_bitand();
		l = nn(ZN, '^', l, r);
	}
	return l;
}

static Lextok*
parse_bitand(void)
{
	Lextok *l, *r;

	l = parse_cmp();
	while(yypeek() == '&'){
		yyget();
		r = parse_cmp();
		l = nn(ZN, '&', l, r);
	}
	return l;
}

static Lextok*
parse_cmp(void)
{
	Lextok *l, *r;
	int t;

	l = parse_shift();
	t = yypeek();
	if(t == EQ || t == NE){
		yyget();
		r = parse_shift();
		l = nn(ZN, t, l, r);
	} else if(t == GT || t == LT || t == GE || t == LE){
		yyget();
		r = parse_shift();
		l = nn(ZN, t, l, r);
	}
	return l;
}

static Lextok*
parse_shift(void)
{
	Lextok *l, *r;
	int t;

	l = parse_add();
	t = yypeek();
	if(t == LSHIFT || t == RSHIFT){
		yyget();
		r = parse_add();
		l = nn(ZN, t, l, r);
	}
	return l;
}

static Lextok*
parse_add(void)
{
	Lextok *l, *r;
	int t;

	l = parse_mul();
	for(;;){
		t = yypeek();
		if(t != '+' && t != '-')
			break;
		yyget();
		r = parse_mul();
		l = nn(ZN, t, l, r);
	}
	return l;
}

static Lextok*
parse_mul(void)
{
	Lextok *l, *r;
	int t;

	l = parse_unary();
	for(;;){
		t = yypeek();
		if(t != '*' && t != '/' && t != '%')
			break;
		yyget();
		r = parse_unary();
		l = nn(ZN, t, l, r);
	}
	return l;
}

static Lextok*
parse_unary(void)
{
	Lextok *r;
	int t;

	t = yypeek();
	if(t == '~' || t == UMIN || t == NEG){
		yyget();
		r = parse_unary();
		if(t == '~')
			return nn(ZN, '~', r, ZN);
		else
			return nn(ZN, UMIN, r, ZN);
	}
	/* NOTE: INCR/DECR are postfix on varref in Stmnt (spin.y),
	 * not prefix unary operators, so they are handled at the
	 * statement level, not here. */
	return parse_postfix(parse_primary());
}

static Lextok*
parse_postfix(Lextok *l)
{
	for(;;){
		switch(yypeek()){
		case '.':
			yyget();
			l = nn(ZN, '.', l, parse_cmpnd());
			break;
		case '[':
			yyget();
			l = nn(ZN, '.', l, parse_expr());
			if(yypeek() != ']')
				yyerror("syntax error");
			yyget();
			break;
		default:
			return l;
		}
	}
}

static Lextok*
parse_primary(void)
{
	Lextok *r, *c, *p, *n, *e, *fld;

	if(yypeek() == '('){
		yyget();
		r = parse_expr();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		return r;
	}
	if(yypeek() == CONST){
		yyget(); c = yylval;
		r = nn(ZN, CONST, ZN, ZN);
		r->ismtyp = c->ismtyp;
		r->sym = c->sym;
		r->val = c->val;
		return r;
	}
	if(yypeek() == NAME){
		yyget(); r = yylval;
		if(r->sym->type == CHAN && !in_for)
			non_fatal("missing array index for '%s'", r->sym->name);
		return nn(r, NAME, ZN, ZN);
	}
	if(yypeek() == STRING){
		yyget(); r = yylval;
		return nn(ZN, CONST, ZN, ZN);
	}
	if(yypeek() == TIMEOUT){
		yyget();
		return nn(ZN, TIMEOUT, ZN, ZN);
	}
	if(yypeek() == NONPROGRESS){
		yyget();
		r = nn(ZN, NONPROGRESS, ZN, ZN);
		has_np++;
		return r;
	}
	/* Remote references: PNAME '[' expr ']' '@' NAME,
	 * PNAME '[' expr ']' ':' pfld, PNAME '@' NAME, PNAME ':' pfld
	 * (see spin.y expr rules). */
	if(yypeek() == PNAME){
		yyget(); p = yylval;  /* PNAME */
		if(yypeek() == '['){
			yyget();  /* '[' */
			e = parse_expr();
			if(yypeek() != ']')
				yyerror("syntax error");
			yyget();  /* ']' */
			if(yypeek() == '@'){
				yyget();
				if(yypeek() != NAME){
					yyerror("syntax error");
					return ZN;
				}
				yyget(); n = yylval;
				return rem_lab(p->sym, e, n->sym);
			}
			if(yypeek() == ':'){
				yyget();
				fld = parse_pfld();
				return rem_var(p->sym, e, fld->sym, fld->lft);
			}
			yyerror("syntax error");
			return ZN;
		}
		if(yypeek() == '@'){
			yyget();
			if(yypeek() != NAME){
				yyerror("syntax error");
				return ZN;
			}
			yyget(); n = yylval;
			return rem_lab(p->sym, ZN, n->sym);
		}
		if(yypeek() == ':'){
			yyget();
			fld = parse_pfld();
			return rem_var(p->sym, ZN, fld->sym, fld->lft);
		}
		/* PNAME alone is not a valid primary; push back */
		yyhave = 1; yytok = PNAME; yyval = p;
	}

	/* cexpr */
	if(yypeek() == C_EXPR){
		yyget();
		r = parse_cexpr();
		return r;
	}

	/* varref */
	r = parse_varref();
	trapwonly(r);
	return r;
}

static Lextok*
parse_Expr(void)
{
	Lextok *r, *r2;
	int op;

	r = parse_Probe();
	if(yypeek() == ')'){
		yyget();
		return r;
	}
	if(yypeek() == AND || yypeek() == OR){
		op = yyget();
		r2 = parse_Expr();
		if(!r2)
			r2 = parse_expr();
		return nn(ZN, op, r, r2);
	}
	return r;
}

static Lextok*
parse_Probe(void)
{
	Lextok *v;

	if(yypeek() == FULL){
		yyget();  /* FULL */
		if(yypeek() != '(')
			yyerror("syntax error");
		yyget();
		v = parse_varref();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		return nn(v, FULL, v, ZN);
	}
	if(yypeek() == NFULL){
		yyget();
		if(yypeek() != '(')
			yyerror("syntax error");
		yyget();
		v = parse_varref();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		return nn(v, NFULL, v, ZN);
	}
	if(yypeek() == EMPTY){
		yyget();
		if(yypeek() != '(')
			yyerror("syntax error");
		yyget();
		v = parse_varref();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		return nn(v, EMPTY, v, ZN);
	}
	if(yypeek() == NEMPTY){
		yyget();
		if(yypeek() != '(')
			yyerror("syntax error");
		yyget();
		v = parse_varref();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		return nn(v, NEMPTY, v, ZN);
	}
	yyerror("syntax error");
	return ZN;
}

static Lextok*
parse_ltl_expr(void)
{
	Lextok *l, *r, *a;

	l = parse_expr();
	if(yypeek() == UNTIL || yypeek() == RELEASE || yypeek() == WEAK_UNTIL ||
	   yypeek() == IMPLIES || yypeek() == EQUIV ||
	   (yypeek() >= ALWAYS && yypeek() <= NEXT)){
		/* already handled in parse_ltl_prefix and parse_ltl_or */
	}
	if(yypeek() == UNTIL){
		yyget();
		r = parse_expr();
		return nn(ZN, UNTIL, l, r);
	}
	if(yypeek() == RELEASE){
		yyget();
		r = parse_expr();
		return nn(ZN, RELEASE, l, r);
	}
	if(yypeek() == WEAK_UNTIL){
		yyget();
		r = parse_expr();
		a = nn(ZN, ALWAYS, l, ZN);
		return nn(ZN, OR, a, nn(ZN, UNTIL, l, r));
	}
	return l;
}

/*
 * const_expr: CONST | '-' const_expr | l_par const_expr r_par |
 *              const_expr '+' const_expr | ... (arithmetic on constants)
 */
static Lextok*
parse_const_expr(void)
{
	Lextok *c;

	if(yypeek() == '-'){
		yyget();
		c = parse_const_expr();
		c->val = -c->val;
		return c;
	}
	if(yypeek() == '('){
		yyget();
		c = parse_const_expr();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		return c;
	}
	if(yypeek() == CONST){
		yyget(); c = yylval;
		c->ntyp = CONST;
		return c;
	}
	yyerror("syntax error");
	return ZN;
}

static Lextok*
parse_varref(void)
{
	Lextok *r;

	if(yypeek() == NAME){
		yyget(); r = yylval;
		r = mk_explicit(r, Expand_Ok, NAME);
		return r;
	}
	if(yypeek() == UNAME){
		yyget(); r = yylval;
		r = mk_explicit(r, Expand_Ok, NAME);
		return r;
	}
	yyerror("syntax error");
	return ZN;
}

static Lextok*
parse_cmpnd(void)
{
	Lextok *f, *s;

	f = parse_pfld();
	Embedded++;
	s = parse_sfld();
	if(f->sym->type == STRUCT)
		owner = f->sym->Snm;
	f->rgt = s;
	if(s && f->sym->type != STRUCT)
		f->sym->type = STRUCT;
	Embedded--;
	if(!Embedded && !NamesNotAdded && !f->sym->type)
		fatal("undeclared variable: %s", f->sym->name);
	if(s)
		validref(f, s->lft);
	owner = ZS;
	return f;
}

static Lextok*
parse_sfld(void)
{
	Lextok *f;

	if(yypeek() != '.')
		return ZN;
	yyget();
	f = parse_cmpnd();
	return nn(ZN, '.', f, ZN);
}

static Lextok*
parse_pfld(void)
{
	Lextok *r, *e;

	/* pfld: NAME | NAME '[' expr ']' (spin.y) */
	if(yypeek() != NAME){
		yyerror("syntax error");
		return ZN;
	}
	yyget(); r = yylval;
	if(yypeek() == '['){
		yyget();  /* '[' */
		e = parse_expr();
		if(yypeek() != ']')
			yyerror("syntax error");
		yyget();  /* ']' */
		return nn(r, NAME, e, ZN);
	}
	if(r->sym->isarray && !in_for)
		non_fatal("missing array index for '%s'", r->sym->name);
	return nn(r, NAME, ZN, ZN);
}

/*
 * var_list: ivar | ivar ',' var_list
 */
static Lextok*
parse_var_list(void)
{
	Lextok *r, *rest;

	r = parse_ivar();
	if(yypeek() == ','){
		yyget();
		rest = parse_var_list();
		return nn(r, TYPE, ZN, rest);
	}
	return nn(r, TYPE, ZN, ZN);
}

/*
 * ivar: vardcl | vardcl ASGN '{' c_list '}' |
 *       vardcl ASGN expr | vardcl ASGN ch_init
 */
static Lextok*
parse_ivar(void)
{
	Lextok *v, *r;

	v = parse_vardcl();
	v->sym->ini = nn(ZN, CONST, ZN, ZN);
	v->sym->ini->val = 0;
	if(!initialization_ok){
		Lextok *zx, *xz;
		zx = nn(ZN, NAME, ZN, ZN);
		zx->sym = v->sym;
		xz = nn(zx, ASGN, zx, v->sym->ini);
		keep_track_off(xz);
		add_seq(xz);
	}

	if(yypeek() == ASGN){
		yyget();
		if(yypeek() == '{'){
			yyget();
			if(v->sym->isarray)
				v->sym->hidden |= (4|8);
			v->sym->ini = parse_c_list();
			has_ini = 1;
			if(yypeek() != '}')
				yyerror("syntax error");
			yyget();
		} else {
			r = parse_expr();
			v->sym->ini = r;
			if(r->ntyp == CONST || (r->ntyp == NAME && r->sym->context))
				has_ini = 2;
			else
				has_ini = 1;
			trackvar(v, r);
			if(any_oper(r, RUN))
				fatal("cannot use 'run' in var init", (char *)0);
			nochan_manip(v, r, 0);
			no_internals(v);
			if(!initialization_ok){
				Lextok *zx = nn(ZN, NAME, ZN, ZN);
				zx->sym = v->sym;
				add_seq(nn(zx, ASGN, zx, r));
				v->sym->ini = 0;
			}
		}
	}

	return v;
}

/*
 * vardcl: NAME | NAME ':' CONST | NAME '[' const_expr ']' |
 *          NAME '[' NAME ']'
 */
static Lextok*
parse_vardcl(void)
{
	Lextok *name, *c, *n2;

	yyget(); name = yylval;  /* NAME */
	name->sym->nel = 1;

	if(yypeek() == ':'){
		yyget();  /* ':' */
		if(yypeek() != CONST)
			yyerror("syntax error");
		yyget(); c = yylval;
		name->sym->nbits = c->val;
		if(c->val >= 8*sizeof(long)){
			non_fatal("width-field %s too large", name->sym->name);
			c->val = 8*sizeof(long)-1;
		}
		name->sym->nel = 1;
		return name;
	}
	if(yypeek() == '['){
		yyget();  /* '[' */
		if(yypeek() == CONST){
			c = parse_const_expr();
			name->sym->nel = c->val;
			name->sym->isarray = 1;
		} else if(yypeek() == NAME){
			yyget(); n2 = yylval;
			/* warning: NAME in array bound */
			if(n2->sym->ini && n2->sym->ini->val > 0)
				name->sym->nel = n2->sym->ini->val;
			else
				name->sym->nel = 1;
			name->sym->isarray = 1;
		}
		if(yypeek() != ']')
			yyerror("syntax error");
		yyget();  /* ']' */
		return name;
	}

	return name;
}

/*
 * c_list: CONST | CONST ',' c_list
 */
static Lextok*
parse_c_list(void)
{
	Lextok *c, *r;

	if(yypeek() != CONST)
		yyerror("syntax error");
	yyget(); c = yylval;
	c->ntyp = CONST;
	if(yypeek() == ','){
		yyget();
		r = parse_c_list();
		return nn(c, ',', c, r);
	}
	return c;
}

/*
 * one_decl: vis TYPE osubt var_list | vis UNAME var_list |
 *            vis TYPE asgn '{' nlst '}'
 */
static Lextok*
parse_one_decl(void)
{
	Lextok *vis, *type, *osubt, *vl;

	vis = parse_vis();
	yyget(); type = yylval;

	if(yypeek() == ':'){
		yyget();  /* ':' */
		yyget(); osubt = yylval;  /* NAME */
	} else
		osubt = ZN;

	vl = parse_var_list();
	setptype(osubt, vl, type->val, vis);
	vl->val = type->val;
	return vl;
}

static Lextok*
parse_vis(void)
{
	if(yypeek() == HIDDEN){
		yyget();
		return yylval;
	}
	if(yypeek() == SHOW){
		yyget();
		return yylval;
	}
	if(yypeek() == ISLOCAL){
		yyget();
		return yylval;
	}
	return ZN;
}

static Lextok*
parse_asgn(void)
{
	Lextok *n;

	if(yypeek() == ':'){
		yyget();
		if(yypeek() == NAME){
			yyget(); n = yylval;
			return n;  /* mtype decl */
		}
	}
	if(yypeek() == ASGN){
		yyget();
		return ZN;
	}
	yyerror("syntax error");
	return ZN;
}

static Lextok*
parse_osubt(void)
{
	if(yypeek() == ':'){
		yyget();
		if(yypeek() == NAME){
			yyget();
			return yylval;
		}
	}
	return ZN;
}

/*
 * decl_lst: one_decl | one_decl SEMI decl_lst
 */
static Lextok*
parse_decl_lst(void)
{
	Lextok *r, *r2;

	r = parse_one_decl();
	if(yypeek() == SEMI){
		yyget();
		r2 = parse_decl_lst();
		return nn(ZN, ',', r, r2);
	}
	return nn(ZN, ',', r, ZN);
}

/*
 * decl: empty | decl_lst
 */
static Lextok*
parse_decl(void)
{
	if(yypeek() == ')' || yypeek() == '}')
		return ZN;
	return parse_decl_lst();
}

/*
 * vref_lst: varref | varref ',' vref_lst
 */
static Lextok*
parse_vref_lst(void)
{
	Lextok *v, *r;

	v = parse_varref();
	if(yypeek() == ','){
		yyget();
		r = parse_vref_lst();
		return nn(v, XU, v, r);
	}
	return nn(v, XU, v, ZN);
}

/*
 * aname: NAME | PNAME
 */
static Lextok*
parse_aname(void)
{
	Lextok *r = ZN;

	if(yypeek() == NAME || yypeek() == PNAME){
		yyget(); r = yylval;
	} else
		yyerror("syntax error");
	return r;
}

/*
 * basetype: TYPE oname | UNAME | error
 */
static Lextok*
parse_basetype(void)
{
	Lextok *r;

	if(yypeek() == UNAME){
		yyget(); r = yylval;
		r->val = STRUCT;
		return r;
	}
	if(yypeek() == TYPE){
		Lextok *n;
		yyget(); r = yylval;
		if(yypeek() == ':'){
			yyget();
			n = parse_aname();
			r->sym = n ? n->sym : ZS;
		}
		if(r->val != MTYPE)
			fatal("unexpected type", (char *)0);
		return r;
	}
	/* error case */
	yyerror("syntax error");
	return ZN;
}

/*
 * typ_list: basetype | basetype ',' typ_list
 */
static Lextok*
parse_typ_list(void)
{
	Lextok *b, *r;

	b = parse_basetype();
	if(yypeek() == ','){
		yyget();
		r = parse_typ_list();
		return nn(b, b->val, ZN, r);
	}
	return nn(b, b->val, ZN, ZN);
}

/*
 * two_args: expr ',' expr
 */
static Lextok*
parse_two_args(void)
{
	Lextok *l, *r;

	l = parse_expr();
	if(yypeek() != ',')
		yyerror("syntax error");
	yyget();
	r = parse_expr();
	return nn(ZN, ',', l, r);
}

/*
 * args: empty | arg
 */
static Lextok*
parse_args(void)
{
	if(yypeek() == ')')
		return ZN;
	return parse_arg();
}

/*
 * prargs: empty | ',' arg
 */
static Lextok*
parse_prargs(void)
{
	if(yypeek() == ')')
		return ZN;
	if(yypeek() == ','){
		yyget();
		return parse_arg();
	}
	return parse_arg();
}

/*
 * margs: arg | expr l_par arg r_par
 */
static Lextok*
parse_margs(void)
{
	Lextok *a, *inner;

	a = parse_arg();
	if(yypeek() == '('){
		yyget();
		inner = parse_arg();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		if(a->ntyp == ',')
			return tail_add(a, inner);
		else
			return nn(ZN, ',', a, inner);
	}
	return a;
}

/*
 * arg: expr | expr ',' arg
 */
static Lextok*
parse_arg(void)
{
	Lextok *e, *rest;

	e = parse_expr();
	if(e->ntyp == ',')
		return e;
	else {
		if(yypeek() == ','){
			yyget();
			rest = parse_arg();
			return nn(ZN, ',', e, rest);
		}
		return nn(ZN, ',', e, ZN);
	}
}

/*
 * rarg: varref | EVAL l_par expr r_par | CONST | '-' CONST
 */
static Lextok*
parse_rarg(void)
{
	Lextok *e, *c, *r;

	if(yypeek() == EVAL){
		yyget();
		if(yypeek() != '(')
			yyerror("syntax error");
		yyget();
		e = parse_expr();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		return nn(ZN, EVAL, e, ZN);
	}
	if(yypeek() == CONST){
		yyget(); c = yylval;
		r = nn(ZN, CONST, ZN, ZN);
		r->ismtyp = c->ismtyp;
		r->sym = c->sym;
		r->val = c->val;
		return r;
	}
	if(yypeek() == '-'){
		yyget();
		if(yypeek() != CONST)
			yyerror("syntax error");
		yyget(); c = yylval;
		r = nn(ZN, CONST, ZN, ZN);
		r->val = -(c->val);
		return r;
	}
	return parse_varref();
}

/*
 * rargs: rarg | rarg ',' rargs | rarg l_par rargs r_par | l_par rargs r_par
 */
static Lextok*
parse_rargs(void)
{
	Lextok *r, *rest, *inner;

	if(yypeek() == '('){
		yyget();
		r = parse_rargs();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		return r;
	}
	r = parse_rarg();
	if(r->ntyp == ',')
		return r;

	if(yypeek() == ','){
		yyget();
		rest = parse_rargs();
		return nn(ZN, ',', r, rest);
	}
	if(yypeek() == '('){
		yyget();
		inner = parse_rargs();
		if(yypeek() != ')')
			yyerror("syntax error");
		yyget();
		if(r->ntyp == ',')
			return tail_add(r, inner);
		else
			return nn(ZN, ',', r, inner);
	}
	return nn(ZN, ',', r, ZN);
}

/*
 * nlst: NAME | nlst NAME | nlst ','
 */
static Lextok*
parse_nlst(void)
{
	Lextok *r, *t;

	yyget(); t = yylval;
	r = nn(t, NAME, ZN, ZN);
	r = nn(ZN, ',', r, ZN);
	while(yypeek() == NAME){
		yyget(); t = yylval;
		r = nn(ZN, ',', nn(t, NAME, ZN, ZN), r);
	}
	if(yypeek() == ',')
		yyget();
	return r;
}

#define binop(n, sop)	fprintf(fd, "("); recursive(fd, n->lft); \
			fprintf(fd, ") %s (", sop); recursive(fd, n->rgt); \
			fprintf(fd, ")");
#define unop(n, sop)	fprintf(fd, "%s (", sop); recursive(fd, n->lft); \
			fprintf(fd, ")");

static void
recursive(FILE *fd, Lextok *n)
{
	if (n)
	switch (n->ntyp) {
	case NEXT:
		unop(n, "X");
		break;
	case ALWAYS:
		unop(n, "[]");
		break;
	case EVENTUALLY:
		unop(n, "<>");
		break;
	case '!':
		unop(n, "!");
		break;
	case UNTIL:
		binop(n, "U");
		break;
	case WEAK_UNTIL:
		binop(n, "W");
		break;
	case RELEASE:
		binop(n, "V");
		break;
	case OR:
		binop(n, "||");
		break;
	case AND:
		binop(n, "&&");
		break;
	case IMPLIES:
		binop(n, "->");
		break;
	case EQUIV:
		binop(n, "<->");
		break;
	case C_EXPR:
		fprintf(fd, "c_expr { %s }", put_inline(fd, n->sym->name));
		break;
	default:
		comment(fd, n, 0);
		break;
	}
}

static Lextok *
ltl_to_string(Lextok *n)
{	Lextok *m = nn(ZN, 0, ZN, ZN);
	ssize_t retval;
	char *ltl_formula = NULL;
	FILE *tf = fopen(TMP_FILE1, "w+");

	if (!tf)
	{	fatal("cannot create temporary file", (char *) 0);
	}
	dont_simplify = 1;
	recursive(tf, n);
	dont_simplify = 0;
	(void) fseek(tf, 0L, SEEK_SET);

	size_t linebuffsize = 0;
	retval = getline(&ltl_formula, &linebuffsize, tf);
	fclose(tf);

	(void) unlink(TMP_FILE1);

	if (!retval)
	{	printf("%ld\n", (long int) retval);
		fatal("could not translate ltl ltl_formula", 0);
	}

	if (1) printf("ltl %s: %s\n", ltl_name, ltl_formula);

	m->sym = lookup(ltl_formula);
#ifndef __MINGW32__
	free(ltl_formula);
#endif
	return m;
}

int
is_temporal(int t)
{
	return (t == EVENTUALLY || t == ALWAYS || t == UNTIL
	     || t == WEAK_UNTIL || t == RELEASE);
}

int
is_boolean(int t)
{
	return (t == AND || t == OR || t == IMPLIES || t == EQUIV);
}

void
yyerror(char *fmt, ...)
{
	non_fatal(fmt, (char *) 0);
}
