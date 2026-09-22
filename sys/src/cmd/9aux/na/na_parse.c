/*
 * Hand-written recursive-descent replacement for na.y (LALR).
 *
 * Original yacc grammar (precedence low -> high):
 *	%left '-' '+'
 *	%left '*' '/'
 *	%left NEG		negation -- unary minus
 *	%right '^'		exponentiation (declared but no production uses it)
 *	input: | input line
 *	line: .label .opcode .comment '\n' {emit}
 *	    | ABSOLUTE SYMBOL '=' exp .comment '\n' {setsym}
 *	    | SYMBOL '=' exp .comment '\n' {setsym}
 *	    | EXTERN SYMBOL {extern}
 *	.label: SYMBOL ':' {define} |
 *	.comment: COMMENT |
 *	.opcode: opcode | (empty: out.len = 0)
 *	opcode: SET/CLEAR set_list | DISCONNECT | INT exp .cond |
 *	    INTFLY exp .cond | jump_or_call exp .cond |
 *	    jump_or_call REL '(' exp ')' .cond |
 *	    MOVE exp ',' .ptr regexp ',' with_or_when phase |
 *	    MOVE FROM exp ',' with_or_when phase |
 *	    MOVE MEMORY exp ',' regexp ',' regexp |
 *	    MOVE regA TO regA | MOVE exp TO regA |
 *	    MOVE regA '|'/'&'/'+'/'-' exp TO regA [+ WITH CARRY] |
 *	    MOVE regA SHL/SHR TO regA | MOVE regA XOR exp TO regA |
 *	    NOP | RESELECT ... | RETURN .cond | SELECT ... |
 *	    WAIT ... | DEFW exp
 *	.cond: ',' IF ... | ',' WHEN ... | (empty: COND_TRUE)
 *	exp: NUM | SYMBOL | exp '+'/'-'/'*'/'/' exp |
 *	    '-' exp %prec NEG | '(' exp ')' | '~' exp %prec NEG
 *	byteexp: exp (with truncation check)
 *	regexp: exp | regA
 *
 * Precedence-map (hand parser):
 *	level 1 (lowest, left): '-' '+'
 *	level 2 (left): '*' '/'
 *	level 3 (highest): unary '-' (NEG) and '~' (NEG), right-assoc
 *	'^' is declared %right in yacc but no exp production uses it;
 *	hand parser treats '^' as a syntax error (yyerror + skip).
 *	Call chain: parse_exp -> parse_add -> parse_mul ->
 *	            parse_unary -> parse_primary.
 *	All other nonterminals (line, opcode, .cond, reg, phase, etc.)
 *	are one function each; left recursion replaced by loops
 *	(input: input line -> while(peek != EOF) parse_line();
 *	 set_list: set_bit (,|AND set_bit)* -> loop).
 *
 * Error-recovery-map: original had no `error' production; syntax
 *	error called yyerror() (errors++) and yacc aborted (ret1).
 *	Hand parser calls yyerror() at the exact failure point, then
 *	skips to sync ('\n' or EOF) and continues with the next line,
 *	so one pass reports all line errors; main still exits
 *	("pass1"/"pass2") when errors != 0, preserving the protocol.
 */

#include <u.h>
#include <libc.h>
#include <stdio.h>
#include <ctype.h>

#include "na.h"

#define COND_WAIT (1L << 16)
#define COND_TRUE (1L << 19)
#define COND_INTFLY (1L << 20)
#define COND_CARRY (1L << 21)
#define COND_REL (1L << 23)
#define COND_PHASE (1L << 17)
#define COND_DATA (1L << 18)

#define IO_REL (1L << 26)

#define MOVE_MODE (1L << 27)

int yyparse(void);
void assemble(void);
void yyerror(char *, ...);
void yywarn(char *, ...);
void p2error(int line, char *);

struct addr {
	int type; /* 0 - direct, 1 - indirect 2 - table indirect */
	unsigned long offset;
};

typedef enum Type { Const, Addr, Table, Extern, Reg, Unknown, Error } Type;

struct sym {
	char *name;
	int set;
	Type t;
	long value;
	struct sym *next;
};

struct sym *findsym(char *name);
struct sym *symlist;

void newsym(struct sym *s, Type t, long v);

struct binary {
	char len;
	unsigned long data[3];
	unsigned char patch[3];
};

#define MAXCPPOPTS 30
#define MAX_PATCHES 1000
struct na_patch patch[MAX_PATCHES];
int patches;

struct binary out;

struct expval {
	Type t;
	long value;
};

struct expval eval(struct expval a, struct expval b, char op);

int patchtype(Type t);
void fixup(void);

unsigned dot;
unsigned externs;
int errors, warnings;
struct sym *externp[100];

void regmove(unsigned char src_reg, unsigned char op,
    unsigned char dst_reg, struct expval *imm);

void preprocess(char *in, FILE *out);

int mk24bitssigned(long *l);
long mkreladdr(long value, int len);
long chkreladdr(int d, struct expval *e, int len, long relrv);
int pass2;
FILE *in_f;

int yyline = 0;
char yyfilename[200];
char line[500];
char *cppopts[MAXCPPOPTS];
int ncppopts;
int wflag;

/* ---- yacc replacement: YYSTYPE, tokens, lookahead ---- */

typedef union {
	long n;
	struct sym *s;
	struct expval e;
} YYSTYPE;
YYSTYPE yylval;

/* Token numbers: yacc replacement uses #define (like y.tab.h), not
 * enum, because Plan 9 cc treats enum members as LNAME and any prior
 * #define (system headers, -D flags, or other headers) makes the enum
 * fail with "expected '}'" at the first colliding member. #undef +
 * #define exactly mimics yacc output and always compiles.
 */
