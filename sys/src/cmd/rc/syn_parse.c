#include "rc.h"
#include "exec.h"
#include "io.h"
#include "fns.h"

/*
 * Hand-written recursive-descent replacement for syn.y (LALR).
 *
 * Precedence (low -> high), from syn.y %left declarations:
 *   level 1: IF WHILE FOR SWITCH ')' NOT
 *   level 2: ANDAND OROR
 *   level 3: BANG SUBSHELL        (also %prec BANG on redir/assign rules)
 *   level 4: PIPE
 *   level 5: '^'
 *   level 6: '$' COUNT '"'
 *   level 7: SUB
 *
 * Structural: cmd1 handles ANDAND/OROR (level 2),
 *             cmd2 handles BANG/SUBSHELL/redir/assign (level 3),
 *             cmd3 handles PIPE (level 4),
 *             primary handles structural commands (level 1).
 *
 * In this layout, BANG(3) < PIPE(4): PIPE is tighter, so the cmd
 * operand of BANG/redir/assign is parsed at the PIPE level:
 *     ! cmd1 | cmd2   =>  !(cmd1 | cmd2)
 *
 * ANDAND(2) < BANG(3): BANG reduces before ANDAND:
 *     !cmd && cmd2    =>  (!cmd) && cmd2
 *
 * PIPE(4) > ANDAND(2): PIPE is tighter, so a|PIPE b shifts before
 * ANDAND reduces:
 *     a && b | c      =>  a && (b | c)
 */

static int lookahead;
static int have_look;

#define YYEOF EOF

extern int yylex(void);
/* extern void yyerror(char *);  -- provided by subr.c */

/* the lexer sets yylval.tree before returning WORD/REDIR/DUP/etc. */
YYSTYPE yylval;

static int  peek(void);
static void advance(void);

static Node parse_cmd(void);
static Node parse_andor(void);
static Node parse_pipe(void);
static Node parse_bang(void);
static Node parse_primary(void);
static Node parse_body(void);
static Node parse_brace(void);
static Node parse_simple(void);
static Node parse_first(void);
static Node parse_word(void);
static Node parse_comword(void);
static Node parse_words(void);
static Node parse_epilog(void);
static Node parse_redir_word(void);
static Node parse_line(void);

static int
peek(void)
{
	if (!have_look) {
		lookahead = yylex();
		have_look = 1;
	}
	return lookahead;
}

static void
advance(void)
{
	if (!have_look)
		lookahead = yylex();
	have_look = 0;
}

/*
 * entry point:  rc: (empty) { return 1; }
 *                 | line '\n'  { return !compile($1); }
 *
 * line: cmd
 *     | cmdsa line  { $$ = tree2(';', $1, $2); }
 *
 * cmdsa: cmd ';'  { $$ = tree1(';', $1); }
 *     | cmd '&'  { $$ = tree1('&', $1); }
 *
 * In yyparse, parse_cmd() parses a full cmd (including binary operators).
 * If the next token is ';' or '&', wrap the cmd and continue parsing the
 * rest of the line (right-recursive). Otherwise, the line is just the
 * single cmd.
 */
int
yyparse(void)
{
	Node t;

	have_look = 1;
	lookahead = yylex();
	for (;;) {
		if (lookahead == YYEOF)
			break;
		/* skip leading newlines */
		if (lookahead == '\n') {
			advance();
			continue;
		}
		t = parse_line();
		if (t)
			compile(t);
		if (lookahead == '\n')
			advance();
	}
	return 0;
}

/*
 * line: cmd | cmdsa line
 */
static Node
parse_line(void)
{
	Node t;

	t = parse_cmd();
	while (lookahead == ';' || lookahead == '&') {
		if (lookahead == '&') {
			advance();
			t = tree2(';', tree1('&', t), parse_line());
		} else {
			advance();
			t = tree2(';', tree1(';', t), parse_line());
		}
	}
	return t;
}

/*
 * Precedence-cascade for cmd binary operators.
 *
 *   parse_andor (level 2: ANDAND, OROR)
 *     parse_pipe (level 4: PIPE)
 *       parse_bang (level 3: prefix BANG/SUBSHELL, redir/assign %prec BANG)
 *         parse_primary (level 1: structural + simple)
 *
 * Level numbers: lower = binds looser.
 */

/* level 2: ANDAND, OROR (left-assoc, loosest cmd binary ops) */
/* parse_cmd is the entry point for a single command (with binary operators) */
static Node
parse_cmd(void)
{
	return parse_andor();
}

