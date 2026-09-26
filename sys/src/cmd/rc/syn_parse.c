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
 *
 * Structural cmds (level 1, loosest) take a full cmd operand:
 *     if(a) b && c    =>  if(a) (b && c)
 * BANG/redir/assign (level 3) take a PIPE-level operand:
 *     ! a | b         =>  !(a | b)
 *     ! a && b        =>  (!a) && b
 */

typedef tree *Node;

static int lookahead;
/* set by syn_error(); unwinds parsing so one bad line cannot merge
 * with the next (yacc abandons the rule; yyerror already skipped
 * input chars to the next line, so just stop consuming tokens). */
static int parse_failed;

extern int yylex(void);
/* yyerror(char *) provided by subr.c; it skips input to '\n'/EOF */
extern int lastdol;
/* lastword declared in rc.h */

YYSTYPE yylval;

static void syn_advance(void);
static void syn_error(char*);
static void skipnl_tok(void);
static int is_wordstart(int);

static Node parse_cmd(void);
static Node parse_andor(void);
static Node parse_pipe(void);
static Node parse_bang(void);
static Node parse_primary(void);
static Node parse_body(void);
static Node parse_brace(void);
static Node parse_simple(void);
static Node parse_simple_rest(Node);
static Node parse_first(void);
static Node parse_word(void);
static Node parse_word_base(void);
static Node parse_comword(void);
static Node parse_words(void);
static Node parse_epilog(void);
static Node parse_redir_word(void);
static Node parse_line(void);

static void
syn_advance(void)
{
	/* buffer next token in lookahead; yylval corresponds to it.
	 * Callers needing the current token's tree must save
	 * yylval.tree BEFORE calling. */
	lookahead = yylex();
}

static void
syn_error(char *msg)
{
	/* yyerror() consumes chars to end of line at char level.
	 * Do NOT lex here: the next yyparse() call will lex fresh
	 * from the start of the next line. Lexing now would buffer
	 * the next line's first token in lookahead, only for the
	 * next yyparse() to overwrite it (losing that token). */
	yyerror(msg);
	parse_failed = 1;
}

/*
 * Token-level equivalent of lex.c skipnl(): skip buffered newlines.
 * Must clear lastword/lastdol before lexing past a newline, otherwise
 * yylex() would insert a bogus '^'/SUB (e.g. "a\n(b)" must not become
 * "a SUB ...").  yacc's {skipnl();} runs before the next yylex(), so
 * the override belongs here.
 */
static void
skipnl_tok(void)
{
	while(!parse_failed && lookahead == '\n'){
		lastword = 0;
		lastdol = 0;
		syn_advance();
	}
}

static int
is_wordstart(int t)
{
	switch(t){
	case WORD:
	case '$':
	case '"':
	case COUNT:
	case '`':
	case '(':
	case REDIR:
	case FOR:
	case IN:
	case WHILE:
	case IF:
	case NOT:
	case TWIDDLE:
	case BANG:
	case SUBSHELL:
	case SWITCH:
	case FN:
		return 1;
	default:
		return 0;
	}
}

/*
 * entry point, faithful to syn.y:
 *	rc: (empty) { return 1; }
 *	  | line '\n' { return !compile($1); }
 *
 * Exactly one line per call.  return 1 means EOF (or compile error,
 * so Xrdcmds takes the no-execute path); return 0 means codebuf holds
 * freshly compiled code for Xrdcmds to start().
 *
 * line: cmd
 *     | cmdsa line  { $$ = tree2(';', $1, $2); }
 * cmdsa: cmd ';'  { $$ = tree1(';', $1); }
 *      | cmd '&'  { $$ = tree1('&', $1); }
 */
int
yyparse(void)
{
	Node t;

	parse_failed = 0;
	lookahead = yylex();
	if(lookahead == EOF)
		return 1;
	t = parse_line();
	if(parse_failed)
		return 1;
	/*
	 * line '\n': the terminating '\n' token's chars are already
	 * consumed by yylex, so there is nothing to advance past --
	 * prefetching here would drop the next line's first token.
	 * Accept EOF as terminator too.
	 */
	if(lookahead != '\n' && lookahead != EOF)
		syn_error("expected newline");
	if(parse_failed)
		return 1;
	return !compile(t);
}