#undef NUM
#define NUM 257
#undef MOVE
#define MOVE 258
#undef WHEN
#define WHEN 259
#undef SYMBOL
#define SYMBOL 260
#undef SELECT
#define SELECT 261
#undef WAIT
#define WAIT 262
#undef DISCONNECT
#define DISCONNECT 263
#undef RESELECT
#define RESELECT 264
#undef SET
#define SET 265
#undef CLEAR
#define CLEAR 266
#undef DATA_OUT
#define DATA_OUT 267
#undef DATA_IN
#define DATA_IN 268
#undef COMMAND
#define COMMAND 269
#undef STATUS
#define STATUS 270
#undef RESERVED_OUT
#define RESERVED_OUT 271
#undef RESERVED_IN
#define RESERVED_IN 272
#undef MESSAGE_OUT
#define MESSAGE_OUT 273
#undef MESSAGE_IN
#define MESSAGE_IN 274
#undef WITH
#define WITH 275
#undef ATN
#define ATN 276
#undef FAIL
#define FAIL 277
#undef CARRY
#define CARRY 278
#undef TARGET
#define TARGET 279
#undef ACK
#define ACK 280
#undef COMMENT
#define COMMENT 281
#undef TO
#define TO 282
#undef SCNTL0
#define SCNTL0 283
#undef SCNTL1
#define SCNTL1 284
#undef SCNTL2
#define SCNTL2 285
#undef SCNTL3
#define SCNTL3 286
#undef SCID
#define SCID 287
#undef SXFER
#define SXFER 288
#undef SDID
#define SDID 289
#undef GPREG
#define GPREG 290
#undef SFBR
#define SFBR 291
#undef SOCL
#define SOCL 292
#undef SSID
#define SSID 293
#undef SBCL
#define SBCL 294
#undef DSTAT
#define DSTAT 295
#undef SSTAT0
#define SSTAT0 296
#undef SSTAT1
#define SSTAT1 297
#undef SSTAT2
#define SSTAT2 298
#undef ISTAT
#define ISTAT 299
#undef CTEST0
#define CTEST0 300
#undef CTEST1
#define CTEST1 301
#undef CTEST2
#define CTEST2 302
#undef CTEST3
#define CTEST3 303
#undef TEMP
#define TEMP 304
#undef DFIFO
#define DFIFO 305
#undef CTEST4
#define CTEST4 306
#undef CTEST5
#define CTEST5 307
#undef CTEST6
#define CTEST6 308
#undef DBC
#define DBC 309
#undef DCMD
#define DCMD 310
#undef DNAD
#define DNAD 311
#undef DSP
#define DSP 312
#undef DSPS
#define DSPS 313
#undef DMODE
#define DMODE 314
#undef DIEN
#define DIEN 315
#undef DWT
#define DWT 316
#undef DCNTL
#define DCNTL 317
#undef ADDER
#define ADDER 318
#undef SIEN0
#define SIEN0 319
#undef SIEN1
#define SIEN1 320
#undef SIST0
#define SIST0 321
#undef SIST1
#define SIST1 322
#undef SLPAR
#define SLPAR 323
#undef MACNTL
#define MACNTL 324
#undef GPCNTL
#define GPCNTL 325
#undef STIME0
#define STIME0 326
#undef STIME1
#define STIME1 327
#undef RESPID
#define RESPID 328
#undef STEST0
#define STEST0 329
#undef STEST1
#define STEST1 330
#undef STEST2
#define STEST2 331
#undef STEST3
#define STEST3 332
#undef SIDL
#define SIDL 333
#undef SODL
#define SODL 334
#undef SBDL
#define SBDL 335
#undef SHL
#define SHL 336
#undef SHR
#define SHR 337
#undef AND
#define AND 338
#undef OR
#define OR 339
#undef XOR
#define XOR 340
#undef ADD
#define ADD 341
#undef ADDC
#define ADDC 342
#undef JUMP
#define JUMP 343
#undef CALL
#define CALL 344
#undef RETURN
#define RETURN 345
#undef INT
#define INT 346
#undef INTFLY
#define INTFLY 347
#undef NOT
#define NOT 348
#undef ABSOLUTE
#define ABSOLUTE 349
#undef MASK
#define MASK 350
#undef IF
#define IF 351
#undef REL
#define REL 352
#undef PTR
#define PTR 353
#undef TABLE
#define TABLE 354
#undef FROM
#define FROM 355
#undef MEMORY
#define MEMORY 356
#undef NOP
#define NOP 357
#undef EXTERN
#define EXTERN 358
#undef SCRATCHA0
#define SCRATCHA0 359
#undef SCRATCHA1
#define SCRATCHA1 360
#undef SCRATCHA2
#define SCRATCHA2 361
#undef SCRATCHA3
#define SCRATCHA3 362
#undef SCRATCHB0
#define SCRATCHB0 363
#undef SCRATCHB1
#define SCRATCHB1 364
#undef SCRATCHB2
#define SCRATCHB2 365
#undef SCRATCHB3
#define SCRATCHB3 366
#undef SCRATCHC0
#define SCRATCHC0 367
#undef SCRATCHC1
#define SCRATCHC1 368
#undef SCRATCHC2
#define SCRATCHC2 369
#undef SCRATCHC3
#define SCRATCHC3 370
#undef DSA0
#define DSA0 371
#undef DSA1
#define DSA1 372
#undef DSA2
#define DSA2 373
#undef DSA3
#define DSA3 374
#undef DEFW
#define DEFW 375

int yylex(void);

/* N-token lookahead over yylex() (hardened: same pattern as cc;
 * the old fixed 2-entry buffer with `if(nla == 2)` shift breaks
 * as soon as any code peeks past index 1).
 */
#undef NLA
#define NLA 8
static int nla;
static int latok[NLA];
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

static int
yypeek(int n)
{
	if(n < 0 || n >= NLA){
		yyerror("lookahead overflow");
		return 0;
	}
	lafill(n);
	return latok[n];
}