static Node
parse_andor(void)
{
	Node left, right;
	int op;

	left = parse_pipe();
	while (lookahead == ANDAND || lookahead == OROR) {
		op = lookahead;
		advance();
		right = parse_pipe();
		left = tree2(op, left, right);
	}
	return left;
}

/* level 4: PIPE (left-assoc, tighter than ANDAND) */
static Node
parse_pipe(void)
{
	Node left, right;

	left = parse_bang();
	while (lookahead == PIPE) {
		advance();
		right = parse_bang();
		left = mung2(PIPE, left, right);
	}
	return left;
}

/*
 * level 3: BANG, SUBSHELL (prefix unary), redir cmd %prec BANG,
 *          assign cmd %prec BANG.
 *
 * BANG(3) < PIPE(4): the operand of BANG is parsed at PIPE level.
 *   ! a | b  =>  !(a | b)
 * BANG(3) > ANDAND(2): BANG(cmd) reduces before ANDAND.
 *   !a && b  =>  (!a) && b
 *
 * For redir/assign %prec BANG: same level 3.
 *   >a cmd | b  =>  >a(cmd | b)   (PIPE tighter, part of cmd operand)
 *   >a cmd && b =>  (>a cmd) && b (redir reduces before ANDAND)
 */
static Node
parse_bang(void)
{
	Node r;

	/* BANG cmd  /  SUBSHELL cmd */
	if (lookahead == BANG || lookahead == SUBSHELL) {
		int op = lookahead;
		advance();
		return mung1(op, parse_pipe());
	}

	/* redir cmd  %prec BANG
	 * REDIR word  or  DUP
	 */
	if (lookahead == REDIR || lookahead == DUP) {
		r = parse_redir_word();
		return mung2(r->type, r->child[0], parse_pipe());
	}

	/* assign cmd  %prec BANG
	 * assign: first '=' word
	 * Need to peek: parse first then check for '='.
	 */
	if (lookahead == WORD || lookahead == '\'' || lookahead == '$'
	    || lookahead == COUNT || lookahead == '`' || lookahead == '"') {
		Node first_t, assign_t, w;
		/* save position, parse first, check '=' */
		first_t = parse_first();
		if (lookahead == '=') {
			advance();
			w = parse_word();
			/* assign: first '=' word { $$ = tree2('=', $1, $3); } */
			assign_t = tree2('=', first_t, w);
			/* cmd: assign cmd %prec BANG { $$ = mung3($1, $1->child[0], $1->child[1], $2); } */
			return mung3(assign_t, assign_t->child[0], assign_t->child[1],
			    parse_pipe());
		}
		/* not an assign: treat as simple */
		return simplemung(first_t);
	}

	/* default: structural primary */
	return parse_primary();
}

/*
 * structural commands (level 1):
 *   IF paren skipnl() cmd
 *   IF NOT skipnl() cmd
 *   FOR '(' word IN words ')' skipnl() cmd
 *   FOR '(' word ')' skipnl() cmd
 *   WHILE paren skipnl() cmd
 *   SWITCH word skipnl() brace
 *   FN words brace
 *   FN words
 *   TWIDDLE word words
 *   brace epilog
 *   (empty)
 */
static Node
parse_primary(void)
{
	Node w, b;

	switch (lookahead) {
	case IF:
		advance();
		if (lookahead != '(')
			yyerror("expected '(' after if");
		else
			advance();
		w = parse_body();	/* 'if (cond)' -- cond is a body */
		if (lookahead != ')')
			yyerror("expected ')' after if condition");
		else
			advance();
		if (lookahead == NOT) {
			advance();
			skipnl();
			return mung1(NOT, parse_pipe());
		}
		skipnl();
		return mung2(IF, w, parse_pipe());

	case WHILE:
		advance();
		if (lookahead != '(')
			yyerror("expected '(' after while");
		else
			advance();
		w = parse_body();	/* 'while (cond)' -- cond is a body */
		if (lookahead != ')')
			yyerror("expected ')' after while condition");
		else
			advance();
		skipnl();
		return mung2(WHILE, w, parse_pipe());

	case FOR:
		advance();
		if (lookahead != '(')
			yyerror("expected '(' after for");
		else
			advance();
		w = parse_word();
		if (lookahead == IN) {
			advance();
			b = parse_words();
		} else {
			b = (Node)0;
		}
		if (lookahead != ')')
			yyerror("expected ')' in for");
		else
			advance();
		skipnl();
		if (b)
			return mung3(FOR, w, b, parse_pipe());
		else
			return mung3(FOR, w, (Node)0, parse_pipe());

	case SWITCH:
		advance();
		w = parse_word();
		skipnl();
		if (lookahead != '{')
			yyerror("expected '{' after switch");
		return tree2(SWITCH, w, parse_brace());

	case FN:
		advance();
		w = parse_words();
		if (lookahead == '{') {
			b = parse_brace();
			return tree2(FN, w, b);
		}
		return tree1(FN, w);

	case TWIDDLE:
		advance();
		w = parse_word();
		b = parse_words();
		return mung2(TWIDDLE, w, b);

	case '{':
		b = parse_brace();
		b = epimung(b, parse_epilog());
		return b;

	case ';':
	case '&':
	case '\n':
	case ')':
	case '}':
	case EOF:
	case YYEOF:
		return (Node)0;

	default:
		/* simple command */
		return simplemung(parse_simple());
	}
}