/*
 * line: cmd | cmdsa line
 */
static Node
parse_line(void)
{
	Node t;

	if(parse_failed)
		return (Node)0;
	t = parse_cmd();
	while(!parse_failed && (lookahead == ';' || lookahead == '&')){
		if(lookahead == '&'){
			syn_advance();
			t = tree2(';', tree1('&', t), parse_line());
		} else {
			syn_advance();
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
 */

/* parse_cmd is the entry point for a single command (with binary operators) */
static Node
parse_cmd(void)
{
	return parse_andor();
}

/* level 2: ANDAND, OROR (left-assoc, loosest cmd binary ops) */
static Node
parse_andor(void)
{
	Node left, right;
	int op;

	left = parse_pipe();
	while(!parse_failed && (lookahead == ANDAND || lookahead == OROR)){
		op = lookahead;
		syn_advance();
		right = parse_pipe();
		left = tree2(op, left, right);
	}
	return left;
}

/* level 4: PIPE (left-assoc, tighter than ANDAND) */
static Node
parse_pipe(void)
{
	Node left, right, opnode;

	left = parse_bang();
	while(!parse_failed && lookahead == PIPE){
		opnode = yylval.tree;
		syn_advance();
		right = parse_bang();
		left = mung2(opnode, left, right);
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

	if(parse_failed)
		return (Node)0;
	/* BANG cmd  /  SUBSHELL cmd */
	if(lookahead == BANG || lookahead == SUBSHELL){
		Node n = yylval.tree;
		syn_advance();
		return mung1(n, parse_pipe());
	}

	/*
	 * redir cmd  %prec BANG
	 * REDIR word  or  DUP
	 * Ambiguity: REDIR '{' starts a comword (PIPEFD), not a redir.
	 * yacc shifts REDIR then decides on the next token, so do the
	 * same: consume REDIR, then branch.
	 */
	if(lookahead == REDIR || lookahead == DUP){
		if(lookahead == DUP){
			r = parse_redir_word();
			return mung2(r, r->child[0], parse_pipe());
		}
		/* REDIR: peek next token */
		r = yylval.tree;
		syn_advance();
		if(lookahead == '{'){
			Node cw, fst;
			cw = mung1(r, parse_brace());
			cw->type = PIPEFD;
			/* comword -> first ('^' word)*, then simple tail.
			 * Left-assoc per %left '^': RHS is base (like parse_first). */
			fst = cw;
			while(!parse_failed && lookahead == '^'){
				Node w;
				syn_advance();
				w = parse_word_base();
				fst = tree2('^', fst, w);
			}
			return simplemung(parse_simple_rest(fst));
		}
		{
			Node w, red;
			w = parse_word();
			if(r->rtype == HERE)
				red = mung1(r, heredoc(w));
			else
				red = mung1(r, w);
			return mung2(red, red->child[0], parse_pipe());
		}
	}

	/*
	 * assign cmd  %prec BANG
	 * assign: first '=' word
	 * Only attempt when lookahead can start a comword (first is
	 * comword-based, not keyword-based).  Keywords fall through to
	 * parse_primary (structural/word handling).
	 */
	if(lookahead == WORD || lookahead == '$' || lookahead == '"'
	    || lookahead == COUNT || lookahead == '`' || lookahead == '('
	    || lookahead == REDIR){
		Node first_t, assign_t, w;
		first_t = parse_first();
		if(lookahead == '='){
			syn_advance();
			w = parse_word();
			/* assign: first '=' word { $$ = tree2('=', $1, $3); } */
			assign_t = tree2('=', first_t, w);
			/* cmd: assign cmd %prec BANG */
			return mung3(assign_t, assign_t->child[0], assign_t->child[1],
			    parse_pipe());
		}
		/* not an assign: continue as simple from first_t */
		return simplemung(parse_simple_rest(first_t));
	}

	/* default: structural primary */
	return parse_primary();
}

/*
 * structural commands (level 1, loosest: operand is a full cmd):
 *   IF paren skipnl() cmd
 *   IF NOT skipnl() cmd
 *   FOR '(' word IN words ')' skipnl() cmd   (saw_in: nil words => PAREN())
 *   FOR '(' word ')' skipnl() cmd            (no IN: implicit $* loop, c1=0)
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
	Node w, b, n, n2;

	if(parse_failed)
		return (Node)0;
	switch(lookahead){
	case IF:
		n = yylval.tree;
		syn_advance();
		/* syn.y has two productions:
		 *   IF paren skipnl cmd
		 *   IF NOT skipnl cmd   (no parens, e.g. "if not { ... }")
		 * Check NOT first. */
		if(lookahead == NOT){
			n2 = yylval.tree;
			syn_advance();
			skipnl_tok();
			return mung1(n2, parse_cmd());
		}
		if(lookahead != '(')
			syn_error("expected '(' after if");
		else
			syn_advance();
		w = tree1(PCMD, parse_body());	/* paren: '(' body ')' */
		if(lookahead != ')')
			syn_error("expected ')' after if condition");
		else
			syn_advance();
		skipnl_tok();
		return mung2(n, w, parse_cmd());

	case WHILE:
		n = yylval.tree;
		syn_advance();
		if(lookahead != '(')
			syn_error("expected '(' after while");
		else
			syn_advance();
		w = tree1(PCMD, parse_body());	/* paren: '(' body ')' */
		if(lookahead != ')')
			syn_error("expected ')' after while condition");
		else
			syn_advance();
		skipnl_tok();
		return mung2(n, w, parse_cmd());

	case FOR:
		n = yylval.tree;
		syn_advance();
		if(lookahead != '(')
			syn_error("expected '(' after for");
		else
			syn_advance();
		w = parse_word();
		if(lookahead == IN){
			syn_advance();
			b = parse_words();
			if(lookahead != ')')
				syn_error("expected ')' in for");
			else
				syn_advance();
			skipnl_tok();
			/*
			 * saw_in: distinguish "for(i in )" (empty set)
			 * from "for(i)" (implicit $* loop).  nil words
			 * with IN present becomes "()".
			 */
			if(b)
				return mung3(n, w, b, parse_cmd());
			return mung3(n, w, tree1(PAREN, b), parse_cmd());
		}
		if(lookahead != ')')
			syn_error("expected ')' in for");
		else
			syn_advance();
		skipnl_tok();
		return mung3(n, w, (Node)0, parse_cmd());

	case SWITCH:
		syn_advance();
		w = parse_word();
		skipnl_tok();
		if(lookahead != '{')
			syn_error("expected '{' after switch");
		return tree2(SWITCH, w, parse_brace());

	case FN:
		syn_advance();
		w = parse_words();
		if(lookahead == '{'){
			b = parse_brace();
			return tree2(FN, w, b);
		}
		return tree1(FN, w);

	case TWIDDLE:
		n = yylval.tree;
		syn_advance();
		w = parse_word();
		b = parse_words();
		return mung2(n, w, b);

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

	if(parse_failed)
		return (Node)0;
	t = parse_cmd();

	if(lookahead == ';'){
		syn_advance();
		return tree2(';', tree1(';', t), parse_body());
	}
	if(lookahead == '&'){
		syn_advance();
		return tree2(';', tree1('&', t), parse_body());
	}
	if(lookahead == '\n'){
		syn_advance();
		/* cmdsan: cmd '\n'; the body always continues, exactly
		 * like yacc's cmdsan body rule.  In particular blank and
		 * comment-only lines (extra '\n') must be consumed by
		 * recursion here -- stopping early would strand input
		 * and make the enclosing brace/paren fail later with
		 * "expected '}'".  EOF terminates the body. */
		if(lookahead == EOF)
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

	if(parse_failed)
		return tree1(BRACE, (Node)0);
	if(lookahead != '{')
		syn_error("expected '{'");
	else
		syn_advance();
	t = parse_body();
	if(lookahead != '}')
		syn_error("expected '}'");
	else
		syn_advance();
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

	if(parse_failed)
		return (Node)0;
	if(lookahead == REDIR || lookahead == DUP){
		r = parse_redir_word();
		return mung2(r, r->child[0], parse_epilog());
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

	if(parse_failed)
		return (Node)0;
	if(op != REDIR && op != DUP){
		syn_error("expected redir");
		return (Node)0;
	}

	/* get the tree node from yylval before advancing */
	t = yylval.tree;
	syn_advance();

	if(op == REDIR){
		w = parse_word();
		if(t->rtype == HERE)
			return mung1(t, heredoc(w));
		return mung1(t, w);
	}
	/* DUP: pass the lexer's node through (keeps fd/rtype info) */
	return t;
}

/*
 * simple: first
 *       | simple word   { $$ = tree2(ARGLIST, $1, $2); }
 *       | simple redir  { $$ = tree2(ARGLIST, $1, $2); }
 *
 * Single interleaved loop (words and redirs can mix in any order).
 * REDIR '{' is a PIPEFD comword (word), not a redir; handle like
 * parse_bang: consume REDIR then branch on '{'.
 */
static Node
parse_simple(void)
{
	Node t;

	if(parse_failed)
		return (Node)0;
	t = parse_first();
	return parse_simple_rest(t);
}

static Node
parse_simple_rest(Node t)
{
	for(;;){
		if(parse_failed)
			break;
		if(is_wordstart(lookahead)){
			/* REDIR here may still be PIPEFD-word vs redir */
			if(lookahead == REDIR){
				Node r, w;
				r = yylval.tree;
				syn_advance();
				if(lookahead == '{'){
					Node cw, word;
					cw = mung1(r, parse_brace());
					cw->type = PIPEFD;
					/* Left-assoc per %left '^': RHS is base. */
					word = cw;
					while(!parse_failed && lookahead == '^'){
						Node ww;
						syn_advance();
						ww = parse_word_base();
						word = tree2('^', word, ww);
					}
					t = tree2(ARGLIST, t, word);
					continue;
				}
				w = parse_word();
				if(r->rtype == HERE)
					r = mung1(r, heredoc(w));
				else
					r = mung1(r, w);
				t = tree2(ARGLIST, t, r);
				continue;
			}
			t = tree2(ARGLIST, t, parse_word());
		} else if(lookahead == DUP){
			Node r = parse_redir_word();
			t = tree2(ARGLIST, t, r);
		} else
			break;
	}
	return t;
}

/*
 * first: comword
 *      | first '^' word  { $$ = tree2('^', $1, $3); }
 * Left-assoc: iterate with base words.
 */
static Node
parse_first(void)
{
	Node t, w;

	if(parse_failed)
		return (Node)0;
	t = parse_comword();
	while(!parse_failed && lookahead == '^'){
		syn_advance();
		w = parse_word_base();
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
	Node n = yylval.tree;
	if(parse_failed)
		return n;
	syn_advance();
	return n;
}

/*
 * words:          { $$ = (tree*)0; }
 *       | words word { $$ = tree2(WORDS, $1, $2); }
 * Terminators are tokens that cannot start a word: closers, separators,
 * cmd operators, '=' and redirs (redir is not a word).  Keywords CAN
 * start a word (converted by parse_word), so they are not terminators.
 */
static Node
parse_words(void)
{
	Node t = (Node)0;
	Node w;

	for(;;){
		if(parse_failed)
			break;
		if(lookahead == ')' || lookahead == '}'
		    || lookahead == '{'
		    || lookahead == '\n' || lookahead == EOF
		    || lookahead == ANDAND
		    || lookahead == OROR || lookahead == PIPE
		    || lookahead == ';' || lookahead == '&'
		    || lookahead == '=' || lookahead == '^'
		    || lookahead == REDIR || lookahead == DUP
		    || lookahead == SUB)
			break;
		if(!is_wordstart(lookahead))
			break;
		w = parse_word();
		if(w == (Node)0)
			break;
		t = tree2(WORDS, t, w);
	}
	return t;
}

/*
 * word: keyword         { lastword=1; $1->type=WORD; }
 *     | comword
 *     | word '^' word  { $$ = tree2('^', $1, $3); }
 * Left-assoc via base iteration.
 */
static Node
parse_word(void)
{
	Node t, w;

	if(parse_failed)
		return (Node)0;
	t = parse_word_base();
	while(!parse_failed && lookahead == '^'){
		syn_advance();
		w = parse_word_base();
		t = tree2('^', t, w);
	}
	return t;
}

static Node
parse_word_base(void)
{
	Node t;

	if(parse_failed)
		return (Node)0;
	if(lookahead == FOR || lookahead == IN || lookahead == WHILE
	    || lookahead == IF || lookahead == NOT || lookahead == TWIDDLE
	    || lookahead == BANG || lookahead == SUBSHELL
	    || lookahead == SWITCH || lookahead == FN){
		/*
		 * word: keyword { lastword=1; $1->type=WORD; }
		 * yacc runs the action at reduce time, i.e. AFTER the
		 * keyword is recognized but BEFORE the next token is lexed.
		 * The next token is then lexed with lastword=1.
		 */
		lastword = 1;
		t = parse_keyword();
		t->type = WORD;
		return t;
	}
	return parse_comword();
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

	if(parse_failed)
		return (Node)0;
	switch(lookahead){
	case '$':
		syn_advance();
		/*
		 * '$' binds tighter than '^' (see the precedence list in
		 * syn.y: %left '^' is declared before %right '$' COUNT '"'),
		 * so its operand must be a single word_base(), not a full
		 * parse_word(). Calling parse_word() here swallows any
		 * following implicit '^'-concatenation (e.g. the literal
		 * ".install" in "$i.install") into the variable-name operand
		 * itself, so "$i.install" was mis-parsed as "$(i^.install)"
		 * (look up a variable literally named "i.install") instead
		 * of the correct "($i)^.install" (expand $i, then append the
		 * literal ".install"). That silently evaluated to an empty
		 * word instead of concatenating, breaking any "$var.suffix"
		 * pattern (a very common idiom in rc scripts and mkfiles,
		 * e.g. "mk $i.install", "elf=$base.elf").
		 */
		w = parse_word_base();
		if(lookahead == SUB){
			Node words;
			syn_advance();
			words = parse_words();
			if(lookahead != ')')
				syn_error("expected ')' in $");
			else
				syn_advance();
			return tree2(SUB, w, words);
		}
		return tree1('$', w);

	case '"':
		syn_advance();
		/* same precedence issue as '$' above: bind tighter than '^' */
		return tree1('"', parse_word_base());

	case COUNT:
		syn_advance();
		/* same precedence issue as '$' above: bind tighter than '^' */
		return tree1(COUNT, parse_word_base());

	case '`':
		syn_advance();
		if(lookahead == '{')
			return tree2('`', (Node)0, parse_brace());
		if(is_wordstart(lookahead)){
			w = parse_word();
			if(lookahead != '{'){
				syn_error("expected '{' after ` word");
				return tree2('`', w, tree1(BRACE, (Node)0));
			}
			return tree2('`', w, parse_brace());
		}
		syn_error("expected '{' or word after `");
		return tree2('`', (Node)0, tree1(BRACE, (Node)0));

	case '(':
		syn_advance();
		w = parse_words();
		if(lookahead != ')')
			syn_error("expected ')' in comword");
		else
			syn_advance();
		return tree1(PAREN, w);

	case REDIR:
		t = yylval.tree;
		syn_advance();
		w = parse_brace();
		t = mung1(t, w);
		t->type = PIPEFD;
		return t;

	case WORD:
		t = yylval.tree;
		syn_advance();
		return t;

	default:
		syn_error("expected word");
		return (Node)0;
	}
}