static int
yyget(void)
{
	int t;
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
skiptonl(void)
{
	int t;

	for(;;){
		t = yypeek(0);
		if(t == 0 || t == '\n')
			break;
		yyget();
	}
}

/* forward decls */
static void parse_input(void);
static void parse_line(void);
static void parse_label_opt(void);
static void parse_opcode_opt(void);
static void parse_comment_opt(void);
static void parse_opcode(void);
static long parse_set_cmd(void);
static long parse_set_list(void);
static long parse_set_bit(void);
static long parse_cond_opt(void);
static long parse_condsfbr(void);
static long parse_condphase(void);
static long parse_phase(void);
static long parse_atn_opt(void);
static long parse_ptr_opt(void);
static void parse_with_or_when(void);
static long parse_jump_or_call(void);
static long parse_regA(void);
static long parse_reg(void);
static struct expval parse_byteexp(void);
static struct expval parse_regexp(void);
static struct expval parse_exp(void);
static struct expval parse_add(void);
static struct expval parse_mul(void);
static struct expval parse_unary(void);
static struct expval parse_primary(void);
static void setsym(struct sym *s, Type t, long v);
static void emitline(void);

int
yyparse(void)
{
	yyclearla();
	parse_input();
	return 0;
}

static void
parse_input(void)
{
	int t;

	for(;;){
		t = yypeek(0);
		if(t == 0)
			break;
		parse_line();
	}
}

static void
emitline(void)
{
	int x;

	if(pass2){
		for(x = 0; x < out.len; x++){
			printf("/* %.4x */ 0x%.8lxL,",
			    dot, out.data[x]);
			if(x == 0){
				printf(" /*\t");
				fwrite(line, strlen(line) - 1, 1, stdout);
				printf(" */");
			}
			printf("\n");
			if(out.patch[x]){
				patch[patches].lwoff = dot / 4;
				patch[patches].type = out.patch[x];
				patches++;
			}
			dot += 4;
		}
	}else
		dot += 4 * out.len;
}

static void
parse_line(void)
{
	int t0, t1;
	struct sym *s;
	struct expval e;

	t0 = yypeek(0);
	if(t0 == 0)
		return;
	/* ABSOLUTE SYMBOL '=' exp .comment '\n' */
	if(t0 == ABSOLUTE){
		yyget();
		if(yypeek(0) != SYMBOL){
			yyerror("expected symbol after absolute");
			skiptonl();
			if(yypeek(0) == '\n')
				yyget();
			return;
		}
		yyget();
		s = yylval.s;
		if(yyget() != '='){
			yyerror("expected '=' after symbol");
			skiptonl();
			if(yypeek(0) == '\n')
				yyget();
			return;
		}
		e = parse_exp();
		parse_comment_opt();
		if(yypeek(0) != '\n'){
			yyerror("expected newline");
			skiptonl();
		}
		if(yypeek(0) == '\n')
			yyget();
		setsym(s, e.t, e.value);
		if(pass2){
			printf("\t\t\t/*\t");
			fwrite(line, strlen(line) - 1, 1, stdout);
			printf(" */\n");
		}
		return;
	}
	/* EXTERN SYMBOL (no trailing newline in yacc; leave '\n' for next line) */
	if(t0 == EXTERN){
		yyget();
		if(yypeek(0) != SYMBOL){
			yyerror("expected symbol after extern");
			skiptonl();
			return;
		}
		yyget();
		s = yylval.s;
		if(pass2){
			printf("\t\t\t/*\t");
			fwrite(line, strlen(line) - 1, 1, stdout);
			printf(" */\n");
		}else{
			externp[externs] = s;
			setsym(s, Extern, externs++);
		}
		return;
	}
	/* SYMBOL '=' exp .comment '\n'  vs  .label .opcode .comment '\n' */
	if(t0 == SYMBOL){
		t1 = yypeek(1);
		if(t1 == '='){
			yyget();
			s = yylval.s;
			yyget(); /* '=' */
			e = parse_exp();
			parse_comment_opt();
			if(yypeek(0) != '\n'){
				yyerror("expected newline");
				skiptonl();
			}
			if(yypeek(0) == '\n')
				yyget();
			setsym(s, e.t, e.value);
			if(pass2){
				printf("\t\t\t/*\t");
				fwrite(line, strlen(line) - 1, 1, stdout);
				printf(" */\n");
			}
			return;
		}
	}
	/* general: .label .opcode .comment '\n' */
	parse_label_opt();
	parse_opcode_opt();
	parse_comment_opt();
	if(yypeek(0) != '\n'){
		yyerror("expected newline");
		skiptonl();
	}
	if(yypeek(0) == '\n')
		yyget();
	emitline();
}

static void
parse_label_opt(void)
{
	struct sym *s;

	if(yypeek(0) == SYMBOL && yypeek(1) == ':'){
		yyget();
		s = yylval.s;
		yyget(); /* ':' */
		if(s->t != Unknown){
			if(!pass2)
				yyerror("multiply defined symbol");
		}else{
			s->t = Addr;
			s->value = dot;
		}
	}
}

static void
parse_comment_opt(void)
{
	if(yypeek(0) == COMMENT)
		yyget();
}

static int
isopcode(int t)
{
	return t == SET || t == CLEAR || t == DISCONNECT || t == INT ||
	    t == INTFLY || t == JUMP || t == CALL || t == MOVE ||
	    t == NOP || t == RESELECT || t == RETURN || t == SELECT ||
	    t == WAIT || t == DEFW;
}

static void
parse_opcode_opt(void)
{
	if(isopcode(yypeek(0)))
		parse_opcode();
	else
		out.len = 0;
}

static void
parse_opcode(void)
{
	int t;
	struct expval e1, e2, e3;
	long v, c;
	long reg;

	t = yypeek(0);
	if(t == SET || t == CLEAR){
		v = parse_set_cmd();
		c = parse_set_list();
		out.len = 2;
		out.data[0] = (1L << 30) | (v << 27) | c;
		out.data[1] = 0;
		out.patch[0] = out.patch[1] = 0;
		return;
	}
	if(t == DISCONNECT){
		yyget();
		out.len = 2;
		out.data[0] = 0x48020000L;
		out.data[1] = 0;
		out.patch[0] = out.patch[1] = 0;
		return;
	}
	if(t == INT || t == INTFLY){
		int fly;

		fly = (t == INTFLY);
		yyget();
		e1 = parse_exp();
		c = parse_cond_opt();
		out.len = 2;
		out.data[0] = c | 0x98000000L;
		if(fly)
			out.data[0] |= COND_INTFLY;
		out.data[1] = e1.value;
		out.patch[0] = out.patch[1] = 0;
		return;
	}
	if(t == JUMP || t == CALL){
		v = parse_jump_or_call();
		/* jump_or_call exp .cond  vs  jump_or_call REL '(' exp ')' .cond */
		if(yypeek(0) == REL){
			yyget();
			if(yyget() != '('){
				yyerror("expected '(' after rel");
				skiptonl();
				out.len = 0;
				return;
			}
			e1 = parse_exp();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptonl();
				out.len = 0;
				return;
			}
			c = parse_cond_opt();
			out.len = 2;
			out.data[0] = v | c | COND_REL;
			out.data[1] = mkreladdr(e1.value, 2);
			out.patch[0] = out.patch[1] = 0;
			return;
		}
		e1 = parse_exp();
		c = parse_cond_opt();
		out.len = 2;
		out.data[0] = v | c | chkreladdr(1, &e1, 2, COND_REL);
		out.patch[0] = 0;
		return;
	}
	if(t == MOVE){
		struct expval imm;
		long ptr, ph;

		yyget();
		t = yypeek(0);
		/* MOVE MEMORY exp ',' regexp ',' regexp */
		if(t == MEMORY){
			yyget();
			e1 = parse_exp();
			if(yyget() != ','){
				yyerror("expected ','");
				skiptonl();
				out.len = 0;
				return;
			}
			e2 = parse_regexp();
			if(yyget() != ','){
				yyerror("expected ','");
				skiptonl();
				out.len = 0;
				return;
			}
			e3 = parse_regexp();
			out.len = 3;
			out.data[0] = 0xc0000000L | e1.value;
			out.data[1] = e2.value;
			out.data[2] = e3.value;
			out.patch[0] = 0;
			out.patch[1] = patchtype(e2.t);
			out.patch[2] = patchtype(e3.t);
			return;
		}
		/* MOVE FROM exp ',' with_or_when phase */
		if(t == FROM){
			yyget();
			e1 = parse_exp();
			if(yyget() != ','){
				yyerror("expected ','");
				skiptonl();
				out.len = 0;
				return;
			}
			parse_with_or_when();
			ph = parse_phase();
			out.len = 2;
			out.data[0] = (ph << 24) | (1L << 28) | MOVE_MODE;
			out.data[1] = e1.value;
			out.patch[0] = 0;
			out.patch[1] = patchtype(e1.t);
			return;
		}
		/* reg-to-reg and reg-imm forms start with regA or exp.
		 * Distinguish: if current token can start regA and the
		 * following token is TO/'|'/'&'/'+'/'-'/SHL/SHR/XOR,
		 * treat as reg form; otherwise generic move.
		 * Simplest: try regA lookahead for the common patterns.
		 */
		/* Peek for regA TO regA etc. We attempt reg parse only
		 * when it unambiguously leads to TO or an operator.
		 * Otherwise fall through to generic exp move.
		 * To keep it simple and faithful, check for reg tokens.
		 */
		if(t == SCNTL0 || t == SCNTL1 || t == SCNTL2 || t == SCNTL3 ||
		   t == SCID || t == SXFER || t == SDID || t == GPREG ||
		   t == SFBR || t == SOCL || t == SSID || t == SBCL ||
		   t == DSTAT || t == SSTAT0 || t == SSTAT1 || t == SSTAT2 ||
		   t == DSA0 || t == DSA1 || t == DSA2 || t == DSA3 ||
		   t == ISTAT || t == CTEST0 || t == CTEST1 || t == CTEST2 ||
		   t == CTEST3 || t == TEMP || t == DFIFO || t == CTEST4 ||
		   t == CTEST5 || t == CTEST6 || t == DBC || t == DCMD ||
		   t == DNAD || t == DSP || t == DSPS || t == SCRATCHA0 ||
		   t == SCRATCHA1 || t == SCRATCHA2 || t == SCRATCHA3 ||
		   t == DMODE || t == DIEN || t == DWT || t == DCNTL ||
		   t == ADDER || t == SIEN0 || t == SIEN1 || t == SIST0 ||
		   t == SIST1 || t == SLPAR || t == MACNTL || t == GPCNTL ||
		   t == STIME0 || t == STIME1 || t == RESPID || t == STEST0 ||
		   t == STEST1 || t == STEST2 || t == STEST3 || t == SIDL ||
		   t == SODL || t == SBDL || t == SCRATCHB0 || t == SCRATCHB1 ||
		   t == SCRATCHB2 || t == SCRATCHB3 || t == SCRATCHC0 ||
		   t == SCRATCHC1 || t == SCRATCHC2 || t == SCRATCHC3){
			/* could be reg form; parse first regA */
			long src;

			src = parse_regA();
			t = yypeek(0);
			if(t == TO){
				yyget();
				reg = parse_regA();
				regmove((unsigned char)src, 2, (unsigned char)reg, 0);
				return;
			}
			if(t == '|' || t == '&' || t == '+' || t == '-' || t == XOR){
				int op = t;
				yyget();
				/* SHL/SHR are separate: MOVE regA SHL TO regA */
				e1 = parse_exp();
				if(yyget() != TO){
					yyerror("expected TO");
					skiptonl();
					out.len = 0;
					return;
				}
				reg = parse_regA();
				if(op == '|')
					regmove((unsigned char)src, 2, (unsigned char)reg, &e1);
				else if(op == '&')
					regmove((unsigned char)src, 4, (unsigned char)reg, &e1);
			else if(op == '+' || op == '-'){
				long withcarry = 0;
				if(yypeek(0) == WITH){
					yyget();
					if(yyget() != CARRY){
						yyerror("expected CARRY after WITH");
						skiptonl();
						out.len = 0;
						return;
					}
					withcarry = 1;
				}
				if(op == '-' && withcarry)
					e1.value = -e1.value;
				if(withcarry)
					regmove((unsigned char)src, 7, (unsigned char)reg, &e1);
				else
					regmove((unsigned char)src, 6, (unsigned char)reg, &e1);
			}
				else
					regmove((unsigned char)src, 3, (unsigned char)reg, &e1);
				return;
			}
			if(t == SHL || t == SHR){
				int op = t;
				yyget();
				if(yyget() != TO){
					yyerror("expected TO");
					skiptonl();
					out.len = 0;
					return;
				}
				reg = parse_regA();
				if(op == SHL)
					regmove((unsigned char)src, 1, (unsigned char)reg, 0);
				else
					regmove((unsigned char)src, 5, (unsigned char)reg, 0);
				return;
			}
			if(t == WITH){
				/* MOVE regA '+'/'-' exp TO regA WITH CARRY */
				yyerror("misplaced WITH");
				skiptonl();
				out.len = 0;
				return;
			}
			yyerror("expected TO or operator after register");
			skiptonl();
			out.len = 0;
			return;
		}
		/* Generic: MOVE exp ',' .ptr regexp ',' with_or_when phase
		 * but also: MOVE exp TO regA, MOVE regA ... handled above.
		 * Here handle MOVE exp TO regA and MOVE exp ... forms.
		 * We already consumed MOVE; parse exp first.
		 */
		e1 = parse_exp();
		t = yypeek(0);
		if(t == TO){
			yyget();
			reg = parse_regA();
			imm = e1;
			regmove((unsigned char)reg, 0, (unsigned char)reg, &imm);
			return;
		}
		if(t != ','){
			yyerror("expected ',' or TO after move source");
			skiptonl();
			out.len = 0;
			return;
		}
		yyget(); /* ',' */
		ptr = parse_ptr_opt();
		e2 = parse_regexp();
		if(yyget() != ','){
			yyerror("expected ','");
			skiptonl();
			out.len = 0;
			return;
		}
		parse_with_or_when();
		ph = parse_phase();
		out.len = 2;
		out.data[0] = (ph << 24) | e1.value | (ptr << 29) | MOVE_MODE;
		out.data[1] = e2.value;
		out.patch[0] = 0;
		out.patch[1] = patchtype(e2.t);
		return;
	}
	if(t == NOP){
		yyget();
		out.len = 2;
		out.data[0] = 0x80000000L;
		out.data[1] = 0;
		out.patch[0] = out.patch[1] = 0;
		return;
	}
	if(t == RESELECT){
		yyget();
		/* RESELECT exp ',' exp|REL... | RESELECT FROM exp ',' ... */
		if(yypeek(0) == FROM){
			yyget();
			e1 = parse_exp();
			if(yyget() != ','){
				yyerror("expected ','");
				skiptonl();
				out.len = 0;
				return;
			}
			if(yypeek(0) == REL){
				yyget();
				if(yyget() != '('){
					yyerror("expected '('");
					skiptonl();
					out.len = 0;
					return;
				}
				e2 = parse_exp();
				if(yyget() != ')'){
					yyerror("expected ')'");
					skiptonl();
					out.len = 0;
					return;
				}
				out.len = 2;
				out.data[0] = 0x40000000L | (1L << 25) | IO_REL | e1.value;
				out.patch[0] = 5;
				out.data[1] = mkreladdr(e2.value, 2);
				out.patch[1] = 0;
				return;
			}
			e2 = parse_exp();
			out.len = 2;
			out.data[0] = 0x40000000L | (1L << 25) | e1.value | chkreladdr(1, &e2, 2, IO_REL);
			out.patch[0] = 5;
			return;
		}
		e1 = parse_exp();
		if(yyget() != ','){
			yyerror("expected ','");
			skiptonl();
			out.len = 0;
			return;
		}
		if(yypeek(0) == REL){
			yyget();
			if(yyget() != '('){
				yyerror("expected '('");
				skiptonl();
				out.len = 0;
				return;
			}
			e2 = parse_exp();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptonl();
				out.len = 0;
				return;
			}
			out.len = 2;
			out.data[0] = 0x40000000L | IO_REL
			    | (e1.value << 16) | (1L << 9);
			out.data[1] = mkreladdr(e2.value, 2);
			out.patch[0] = out.patch[1] = 0;
			return;
		}
		e2 = parse_exp();
		out.len = 2;
		out.data[0] = 0x40000000L | (e1.value << 16) | (1L << 9) | chkreladdr(1, &e2, 2, IO_REL);
		out.patch[0] = 0;
		return;
	}
	if(t == RETURN){
		yyget();
		c = parse_cond_opt();
		out.len = 2;
		out.data[0] = 0x90000000L | c;
		out.data[1] = 0;
		out.patch[0] = out.patch[1] = 0;
		return;
	}
	if(t == SELECT){
		long atn;

		yyget();
		atn = parse_atn_opt();
		e1 = parse_exp();
		if(yyget() != ','){
			yyerror("expected ','");
			skiptonl();
			out.len = 0;
			return;
		}
		/* SELECT .atn exp ',' exp | REL ... | FROM ... */
		if(yypeek(0) == REL){
			yyget();
			if(yyget() != '('){
				yyerror("expected '('");
				skiptonl();
				out.len = 0;
				return;
			}
			e2 = parse_exp();
			if(yyget() != ')'){
				yyerror("expected ')'");
				skiptonl();
				out.len = 0;
				return;
			}
			/* SELECT .atn exp ',' REL '(' exp ')' */
			out.len = 2;
			out.data[0] = 0x40000000L | (1L << 26)
			    | (e1.value << 16) | (1L << 9) | atn;
			out.data[1] = mkreladdr(e2.value, 2);
			out.patch[0] = out.patch[1] = 0;
			return;
		}
		if(yypeek(0) == FROM){
			/* SELECT .atn FROM exp ',' exp|REL... */
			yyget();
			e1 = parse_exp();
			if(yyget() != ','){
				yyerror("expected ','");
				skiptonl();
				out.len = 0;
				return;
			}
			if(yypeek(0) == REL){
				yyget();
				if(yyget() != '('){
					yyerror("expected '('");
					skiptonl();
					out.len = 0;
					return;
				}
				e2 = parse_exp();
				if(yyget() != ')'){
					yyerror("expected ')'");
					skiptonl();
					out.len = 0;
					return;
				}
				out.len = 2;
				out.data[0] = 0x40000000L | (1L << 25) | IO_REL | e1.value | atn;
				out.patch[0] = 5;
				out.data[1] = mkreladdr(e2.value, 2);
				out.patch[1] = 0;
				return;
			}
			e2 = parse_exp();
			out.len = 2;
			out.data[0] = 0x40000000L | (1L << 25) | e1.value | atn | chkreladdr(1, &e2, 2, IO_REL);
			out.patch[0] = 5;
			return;
		}
		e2 = parse_exp();
		out.len = 2;
		out.data[0] =
		    0x40000000L | (e1.value << 16) | (1L << 9) | atn | chkreladdr(1, &e2, 2, IO_REL);
		out.patch[0] = 0;
		return;
	}
	if(t == WAIT){
		yyget();
		t = yypeek(0);
		if(t == DISCONNECT){
			yyget();
			out.len = 2;
			out.data[0] = 0x48000000L;
			out.data[1] = 0;
			out.patch[0] = out.patch[1] = 0;
			return;
		}
		if(t == RESELECT){
			yyget();
			if(yypeek(0) == REL){
				yyget();
				if(yyget() != '('){
					yyerror("expected '('");
					skiptonl();
					out.len = 0;
					return;
				}
				e1 = parse_exp();
				if(yyget() != ')'){
					yyerror("expected ')'");
					skiptonl();
					out.len = 0;
					return;
				}
				out.len = 2;
				out.data[0] = 0x50000000L | (1L << 26);
				out.data[1] = mkreladdr(e1.value, 2);
				out.patch[0] = out.patch[1] = 0;
				return;
			}
			e1 = parse_exp();
			out.len = 2;
			out.data[0] = 0x50000000L | chkreladdr(1, &e1, 2, IO_REL);
			out.patch[0] = 0;
			return;
		}
		if(t == SELECT){
			yyget();
			if(yypeek(0) == REL){
				yyget();
				if(yyget() != '('){
					yyerror("expected '('");
					skiptonl();
					out.len = 0;
					return;
				}
				e1 = parse_exp();
				if(yyget() != ')'){
					yyerror("expected ')'");
					skiptonl();
					out.len = 0;
					return;
				}
				out.len = 2;
				out.data[0] = 0x40000000L | (1L << 26) | (1L << 9);
				out.data[1] = mkreladdr(e1.value, 2);
				out.patch[0] = out.patch[1] = 0;
				return;
			}
			e1 = parse_exp();
			out.len = 2;
			out.data[0] = 0x40000000L | (1L << 9) | chkreladdr(1, &e1, 2, IO_REL);
			out.patch[0] = 0;
			return;
		}
		yyerror("expected DISCONNECT, RESELECT or SELECT after WAIT");
		skiptonl();
		out.len = 0;
		return;
	}
	if(t == DEFW){
		yyget();
		e1 = parse_exp();
		out.len = 1;
		out.data[0] = e1.value;
		out.patch[0] = (unsigned char)patchtype(e1.t);
		return;
	}
	yyerror("unknown opcode");
	skiptonl();
	out.len = 0;
}