/*
 * body: cmd
 *     | cmdsan body  { $$ = tree2(';', $1, $2); }
 *
 * cmdsan: cmdsa
 *       | cmd '\n'
 *
 * cmdsa: cmd ';'  { $$ = tree1(';', $1); }
 *      | cmd '&'  { $$ = tree1('&', $1); }
 */
static Node
parse_body(void)
{
	Node t;

	t = parse_cmd();

	if (lookahead == ';') {
		advance();
		return tree2(';', tree1(';', t), parse_body());
	}
	if (lookahead == '&') {
		advance();
		return tree2(';', tree1('&', t), parse_body());
	}
	if (lookahead == '\n') {
		advance();
		/* cmd '\n' is a cmdsan; wrap rest in ';' */
		if (lookahead == ';' || lookahead == '&' ||
		    lookahead == '\n' || lookahead == EOF)
			return t;
		return tree2(';', t, parse_body());
	}
	/* no cmdsan: body is just cmd */
	return t;
}

/*
 * brace: '{' body '}'  { $$ = tree1(BRACE, $2); }
 */
static Node
parse_brace(void)
{
	Node t;

	if (lookahead != '{')
		yyerror("expected '{'");
	advance();
	t = parse_body();
	if (lookahead != '}')
		yyerror("expected '}'");
	advance();
	return tree1(BRACE, t);
}

/*
 * paren: '(' body ')'  { $$ = tree1(PCMD, $2); }
 *
 * Note: in the grammar, IF/WHILE use 'paren' which is '(' body ')'.
 * But '(' is also used in FOR '(' and in comword '$' '(' ')' and '( words ')'.
 * The IF/WHILE context consumes '(' ... ')' as paren (body).
 */

/*
 * epilog:       { $$ = 0; }
 *           | redir epilog { $$ = mung2($1, $1->child[0], $2); }
 */
static Node
parse_epilog(void)
{
	Node r;

	if (lookahead == REDIR || lookahead == DUP) {
		r = parse_redir_word();
		return mung2(r->type, r->child[0], parse_epilog());
	}
	return (Node)0;
}

/*
 * redir (as used in redir cmd and epilog):
 *	REDIR word  { $$ = mung1($1, $1->rtype==HERE?heredoc($2):$2); }
 *	|	DUP
 *
 * Note: the lexer returns a tree node for REDIR/DUP tokens via yylval.tree.
 * The tree node has ->type==REDIR or DUP, and ->rtype set appropriately.
 * For REDIR, yylval.tree is the redir token itself.
 */
static Node
parse_redir_word(void)
{
	Node t, w;
	int op = lookahead;

	if (op != REDIR && op != DUP) {
		yyerror("expected redir");
		advance();
		return (Node)0;
	}

	/* get the tree node from yylval before advancing */
	t = yylval.tree;
	advance();

	if (op == REDIR) {
		w = parse_word();
		if (t->rtype == HERE)
			return mung1(REDIR, heredoc(w));
		return mung1(REDIR, w);
	}
	/* DUP */
	return tree1(DUP, (Node)0);
}

/*
 * simple: first
 *       | simple word   { $$ = tree2(ARGLIST, $1, $2); }
 *       | simple redir  { $$ = tree2(ARGLIST, $1, $2); }
 */
static Node
parse_simple(void)
{
	Node t, arg;

	t = parse_first();
	while (lookahead == WORD || lookahead == '\''
	    || lookahead == '$' || lookahead == COUNT
	    || lookahead == '"' || lookahead == '`' || lookahead == '{') {
		arg = parse_word();
		t = tree2(ARGLIST, t, arg);
	}
	while (lookahead == REDIR || lookahead == DUP) {
		arg = parse_redir_word();
		t = tree2(ARGLIST, t, arg);
	}
	return t;
}