static long
parse_set_cmd(void)
{
	int t = yyget();
	if(t == SET)
		return 3;
	return 4;
}

static long
parse_set_bit(void)
{
	int t = yyget();

	switch(t){
	case CARRY:
		return 0x400;
	case TARGET:
		return 0x200;
	case ACK:
		return 0x40;
	case ATN:
		return 0x8;
	}
	yyerror("expected set bit (carry/target/ack/atn)");
	return 0;
}

static long
parse_set_list(void)
{
	long v, w;
	int t;

	v = parse_set_bit();
	for(;;){
		t = yypeek(0);
		if(t == ','){
			yyget();
			w = parse_set_bit();
			v |= w;
			continue;
		}
		if(t == AND){
			yyget();
			w = parse_set_bit();
			v |= w;
			continue;
		}
		break;
	}
	return v;
}

static long
parse_jump_or_call(void)
{
	int t = yyget();

	if(t == JUMP)
		return 0x80000000L;
	return 0x88000000L;
}

static void
parse_with_or_when(void)
{
	int t = yyget();

	if(t != WITH && t != WHEN)
		yyerror("expected WITH or WHEN");
}

static long
parse_ptr_opt(void)
{
	if(yypeek(0) == PTR){
		yyget();
		return 1;
	}
	return 0;
}

static long
parse_atn_opt(void)
{
	if(yypeek(0) == ATN){
		yyget();
		return 1 << 24;
	}
	return 0;
}

static long
parse_phase(void)
{
	int t = yyget();

	switch(t){
	case DATA_OUT:
		return 0;
	case DATA_IN:
		return 1;
	case COMMAND:
		return 2;
	case STATUS:
		return 3;
	case RESERVED_OUT:
		return 4;
	case RESERVED_IN:
		return 5;
	case MESSAGE_OUT:
		return 6;
	case MESSAGE_IN:
		return 7;
	}
	yyerror("expected phase");
	return 0;
}

static struct expval
parse_byteexp(void)
{
	struct expval e = parse_exp();

	if(pass2 && (e.value < 0 || e.value > 255)){
		if(wflag)
			yywarn("conversion causes truncation");
		e.value = e.value & 0xff;
	}
	return e;
}

static long
parse_condsfbr(void)
{
	struct expval a, b;

	a = parse_byteexp();
	if(yypeek(0) == AND){
		yyget(); /* AND */
		if(yyget() != MASK){
			yyerror("expected MASK after AND");
			return a.value | COND_DATA;
		}
		b = parse_byteexp();
		return (b.value << 8) | a.value | COND_DATA;
	}
	return a.value | COND_DATA;
}

static long
parse_condphase(void)
{
	long p = parse_phase();

	return (p << 24) | COND_PHASE;
}

/*
 * .cond is the most branched nonterminal (17 alts).
 * Order matters: try longest discriminating prefixes first.
 * All alts start with ',' then IF/WHEN.
 */
static long
parse_cond_opt(void)
{
	long a, b;

	if(yypeek(0) != ',')
		return COND_TRUE;
	yyget(); /* ',' */
	if(yypeek(0) == IF){
		yyget();
		if(yypeek(0) == NOT){
			yyget();
			if(yypeek(0) == ATN){
				yyget();
				if(yypeek(0) == OR){
					yyget();
					b = parse_condsfbr();
					return b;
				}
				return 0;
			}
			/* NOT condphase [OR condsfbr] | NOT CARRY | NOT condsfbr */
			if(yypeek(0) == CARRY){
				yyget();
				return COND_CARRY;
			}
			/* try condsfbr vs condphase: both can start with
			 * byteexp (NUM/SYMBOL/'('/'-'/'~') vs phase keyword.
			 * Phase keywords are distinct, so check first.
			 */
			{
				int q = yypeek(0);
				if(q == DATA_OUT || q == DATA_IN || q == COMMAND ||
				   q == STATUS || q == RESERVED_OUT || q == RESERVED_IN ||
				   q == MESSAGE_OUT || q == MESSAGE_IN){
					a = parse_condphase();
					if(yypeek(0) == OR){
						yyget();
						b = parse_condsfbr();
						return a | b;
					}
					return a;
				}
			}
			b = parse_condsfbr();
			return b;
		}
		if(yypeek(0) == ATN){
			yyget();
			if(yypeek(0) == AND){
				yyget();
				b = parse_condsfbr();
				return b | COND_TRUE;
			}
			return COND_TRUE;
		}
		if(yypeek(0) == CARRY){
			yyget();
			return COND_CARRY | COND_TRUE;
		}
		{
			int q = yypeek(0);
			if(q == DATA_OUT || q == DATA_IN || q == COMMAND ||
			   q == STATUS || q == RESERVED_OUT || q == RESERVED_IN ||
			   q == MESSAGE_OUT || q == MESSAGE_IN){
				a = parse_condphase();
				if(yypeek(0) == AND){
					yyget();
					b = parse_condsfbr();
					return a | b | COND_TRUE;
				}
				return a | COND_TRUE;
			}
		}
		b = parse_condsfbr();
		if(yypeek(0) == AND){
			/* actually ', IF condsfbr' has no trailing AND;
			 * ', IF ATN AND condsfbr' and
			 * ', IF condphase AND condsfbr' handled above.
			 * If we get here with AND, it must be error.
			 */
			yyerror("unexpected AND in condition");
			skiptonl();
			return b | COND_TRUE;
		}
		return b | COND_TRUE;
	}
	if(yypeek(0) == WHEN){
		yyget();
		if(yypeek(0) == NOT){
			yyget();
			if(yypeek(0) == CARRY){
				yyget();
				return COND_CARRY | COND_WAIT;
			}
			{
				int q = yypeek(0);
				if(q == DATA_OUT || q == DATA_IN || q == COMMAND ||
				   q == STATUS || q == RESERVED_OUT || q == RESERVED_IN ||
				   q == MESSAGE_OUT || q == MESSAGE_IN){
					a = parse_condphase();
					if(yypeek(0) == OR){
						yyget();
						b = parse_condsfbr();
						return a | b | COND_WAIT;
					}
					return a | COND_WAIT;
				}
			}
			b = parse_condsfbr();
			return b | COND_WAIT;
		}
		if(yypeek(0) == CARRY){
			yyget();
			return COND_CARRY | COND_WAIT | COND_TRUE;
		}
		{
			int q = yypeek(0);
			if(q == DATA_OUT || q == DATA_IN || q == COMMAND ||
			   q == STATUS || q == RESERVED_OUT || q == RESERVED_IN ||
			   q == MESSAGE_OUT || q == MESSAGE_IN){
				a = parse_condphase();
				if(yypeek(0) == AND){
					yyget();
					b = parse_condsfbr();
					return a | b | COND_WAIT | COND_TRUE;
				}
				return a | COND_WAIT | COND_TRUE;
			}
		}
		b = parse_condsfbr();
		return b | COND_WAIT | COND_TRUE;
	}
	yyerror("expected IF or WHEN in condition");
	return COND_TRUE;
}