/*
 * first: comword
 *      | first '^' word  { $$ = tree2('^', $1, $3); }
 */
static Node
parse_first(void)
{
	Node t, w;

	t = parse_comword();
	while (lookahead == '^') {
		advance();
		w = parse_word();
		t = tree2('^', t, w);
	}
	return t;
}

/*
 * keyword: FOR | IN | WHILE | IF | NOT | TWIDDLE | BANG | SUBSHELL | SWITCH | FN
 */
static Node
parse_keyword(void)
{
	int op = lookahead;
	advance();
	return tree1(op, (Node)0);
}

/*
 * words:          { $$ = (tree*)0; }
 *       | words word { $$ = tree2(WORDS, $1, $2); }
 */
static Node
parse_words(void)
{
	Node t = (Node)0;
	Node w;

	for (;;) {
		if (lookahead == ')' || lookahead == '{'
		    || lookahead == '\n' || lookahead == EOF
		    || lookahead == YYEOF || lookahead == ANDAND
		    || lookahead == OROR || lookahead == PIPE
		    || lookahead == ';' || lookahead == '&'
		    || lookahead == '=' || lookahead == '^')
			break;
		w = parse_word();
		if (w == (Node)0)
			break;
		t = tree2(WORDS, t, w);
	}
	return t;
}

/*
 * word: keyword         { lastword=1; $1->type=WORD; }
 *     | comword
 *     | word '^' word  { $$ = tree2('^', $1, $3); }
 */
static Node
parse_word(void)
{
	Node t, w;

	if (lookahead == FOR || lookahead == IN || lookahead == WHILE
	    || lookahead == IF || lookahead == NOT || lookahead == TWIDDLE
	    || lookahead == BANG || lookahead == SUBSHELL
	    || lookahead == SWITCH || lookahead == FN) {
		lastword = 1;
		t = parse_keyword();
		t->type = WORD;
	} else {
		t = parse_comword();
	}

	while (lookahead == '^') {
		advance();
		w = parse_word();
		t = tree2('^', t, w);
	}
	return t;
}

/*
 * comword: '$' word                  { $$ = tree1('$', $2); }
 *        | '$' word SUB words ')'    { $$ = tree2(SUB, $2, $4); }
 *        | '"' word                  { $$ = tree1('"', $2); }
 *        | COUNT word                { $$ = tree1(COUNT, $2); }
 *        | WORD
 *        | '`' brace                 { $$ = tree2('`', 0, $2); }
 *        | '`' word brace            { $$ = tree2('`', $2, $3); }
 *        | '(' words ')'              { $$ = tree1(PAREN, $2); }
 *        | REDIR brace                { $$ = mung1($1, $2); $$->type=PIPEFD; }
 */
static Node
parse_comword(void)
{
	Node t, w;

	switch (lookahead) {
	case '$':
		advance();
		if (lookahead == SUB) {
			advance();
			w = parse_word();
			if (lookahead != ')')
				yyerror("expected ')' in $");
			advance();
			return tree2(SUB, w, parse_words());
		}
		return tree1('$', parse_word());

	case '"':
		advance();
		return tree1('"', parse_word());

	case COUNT:
		advance();
		return tree1(COUNT, parse_word());

	case '`':
		advance();
		if (lookahead == '{' || lookahead == WORD || lookahead == '\''
		    || lookahead == '$' || lookahead == COUNT
		    || lookahead == '"' || lookahead == FOR
		    || lookahead == IN || lookahead == WHILE || lookahead == IF
		    || lookahead == NOT || lookahead == TWIDDLE
		    || lookahead == BANG || lookahead == SUBSHELL
		    || lookahead == SWITCH || lookahead == FN) {
			w = parse_word();
			if (lookahead == '{')
				return tree2('`', w, parse_brace());
			return w;
		}
		return tree2('`', (Node)0, parse_brace());

	case '(':
		advance();
		w = parse_words();
		if (lookahead != ')')
			yyerror("expected ')' in comword");
		advance();
		return tree1(PAREN, w);

	case REDIR:
		advance();
		t = parse_brace();
		t = mung1(REDIR, t);
		t->type = PIPEFD;
		return t;

	case DUP:
		/* DUP in comword context is unusual; treat as error */
		yyerror("unexpected DUP");
		advance();
		return (Node)0;

	case WORD:
	default:
		t = yylval.tree;
		advance();
		return t;
	}
}