static long
parse_reg(void)
{
	int t = yyget();

	switch(t){
	case SCNTL0: return 0;
	case SCNTL1: return 1;
	case SCNTL2: return 2;
	case SCNTL3: return 3;
	case SCID: return 4;
	case SXFER: return 5;
	case SDID: return 6;
	case GPREG: return 7;
	case SOCL: return 9;
	case SSID: return 0xa;
	case SBCL: return 0xb;
	case DSTAT: return 0xc;
	case SSTAT0: return 0xd;
	case SSTAT1: return 0xe;
	case SSTAT2: return 0xf;
	case DSA0: return 0x10;
	case DSA1: return 0x11;
	case DSA2: return 0x12;
	case DSA3: return 0x13;
	case ISTAT: return 0x14;
	case CTEST0: return 0x18;
	case CTEST1: return 0x19;
	case CTEST2: return 0x1a;
	case CTEST3: return 0x1b;
	case TEMP: return 0x1c;
	case DFIFO: return 0x20;
	case CTEST4: return 0x21;
	case CTEST5: return 0x22;
	case CTEST6: return 0x23;
	case DBC: return 0x24;
	case DCMD: return 0x27;
	case DNAD: return 0x28;
	case DSP: return 0x2c;
	case DSPS: return 0x30;
	case SCRATCHA0: return 0x34;
	case SCRATCHA1: return 0x35;
	case SCRATCHA2: return 0x36;
	case SCRATCHA3: return 0x37;
	case DMODE: return 0x38;
	case DIEN: return 0x39;
	case DWT: return 0x3a;
	case DCNTL: return 0x3b;
	case ADDER: return 0x3c;
	case SIEN0: return 0x40;
	case SIEN1: return 0x41;
	case SIST0: return 0x42;
	case SIST1: return 0x43;
	case SLPAR: return 0x44;
	case MACNTL: return 0x46;
	case GPCNTL: return 0x47;
	case STIME0: return 0x48;
	case STIME1: return 0x49;
	case RESPID: return 0x4a;
	case STEST0: return 0x4c;
	case STEST1: return 0x4d;
	case STEST2: return 0x4e;
	case STEST3: return 0x4f;
	case SIDL: return 0x50;
	case SODL: return 0x54;
	case SBDL: return 0x58;
	case SCRATCHB0: return 0x5c;
	case SCRATCHB1: return 0x5d;
	case SCRATCHB2: return 0x5e;
	case SCRATCHB3: return 0x5f;
	case SCRATCHC0: return 0x60;
	case SCRATCHC1: return 0x61;
	case SCRATCHC2: return 0x62;
	case SCRATCHC3: return 0x63;
	}
	yyerror("expected register");
	return 0;
}

static long
parse_regA(void)
{
	if(yypeek(0) == SFBR){
		yyget();
		return 8;
	}
	return parse_reg();
}

static struct expval
parse_regexp(void)
{
	struct expval e;
	int t0;

	/* regexp: exp | regA. regA starts with a register keyword;
	 * exp starts with NUM/SYMBOL/'('/'-'/'~'. SFBR is ambiguous
	 * (both regA and ... actually exp cannot start with SFBR,
	 * so SFBR always means regA). For other reg keywords, same.
	 * So if peek is a register keyword, parse regA; else exp.
	 */
	t0 = yypeek(0);
	switch(t0){
	case SCNTL0: case SCNTL1: case SCNTL2: case SCNTL3:
	case SCID: case SXFER: case SDID: case GPREG: case SFBR:
	case SOCL: case SSID: case SBCL: case DSTAT:
	case SSTAT0: case SSTAT1: case SSTAT2:
	case DSA0: case DSA1: case DSA2: case DSA3:
	case ISTAT: case CTEST0: case CTEST1: case CTEST2: case CTEST3:
	case TEMP: case DFIFO: case CTEST4: case CTEST5: case CTEST6:
	case DBC: case DCMD: case DNAD: case DSP: case DSPS:
	case SCRATCHA0: case SCRATCHA1: case SCRATCHA2: case SCRATCHA3:
	case DMODE: case DIEN: case DWT: case DCNTL: case ADDER:
	case SIEN0: case SIEN1: case SIST0: case SIST1: case SLPAR:
	case MACNTL: case GPCNTL: case STIME0: case STIME1: case RESPID:
	case STEST0: case STEST1: case STEST2: case STEST3: case SIDL:
	case SODL: case SBDL:
	case SCRATCHB0: case SCRATCHB1: case SCRATCHB2: case SCRATCHB3:
	case SCRATCHC0: case SCRATCHC1: case SCRATCHC2: case SCRATCHC3:
		e.t = Reg;
		e.value = parse_regA();
		return e;
	}
	return parse_exp();
}

static struct expval
parse_exp(void)
{
	return parse_add();
}

static struct expval
parse_add(void)
{
	struct expval l, r;
	int t;

	l = parse_mul();
	for(;;){
		t = yypeek(0);
		if(t != '+' && t != '-')
			break;
		yyget();
		r = parse_mul();
		l = eval(l, r, (char)t);
	}
	return l;
}

static struct expval
parse_mul(void)
{
	struct expval l, r;
	int t;

	l = parse_unary();
	for(;;){
		t = yypeek(0);
		if(t != '*' && t != '/')
			break;
		yyget();
		r = parse_unary();
		l = eval(l, r, (char)t);

	}
	/* '^' is %right but unused; diagnose if present */
	if(yypeek(0) == '^'){
		yyerror("'^' not supported");
		yyget();
		r = parse_unary();
		l.t = Error;
		l.value = 0;
	}
	return l;
}

static struct expval
parse_unary(void)
{
	struct expval e;
	int t;

	t = yypeek(0);
	if(t == '-'){
		yyget();
		e = parse_unary();
		return eval(e, e, '_');
	}
	if(t == '~'){
		yyget();
		e = parse_unary();
		return eval(e, e, '~');
	}
	return parse_primary();
}

static struct expval
parse_primary(void)
{
	struct expval e;
	struct sym *s;
	int t;

	t = yyget();
	if(t == NUM){
		e.t = Const;
		e.value = yylval.n;
		return e;
	}
	if(t == SYMBOL){
		s = yylval.s;
		e.t = s->t;
		e.value = s->value;
		if(pass2 && s->t == Unknown){
			yyerror("Undefined symbol %s", s->name);
			s->t = Error;
			s->value = 0;
			e.t = Error;
			e.value = 0;
		}
		return e;
	}
	if(t == '('){
		e = parse_exp();
		if(yyget() != ')'){
			yyerror("expected ')'");
			e.t = Error;
			e.value = 0;
		}
		return e;
	}
	yyerror("expected number, symbol or '('");
	e.t = Error;
	e.value = 0;
	/* skip to sync to avoid cascading */
	return e;
}

/* ---- original C epilogue from na.y (lexer, main, helpers) ---- */

struct {
	char *name;
	int tok;
} toktab[] =
{
	{ "when", WHEN },
	{ "data_out", DATA_OUT },
	{ "data_in", DATA_IN },
	{ "msg_out", MESSAGE_OUT },
	{ "msg_in", MESSAGE_IN },
	{ "cmd", COMMAND },
	{ "command", COMMAND },
	{ "status", STATUS },
	{ "move", MOVE },
	{ "select", SELECT },
	{ "reselect", RESELECT },
	{ "disconnect", DISCONNECT },
	{ "wait", WAIT },
	{ "set", SET },
	{ "clear", CLEAR },
	{ "with", WITH },
	{ "atn", ATN },
	{ "fail", FAIL },
	{ "carry", CARRY },
	{ "target", TARGET },
	{ "ack", ACK },
	{ "scntl0", SCNTL0 },
	{ "scntl1", SCNTL1 },
	{ "scntl2", SCNTL2 },
	{ "scntl3", SCNTL3 },
	{ "scid", SCID },
	{ "sxfer", SXFER },
	{ "sdid", SDID },
	{ "gpreg", GPREG },
	{ "sfbr", SFBR },
	{ "socl", SOCL },
	{ "ssid", SSID },
	{ "sbcl", SBCL },
	{ "dstat", DSTAT },
	{ "sstat0", SSTAT0 },
	{ "sstat1", SSTAT1 },
	{ "sstat2", SSTAT2 },
	{ "dsa", DSA0 },
	{ "dsa0", DSA0 },
	{ "dsa1", DSA1 },
	{ "dsa2", DSA2 },
	{ "dsa3", DSA3 },
	{ "istat", ISTAT },
	{ "ctest0", CTEST0 },
	{ "ctest1", CTEST1 },
	{ "ctest2", CTEST2 },
	{ "ctest3", CTEST3 },
	{ "temp", TEMP },
	{ "dfifo", DFIFO },
	{ "ctest4", CTEST4 },
	{ "ctest5", CTEST5 },
	{ "ctest6", CTEST6 },
	{ "dbc", DBC },
	{ "dcmd", DCMD },
	{ "dnad", DNAD },
	{ "dsp", DSP },
	{ "dsps", DSPS },
	{ "scratcha", SCRATCHA0 },
	{ "scratcha0", SCRATCHA0 },
	{ "scratcha1", SCRATCHA1 },
	{ "scratcha2", SCRATCHA2 },
	{ "scratcha3", SCRATCHA3 },
	{ "dmode", DMODE },
	{ "dien", DIEN },
	{ "dwt", DWT },
	{ "dcntl", DCNTL },
	{ "adder", ADDER },
	{ "sien0", SIEN0 },
	{ "sien1", SIEN1 },
	{ "sist0", SIST0 },
	{ "sist1", SIST1 },
	{ "slpar", SLPAR },
	{ "macntl", MACNTL },
	{ "gpcntl", GPCNTL },
	{ "stime0", STIME0 },
	{ "stime1", STIME1 },
	{ "respid", RESPID },
	{ "stest0", STEST0 },
	{ "stest1", STEST1 },
	{ "stest2", STEST2 },
	{ "stest3", STEST3 },
	{ "sidl", SIDL },
	{ "sodl", SODL },
	{ "sbdl", SBDL },
	{ "scratchb", SCRATCHB0 },
	{ "scratchb0", SCRATCHB0 },
	{ "scratchb1", SCRATCHB1 },
	{ "scratchb2", SCRATCHB2 },
	{ "scratchb3", SCRATCHB3 },
	{ "scratchc", SCRATCHC0 },
	{ "scratchc0", SCRATCHC0 },
	{ "scratchc1", SCRATCHC1 },
	{ "scratchc2", SCRATCHC2 },
	{ "scratchc3", SCRATCHC3 },
	{ "add", ADD },
	{ "addc", ADDC },
	{ "and", AND },
	{ "or", OR },
	{ "xor", XOR },
	{ "shl", SHL },
	{ "shr", SHR },
	{ "jump", JUMP },
	{ "call", CALL },
	{ "return", RETURN },
	{ "int", INT },
	{ "intfly", INTFLY },
	{ "not", NOT },
	{ "absolute", ABSOLUTE },
	{ "mask", MASK },
	{ "if", IF },
	{ "rel", REL },
	{ "ptr", PTR },
	{ "table", TABLE },
	{ "from", FROM },
	{ "memory", MEMORY },
	{ "to", TO },
	{ "nop", NOP },
	{ "extern", EXTERN },
	{ "defw", DEFW },
};

#define TOKS (sizeof(toktab)/sizeof(toktab[0]))

int lc;
int ll;

void
yyrewind(void)
{
	rewind(in_f);
	ll = lc = 0;
	yyline = 0;
	dot = 0;
}

int
yygetc(void)
{
	if (lc == ll)
	{
	next:
		if (fgets(line, 500, in_f) == 0)
			return EOF;
		/* do nasty check for #line directives */
		if (strncmp(line, "#line", 5) == 0) {
			/* #line n "filename" */
			sscanf(line, "#line %d \"%[^\"]", &yyline, yyfilename);
			yyline--;
			goto next;
		}
		yyline++;
		ll = strlen(line);
		lc = 0;
	}
	return line[lc++];
}

void
yyungetc(void)
{
	if (lc <= 0)
		exits("ungetc");
	lc--;
}

int
yylex(void)
{
	char token[100];
	int tl = 0;
	int c;
	while ((c = yygetc()) != EOF && (c == ' ' || c == '\t'))
		;
	if (c == EOF)
		return 0;
	if (isalpha(c) || c == '_')
	{
		unsigned int x;
		do {
			token[tl++] = c;
		} while ((c = yygetc()) != EOF && (isalnum(c) || c == '_'));
		if (c == EOF)
			return 0;
		yyungetc();
		token[tl] = 0;
		for (x = 0; x < TOKS; x++)
			if (strcmp(toktab[x].name, token) == 0)
				return toktab[x].tok;
		/* must be a symbol */
		yylval.s = findsym(token);
		return SYMBOL;
	}
	else if (isdigit(c))
	{
		/* accept 0x<digits> or 0b<digits> 0<digits> or <digits> */
		int prefix = c == '0';
		unsigned long n = c - '0';
		int base = 10;
		for (;;)
		{
			c = yygetc();
			if (c == EOF)
				return 0;
			if (prefix)
			{
				prefix = 0;
				if (c == 'x') {
					base = 16;
					continue;
				}
				else if (c == 'b')
				{
					base = 2;
					continue;
				}
				else
					base = 8;
			}
			if (isdigit(c))
				c -= '0';
			else if (isalpha(c) && base > 10)
			{
				if (isupper(c))
					c = tolower(c);
				c = c - 'a' + 10;
			}
			else {
				yyungetc();
				yylval.n = n;
				return NUM;
			}
			if (c >= base)
				yyerror("illegal format number");
			n = n * base + c;
		}
	}
	else if (c == ';') {
		/* skip to end of line */
		while ((c = yygetc()) != EOF && c != '\n')
			;
		if (c != EOF)
			yyungetc();
		return COMMENT;
	}
	return c;
}

void
yyerror(char *s, ...)
{
	va_list ap;

	va_start(ap, s);
	fprintf(stderr, "%s: %d: ", yyfilename, yyline);
	vfprintf(stderr, s, ap);
	if (putc('\n', stderr) < 0)
		exits("io");
	errors++;
	va_end(ap);
}

void
yywarn(char *s, ...)
{
	va_list ap;

	va_start(ap, s);
	fprintf(stderr, "%s: %d: warning: ", yyfilename, yyline);
	vfprintf(stderr, s, ap);
	if (putc('\n', stderr) < 0)
		exits("io");
	warnings++;
	va_end(ap);
}

void
p2error(int line, char *s)
{
	USED(line);
	printf("/*\t%s */\n", s);
}

void
main(int argc, char *argv[])
{
	int a;
	for (a = 1; a < argc; a++)
	{
		if (argv[a][0] == '-')
			switch (argv[a][1]) {
			case 'D':
				/* #defines for cpp */
				if (ncppopts >= MAXCPPOPTS) {
					fprintf(stderr, "too many cpp options\n");
					exits("options");
				}
				cppopts[ncppopts++] = argv[a];
				break;
			default:
				fprintf(stderr, "unrecognised option %s\n",
				    argv[a]);
				exits("options");
			}
		else
			break;
	}
	if (a != argc - 1)
	{
		fprintf(stderr, "usage: na [options] file\n");
		exits("options");
	}
	if (access(argv[a], 4) < 0) {
		fprintf(stderr, "can't read %s\n", argv[a]);
		exits("");
	}
	in_f = tmpfile();
	preprocess(argv[a], in_f);
	rewind(in_f);
	strcpy(yyfilename, argv[a]);
	yyparse();
	if (errors)
		exits("pass1");
	pass2 = 1;
	printf("unsigned long na_script[] = {\n");
	yyrewind();
	yyclearla();
	yyparse();
	printf("};\n");
	printf("\n");
	printf("#define NA_SCRIPT_SIZE %d\n", dot / 4);
	printf("\n");
	fixup();
	exits(errors ? "pass2" : "");
}

void
preprocess(char *in, FILE *out)
{
	Waitmsg *w;
	char **argv;

	if (fork() == 0) {
		/* child */
		dup(fileno(out), 1);
		argv = (char **)malloc(sizeof(char *) * (ncppopts + 5));
		argv[0] = "cpp";
		memcpy(&argv[1], cppopts, sizeof(char *) * ncppopts);
		argv[ncppopts + 1] = "-+";
		argv[ncppopts + 2] = "-N";
		argv[ncppopts + 3] = in;
		argv[ncppopts + 4] = 0;
		exec("/bin/cpp", argv);
		fprintf(stderr, "failed to exec cpp (%R)\n");
		exits("exec");
	}
	w = wait();
	free(w);
}

struct sym *
findsym(char *name)
{
	struct sym *s;
	for (s = symlist; s; s = s->next)
		if (strcmp(name, s->name) == 0)
			return s;
	s = (struct sym *)malloc(sizeof(*s));
	s->name = strdup(name);
	s->t = Unknown;
	s->set = 0;
	s->next = symlist;
	symlist = s;
	return s;
}

static void
setsym(struct sym *s, Type t, long v)
{
	if (pass2) {
		if (t == Unknown || t == Error)
			yyerror("can't resolve symbol");
		else {
			s->t = t;
			s->value = v;
		}
	}
	else {
		if (s->set)
			yyerror("multiply defined symbol");
		s->set = 1;
		s->t = t;
		s->value = v;
	}
}

int
mk24bitssigned(long *l)
{
	if (*l < 0) {
		if ((*l & 0xff800000L) != 0xff800000L) {
			*l = 0;
			return 0;
		}
		else
			*l = (*l) & 0xffffffL;
	}
	else if (*l > 0xffffffL) {
		*l = 0;
		return 0;
	}
	return 1;
}

static Type addresult[5][5] = {
/*		Const	Addr	Table   Extern	Reg */
/* Const */	Const,	Addr,	Table,	Error,  Reg,
/* Addr */	Addr,	Error,	Error,	Error,  Error,
/* Table */	Table,	Error,	Error,	Error,  Error,
/* Extern */	Error,	Error,	Error,	Error,	Error,
/* Reg */	Reg,	Error,  Error,  Error,	Error,
};

static Type subresult[5][5] = {
/*		Const	Addr	Table   Extern	Reg */
/* Const */	Const,	Error,	Error,	Error,  Error,
/* Addr */	Addr,	Const,	Error,	Error,	Error,
/* Table */	Table,	Error,	Const,	Error,	Error,
/* Extern */	Error,	Error,	Error,	Const,	Error,
/* Reg */	Error,	Error,  Error,  Error,	Error,
};

static Type muldivresult[5][5] = {
/*		Const	Addr	Table   Extern */
/* Const */	Const,	Error,	Error,	Error,	Error,
/* Addr */	Error,	Error,	Error,	Error,	Error,
/* Table */	Error,	Error,	Error,	Error,	Error,
/* Extern */	Error,	Error,	Error,	Error,	Error,
/* Reg */	Error,	Error,	Error,	Error,	Error,
};

static Type negresult[] = {
/* Const */	Const,
/* Addr */	Error,
/* Table */	Error,
/* Extern */	Error,
/* Reg */	Error,
};

int
patchtype(Type t)
{
	switch (t) {
	case Addr:
		return 1;
	case Reg:
		return 2;
	case Extern:
		return 4;
	default:
		return 0;
	}
}

struct expval
eval(struct expval a, struct expval b, char op)
{
	struct expval c;
	
	if (a.t == Unknown || b.t == Unknown) {
		c.t = Unknown;
		c.value = 0;
	}
	else if (a.t == Error || b.t == Error) {
		c.t = Error;
		c.value = 0;
	}
	else {
		switch (op) {
		case '+':
			c.t = addresult[a.t][b.t];
			break;
		case '-':
			c.t = subresult[a.t][b.t];
			break;
		case '*':
		case '/':
			c.t = muldivresult[a.t][b.t];
			break;
		case '_':
		case '~':
			c.t = negresult[a.t];
			break;
		default:
			c.t = Error;
			break;
		}
		if (c.t == Error) {
			if (pass2)
				yyerror("type clash in evaluation");
			c.value = 0;
		}
		else {
			switch (op) {
			case '+':
				c.value = a.value + b.value;
				break;
			case '-':
				c.value = a.value - b.value;
				break;
			case '*':
				c.value = a.value * b.value;
				break;
			case '/':
				c.value = a.value / b.value;
				break;
			case '_':
				c.value = -a.value;
				break;
			case '~':
				c.value = ~a.value;
				break;
			}
		}
	}
	return c;
}

void
regmove(unsigned char src_reg, unsigned char op,
    unsigned char dst_reg, struct expval *imm)
{
	unsigned char func, reg;
	int immdata;
	out.len = 2;
	if (src_reg == 8) {
		func = 5;
		reg = dst_reg;
	}
	else if (dst_reg == 8) {
		func = 6;
		reg = src_reg;
	}
	else {
		if (pass2 && src_reg != dst_reg)
			yyerror("Registers must be the same");
		func = 7;
		reg = src_reg;
	}
	immdata = imm ? (imm->value & 0xff) : 0;
	out.data[0] = 0x40000000L
	    | ((long)func << 27)
	    | ((long)op << 24)
	    | ((long)reg << 16)
	    | ((long)(immdata) << 8);
	out.data[1] = 0;
	out.patch[0] = (imm && imm->t == Extern) ? 3 : 0;
	out.patch[1] = 0;
}

long
mkreladdr(long addr, int len)
{
	long rel;
	rel = addr - (dot + 4 * len);
	mk24bitssigned(&rel);
	return rel;
}

long
chkreladdr(int d, struct expval *e, int len, long relrv)
{
	if (e->t == Addr) {
		out.data[d] = mkreladdr(e->value, len);
		out.patch[d] = 0;
		return relrv;
	} else {
		out.data[d] = e->value;
		out.patch[d] = (unsigned char)patchtype(e->t);
		return 0;
	}
}

void
fixup(void)
{
	struct sym *s;
	int p;
	printf("struct na_patch na_patches[] = {\n");
	for (p = 0; p < patches; p++) {
		printf("\t{ 0x%.4x, %d }, /* %.8lx */\n",
		    patch[p].lwoff, patch[p].type, patch[p].lwoff * 4L);
	}
	if (patches == 0) {
		printf("\t{ 0, 0 },\n");
	}
	printf("};\n");
	printf("#define NA_PATCHES %d\n", patches);
	printf("\n");
	if (externs) {
		printf("enum na_external {\n");
		for (p = 0; p < (int)externs; p++) {
			printf("\tX_%s,\n", externp[p]->name);
		}
		printf("};\n");
	}
	/* dump all labels (symbols of type Addr) as E_<Name> */
	for (s = symlist; s; s = s->next)
		if (s->t == Addr)
			break;
	if (s) {
		printf("\nenum {\n");
		while (s) {
			if (s->t == Addr)
				printf("\tE_%s = %ld,\n", s->name, s->value);
			s = s->next;
		}
		printf("};\n");
	}
	/* dump all Consts as #define A_<Name> value */
	for (s = symlist; s; s = s->next)
		if (s->t == Const)
			printf("#define A_%s %ld\n", s->name, s->value);
}
