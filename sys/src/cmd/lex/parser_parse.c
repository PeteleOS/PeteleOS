# include "ldefs.h"
#define YYSTYPE union _yystype_
union _yystype_
{
	int	i;
	uchar	*cp;
};
YYSTYPE yylval;

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef CHAR
#define CHAR 257
#undef CCL
#define CCL 258
#undef NCCL
#define NCCL 259
#undef STR
#define STR 260
#undef DELIM
#define DELIM 261
#undef SCON
#define SCON 262
#undef ITER
#define ITER 263
#undef NEWE
#define NEWE 264
#undef NULLS
#define NULLS 265
#undef CAT
#define CAT 266

int yyparse(void);
int yylex(void);

/*
 * Hand-written recursive-descent replacement for parser.y (LALR).
 *
 * Original yacc grammar (precedence low -> high):
 *	%left SCON '/' NEWE
 *	%left '|'
 *	%left '$' '^'
 *	%left CHAR CCL NCCL '(' '.' STR NULLS
 *	%left ITER
 *	%left CAT
 *	%left '*' '+' '?'
 *	acc: lexinput
 *	lexinput: defns delim prods end | defns delim end | error
 *	end: delim | (empty)
 *	defns: defns STR STR | (empty)
 *	delim: DELIM
 *	prods: prods pr | pr
 *	pr: r NEWE | error NEWE
 *	r: CHAR | STR | '.' | CCL | NCCL | r '*' | r '+' | r '?'
 *	| r '|' r | r r %prec CAT | r '/' r
 *	| r ITER ',' ITER '}' | r ITER '}' | r ITER ',' '}'
 *	| SCON r | '^' r | r '$' | '(' r ')' | NULLS
 *
 * Precedence-map (hand parser, low -> high):
 *	level 1 (left): SCON (prefix <...>), '/' (trailing context)
 *	level 2 (left): '|' (BAR)
 *	level 3: '^' (prefix CARAT, right), '$' (postfix, left)
 *	level 4: CAT (concatenation, implicit, left) -- operands are
 *	  level-5 ITER results so `ab' == RCAT(a,b)
 *	level 5: ITER postfix {n}, {n,m}, {n,} (left)
 *	level 6 (highest, postfix): '*', '+', '?' (left)
 *	primary: CHAR STR '.' CCL NCCL '(' r ')' NULLS
 * Call chain: yyparse -> parse_acc -> parse_lexinput ->
 *	parse_defns/parse_delim/parse_prods/parse_pr -> parse_r ->
 *	parse_bar -> parse_caret -> parse_cat -> parse_iter ->
 *	parse_postfix -> parse_primary.
 * All actions copied verbatim from parser.y (mn0/mn1/mn2/mnp/dupl,
 * divflg/casecount handling, warnings). File-scope i, j, k, g, p
 * are the former yacc temporaries from parser.y lines 20-25.
 *
 * Error-recovery-map: original had `lexinput: error' (dump sections
 * if debug) and `pr: error NEWE' (dump if debug, resume at next rule).
 * Hand parser: inside parse_r, on unexpected token call yyerror and
 * skip to a sync token (NEWE, ')', '|', '/', '$', '*', '+', '?',
 * ITER, ',', '}', DELIM, 0); parse_pr skips to NEWE and consumes it
 * so the prods loop continues with the next rule; parse_lexinput
 * skips to DELIM/0 on top-level errors. yyparse returns 0 on success,
 * 1 on error, matching lmain.c's `if(yyparse()) exits("error")'.
 * The yylex() function below is character-for-character the one from
 * parser.y lines 218-653 (lex description-file scanner); only the
 * surrounding yacc tables are gone.
 */

static int i;
static int j, k;
static int g;
static uchar *p;

/* 1-token lookahead buffer over yylex() */
static int yyhave;
static int yysave;
static YYSTYPE yysaveval;

static int
yypeek(void)
{
	if(!yyhave){
		yysave = yylex();
		yysaveval = yylval;
		yyhave = 1;
	}
	return yysave;
}

static int
yyget(void)
{
	if(yyhave){
		yyhave = 0;
		yylval = yysaveval;
		return yysave;
	}
	return yylex();
}

static int parse_acc(void);
static int parse_lexinput(void);
static int parse_end(void);
static int parse_defns(void);
static int parse_delim(void);
static int parse_prods(void);
static int parse_pr(void);
static int parse_r(void);
static int parse_bar(void);
static int parse_caret(void);
static int parse_cat(void);
static int parse_iter(void);
static int parse_postfix(void);
static int parse_primary(void);

/* can token t start an r (for implicit CAT)? */
static int
rstart(int t)
{
	return t == CHAR || t == STR || t == '.' || t == CCL
	    || t == NCCL || t == '(' || t == NULLS
	    || t == SCON || t == '^';
}

/* sync set for skipping inside r */
static int
rsync(int t)
{
	return t == 0 || t == NEWE || t == ')' || t == '|'
	    || t == '/' || t == '$' || t == '*' || t == '+'
	    || t == '?' || t == ITER || t == ',' || t == '}'
	    || t == DELIM;
}

static void
rskip(void)
{
	int t;

	t = yypeek();
	while(!rsync(t)){
		yyget();
		t = yypeek();
	}
}

static int
parse_primary(void)
{
	int t, v;

	t = yyget();
	switch(t){
	case CHAR:
		v = mn0(yylval.i);
		return v;
	case STR:
		p = yylval.cp;
		v = mn0(*p++);
		while(*p)
			v = mn2(RSTR, v, *p++);
		return v;
	case '.':
		symbol['\n'] = 0;
		if(psave == FALSE){
			p = ccptr;
			psave = ccptr;
			for(i = 1; i < '\n'; i++){
				symbol[i] = 1;
				*ccptr++ = i;
			}
			for(i = '\n'+1; i < NCH; i++){
				symbol[i] = 1;
				*ccptr++ = i;
			}
			*ccptr++ = 0;
			if(ccptr > ccl+CCLSIZE)
				error("Too many large character classes");
		}else
			p = psave;
		v = mnp(RCCL, p);
		cclinter(1);
		return v;
	case CCL:
		v = mnp(RCCL, yylval.cp);
		return v;
	case NCCL:
		v = mnp(RNCCL, yylval.cp);
		return v;
	case '(':
		v = parse_r();
		if(yyget() != ')'){
			yyerror("syntax error");
			rskip();
			return v;
		}
		return v;
	case NULLS:
		v = mn0(RNULLS);
		return v;
	default:
		yyerror("syntax error");
		rskip();
		return mn0(RNULLS);
	}
}

static int
parse_postfix(void)
{
	int v;

	v = parse_primary();
	for(;;){
		if(yypeek() == '*'){
			yyget();
			v = mn1(STAR, v);
		}else if(yypeek() == '+'){
			yyget();
			v = mn1(PLUS, v);
		}else if(yypeek() == '?'){
			yyget();
			v = mn1(QUEST, v);
		}else
			break;
	}
	return v;
}

static int
parse_iter(void)
{
	int v, lo, hi;

	v = parse_postfix();
	for(;;){
		if(yypeek() != ITER)
			break;
		yyget();
		lo = yylval.i;
		if(yypeek() == ','){
			yyget();
			if(yypeek() == '}'){
				yyget();
				/* r ITER ',' '}' : n to infinity */
				if(lo < 0)
					warning("Can't have negative iteration");
				else if(lo == 0)
					v = mn1(STAR, v);
				else if(lo == 1)
					v = mn1(PLUS, v);
				else{
					j = v;
					for(k = 2; k < lo; k++)
						j = mn2(RCAT, j, dupl(v));
					k = mn1(PLUS, dupl(v));
					v = mn2(RCAT, j, k);
				}
			}else if(yypeek() == ITER){
				yyget();
				hi = yylval.i;
				if(yyget() != '}'){
					yyerror("syntax error");
					rskip();
					break;
				}
				/* r ITER ',' ITER '}' */
				if(lo > hi){
					i = lo;
					lo = hi;
					hi = i;
				}
				if(hi <= 0)
					warning("Iteration range must be positive");
				else{
					j = v;
					for(k = 2; k <= lo; k++)
						j = mn2(RCAT, j, dupl(v));
					for(i = lo+1; i <= hi; i++){
						g = dupl(v);
						for(k = 2; k <= i; k++)
							g = mn2(RCAT, g, dupl(v));
						j = mn2(BAR, j, g);
					}
					v = j;
				}
			}else{
				yyerror("syntax error");
				rskip();
				break;
			}
		}else if(yypeek() == '}'){
			yyget();
			/* r ITER '}' */
			if(lo < 0)
				warning("Can't have negative iteration");
			else if(lo == 0)
				v = mn0(RNULLS);
			else{
				j = v;
				for(k = 2; k <= lo; k++)
					j = mn2(RCAT, j, dupl(v));
				v = j;
			}
		}else
			break;
	}
	return v;
}

static int
parse_cat(void)
{
	int v, w;

	v = parse_iter();
	while(rstart(yypeek())){
		w = parse_iter();
		v = mn2(RCAT, v, w);
	}
	return v;
}

static int
parse_caret(void)
{
	int v;

	if(yypeek() == '^'){
		yyget();
		v = parse_caret();
		v = mn1(CARAT, v);
		return v;
	}
	v = parse_cat();
	if(yypeek() == '$'){
		yyget();
		i = mn0('\n');
		if(!divflg){
			j = mn1(S2FINAL, -casecount);
			k = mn2(RCAT, v, j);
			v = mn2(DIV, k, i);
		}else
			v = mn2(RCAT, v, i);
		divflg = TRUE;
	}
	return v;
}

static int
parse_bar(void)
{
	int v, w;

	v = parse_caret();
	while(yypeek() == '|'){
		yyget();
		w = parse_caret();
		v = mn2(BAR, v, w);
	}
	return v;
}

static int
parse_r(void)
{
	int v, w;

	if(yypeek() == SCON){
		uchar *s;

		yyget();
		s = yylval.cp;
		v = parse_r();
		v = mn2(RSCON, v, (uintptr)s);
		return v;
	}
	v = parse_bar();
	while(yypeek() == '/'){
		yyget();
		w = parse_bar();
		if(!divflg){
			j = mn1(S2FINAL, -casecount);
			i = mn2(RCAT, v, j);
			v = mn2(DIV, i, w);
		}else{
			v = mn2(RCAT, v, w);
			warning("Extra slash removed");
		}
		divflg = TRUE;
	}
	return v;
}

/* pr: r NEWE | error NEWE -- returns node or -1 on recovered error */
static int
parse_pr(void)
{
	int v;

	if(yypeek() == NEWE){
		/* empty r before newline: let parse_r report it */
	}
	if(yypeek() == DELIM || yypeek() == 0){
		return -1;
	}
	/* error NEWE recovery: skip a bad rule up to newline */
	if(!rstart(yypeek()) && yypeek() != '(' && yypeek() != SCON
	    && yypeek() != '^' && yypeek() != NEWE){
		yyerror("syntax error");
		while(yypeek() != NEWE && yypeek() != 0 && yypeek() != DELIM)
			yyget();
		if(yypeek() == NEWE)
			yyget();
# ifdef DEBUG
		if(debug)
			sect2dump();
# endif
		return -1;
	}
	v = parse_r();
	if(yypeek() != NEWE){
		yyerror("syntax error");
		while(yypeek() != NEWE && yypeek() != 0 && yypeek() != DELIM)
			yyget();
		if(yypeek() == NEWE)
			yyget();
		return -1;
	}
	yyget();	/* consume NEWE */
	if(divflg == TRUE)
		i = mn1(S1FINAL, casecount);
	else
		i = mn1(FINAL, casecount);
	v = mn2(RCAT, v, i);
	divflg = FALSE;
	casecount++;
	return v;
}

static int
parse_prods(void)
{
	int v, w;

	v = parse_pr();
	if(v < 0)
		v = mn0(RNULLS);	/* recovered empty rule keeps list alive */
	while(yypeek() != DELIM && yypeek() != 0){
		w = parse_pr();
		if(w < 0)
			continue;
		v = mn2(RNEWE, v, w);
	}
	return v;
}

static int
parse_delim(void)
{
	if(yyget() != DELIM){
		yyerror("syntax error");
		return -1;
	}
# ifdef DEBUG
	if(sect == DEFSECTION && debug)
		sect1dump();
# endif
	sect++;
	return 0;
}

static int
parse_defns(void)
{
	uchar *a, *b;

	while(yypeek() == STR){
		yyget();
		a = yylval.cp;
		if(yypeek() != STR){
			yyerror("syntax error");
			return -1;
		}
		yyget();
		b = yylval.cp;
		strcpy((char*)dp, (char*)a);
		def[dptr] = dp;
		dp += strlen((char*)a) + 1;
		strcpy((char*)dp, (char*)b);
		subs[dptr++] = dp;
		if(dptr >= DEFSIZE)
			error("Too many definitions");
		dp += strlen((char*)b) + 1;
		if(dp >= dchar+DEFCHAR)
			error("Definitions too long");
		subs[dptr] = def[dptr] = 0;
	}
	return 0;
}

static int
parse_end(void)
{
	if(yypeek() == DELIM)
		yyget();
	return 0;
}

static int
parse_lexinput(void)
{
	int v;

	if(yypeek() == 0 || yypeek() == DELIM){
		/* defns delim end | defns delim delim handled below */
	}
	if(parse_defns() < 0)
		return -1;
	if(parse_delim() < 0)
		return -1;
	if(yypeek() == DELIM || yypeek() == 0){
		/* defns delim end */
		if(!funcflag)
			phead2();
		funcflag = TRUE;
		parse_end();
		return 0;
	}
	v = parse_prods();
	parse_end();
	return v;
}

static int
parse_acc(void)
{
	int v;

	v = parse_lexinput();
# ifdef DEBUG
	if(debug)
		sect2dump();
# endif
	return v;
}

int
yyparse(void)
{
	int v;

	yyhave = 0;
	divflg = FALSE;
	v = parse_acc();
	if(yypeek() != 0){
		yyerror("syntax error");
		return 1;
	}
	return v < 0 ? 1 : 0;
}

/* ---- yylex below is verbatim parser.y lines 218-653 ---- */
int
yylex(void)
{
	uchar *p;
	int c, i;
	uchar  *t, *xp;
	int n, j, k, x;
	static int sectbegin;
	static uchar token[TOKENSIZE];
	static int iter;

# ifdef DEBUG
	yylval.i = 0;
	yylval.cp = 0;
# endif

	if(sect == DEFSECTION) {		/* definitions section */
		while(!eof) {
			if(prev == '\n'){		/* next char is at beginning of line */
				getl(p=buf);
				switch(*p){
				case '%':
					switch(*(p+1)){
					case '%':
						lgate();
						Bprint(&fout,"#define YYNEWLINE %d\n",'\n');
						Bprint(&fout,"yylex(void){\nint nstr; extern int yyprevious;\n");
						sectbegin = TRUE;
						i = treesize*(sizeof(*name)+sizeof(*left)+
							sizeof(*right)+sizeof(*nullstr)+sizeof(*parent))+ALITTLEEXTRA;
						p = myalloc(i,1);
						if(p == 0)
							error("Too little core for parse tree");
						free(p);
						name = myalloc(treesize,sizeof(*name));
						left = myalloc(treesize,sizeof(*left));
						right = myalloc(treesize,sizeof(*right));
						nullstr = myalloc(treesize,sizeof(*nullstr));
						parent = myalloc(treesize,sizeof(*parent));
						ptr = myalloc(treesize,sizeof(*ptr));
						if(name == 0 || left == 0 || right == 0 || parent == 0 || nullstr == 0 || ptr == 0)
							error("Too little core for parse tree");
						return(freturn(DELIM));
					case 'p': case 'P':	/* has overridden number of positions */
						while(*p && !isdigit(*p))p++;
						maxpos = atol((char*)p);
# ifdef DEBUG
						if (debug) print("positions (%%p) now %d\n",maxpos);
# endif
						if(report == 2)report = 1;
						continue;
					case 'n': case 'N':	/* has overridden number of states */
						while(*p && !isdigit(*p))p++;
						nstates = atol((char*)p);
# ifdef DEBUG
						if(debug)print( " no. states (%%n) now %d\n",nstates);
# endif
						if(report == 2)report = 1;
						continue;
					case 'e': case 'E':		/* has overridden number of tree nodes */
						while(*p && !isdigit(*p))p++;
						treesize = atol((char*)p);
# ifdef DEBUG
						if (debug) print("treesize (%%e) now %d\n",treesize);
# endif
						if(report == 2)report = 1;
						continue;
					case 'o': case 'O':
						while (*p && !isdigit(*p))p++;
						outsize = atol((char*)p);
						if (report ==2) report=1;
						continue;
					case 'a': case 'A':		/* has overridden number of transitions */
						while(*p && !isdigit(*p))p++;
						if(report == 2)report = 1;
						ntrans = atol((char*)p);
# ifdef DEBUG
						if (debug)print("N. trans (%%a) now %d\n",ntrans);
# endif
						continue;
					case 'k': case 'K': /* overriden packed char classes */
						while (*p && !isdigit(*p))p++;
						if (report==2) report=1;
						free(pchar);
						pchlen = atol((char*)p);
# ifdef DEBUG
						if (debug) print( "Size classes (%%k) now %d\n",pchlen);
# endif
						pchar=pcptr=myalloc(pchlen, sizeof(*pchar));
						continue;
					case '{':
						lgate();
						while(getl(p) && strcmp((char*)p,"%}") != 0)
							Bprint(&fout, "%s\n",(char*)p);
						if(p[0] == '%') continue;
						error("Premature eof");
					case 's': case 'S':		/* start conditions */
						lgate();
						while(*p && strchr(" \t,", *p) == 0) p++;
						n = TRUE;
						while(n){
							while(*p && strchr(" \t,", *p)) p++;
							t = p;
							while(*p && strchr(" \t,", *p) == 0)p++;
							if(!*p) n = FALSE;
							*p++ = 0;
							if (*t == 0) continue;
							i = sptr*2;
							Bprint(&fout,"#define %s %d\n",(char*)t,i);
							strcpy((char*)sp, (char*)t);
							sname[sptr++] = sp;
							sname[sptr] = 0;	/* required by lookup */
							if(sptr >= STARTSIZE)
								error("Too many start conditions");
							sp += strlen((char*)sp) + 1;
							if(sp >= stchar+STARTCHAR)
								error("Start conditions too long");
						}
						continue;
					default:
						warning("Invalid request %s",p);
						continue;
					}	/* end of switch after seeing '%' */
				case ' ': case '\t':		/* must be code */
					lgate();
					Bprint(&fout, "%s\n",(char*)p);
					continue;
				default:		/* definition */
					while(*p && !isspace(*p)) p++;
					if(*p == 0)
						continue;
					prev = *p;
					*p = 0;
					bptr = p+1;
					yylval.cp = buf;
					if(isdigit(buf[0]))
						warning("Substitution strings may not begin with digits");
					return(freturn(STR));
				}
			}
			/* still sect 1, but prev != '\n' */
			else {
				p = bptr;
				while(*p && isspace(*p)) p++;
				if(*p == 0)
					warning("No translation given - null string assumed");
				strcpy((char*)token, (char*)p);
				yylval.cp = token;
				prev = '\n';
				return(freturn(STR));
			}
		}
		/* end of section one processing */
	} else if(sect == RULESECTION){		/* rules and actions */
		while(!eof){
			switch(c=gch()){
			case '\0':
				return(freturn(0));
			case '\n':
				if(prev == '\n') continue;
				x = NEWE;
				break;
			case ' ':
			case '\t':
				if(sectbegin == TRUE){
					cpyact();
					while((c=gch()) && c != '\n');
					continue;
				}
				if(!funcflag)phead2();
				funcflag = TRUE;
				Bprint(&fout,"case %d:\n",casecount);
				if(cpyact())
					Bprint(&fout,"break;\n");
				while((c=gch()) && c != '\n');
				if(peek == ' ' || peek == '\t' || sectbegin == TRUE){
					warning("Executable statements should occur right after %%");
					continue;
				}
				x = NEWE;
				break;
			case '%':
				if(prev != '\n') goto character;
				if(peek == '{'){	/* included code */
					getl(buf);
					while(!eof && getl(buf) && strcmp("%}",(char*)buf) != 0)
						Bprint(&fout,"%s\n",(char*)buf);
					continue;
				}
				if(peek == '%'){
					gch();
					gch();
					x = DELIM;
					break;
				}
				goto character;
			case '|':
				if(peek == ' ' || peek == '\t' || peek == '\n'){
					Bprint(&fout,"%d\n",30000+casecount++);
					continue;
				}
				x = '|';
				break;
			case '$':
				if(peek == '\n' || peek == ' ' || peek == '\t' || peek == '|' || peek == '/'){
					x = c;
					break;
				}
				goto character;
			case '^':
				if(prev != '\n' && scon != TRUE) goto character;	/* valid only at line begin */
				x = c;
				break;
			case '?':
			case '+':
			case '.':
			case '*':
			case '(':
			case ')':
			case ',':
			case '/':
				x = c;
				break;
			case '}':
				iter = FALSE;
				x = c;
				break;
			case '{':	/* either iteration or definition */
				if(isdigit(c=gch())){	/* iteration */
					iter = TRUE;
				ieval:
					i = 0;
					while(isdigit(c)){
						token[i++] = c;
						c = gch();
					}
					token[i] = 0;
					yylval.i = atol((char*)token);
					munputc(c);
					x = ITER;
					break;
				} else {		/* definition */
					i = 0;
					while(c && c!='}'){
						token[i++] = c;
						c = gch();
					}
					token[i] = 0;
					i = lookup(token,def);
					if(i < 0)
						warning("Definition %s not found",token);
					else
						munputs(subs[i]);
					continue;
				}
			case '<':		/* start condition ? */
				if(prev != '\n')		/* not at line begin, not start */
					goto character;
				t = slptr;
				do {
					i = 0;
					c = gch();
					while(c != ',' && c && c != '>'){
						token[i++] = c;
						c = gch();
					}
					token[i] = 0;
					if(i == 0)
						goto character;
					i = lookup(token,sname);
					if(i < 0) {
						warning("Undefined start condition %s",token);
						continue;
					}
					*slptr++ = i+1;
				} while(c && c != '>');
				*slptr++ = 0;
				/* check if previous value re-usable */
				for (xp=slist; xp<t; ){
					if (strcmp((char*)xp, (char*)t)==0)
						break;
					while (*xp++);
				}
				if (xp<t){
					/* re-use previous pointer to string */
					slptr=t;
					t=xp;
				}
				if(slptr > slist+STARTSIZE)		/* note not packed ! */
					error("Too many start conditions used");
				yylval.cp = t;
				x = SCON;
				break;
			case '"':
				i = 0;
				while((c=gch()) && c != '"' && c != '\n'){
					if(c == '\\') c = usescape(gch());
					token[i++] = c;
					if(i > TOKENSIZE){
						warning("String too long");
						i = TOKENSIZE-1;
						break;
					}
				}
				if(c == '\n') {
					yyline--;
					warning("Non-terminated string");
					yyline++;
				}
				token[i] = 0;
				if(i == 0)x = NULLS;
				else if(i == 1){
					yylval.i = token[0];
					x = CHAR;
				} else {
					yylval.cp = token;
					x = STR;
				}
				break;
			case '[':
				for(i=1;i<NCH;i++) symbol[i] = 0;
				x = CCL;
				if((c = gch()) == '^'){
					x = NCCL;
					c = gch();
				}
				while(c != ']' && c){
					if(c == '\\') c = usescape(gch());
					symbol[c] = 1;
					j = c;
					if((c=gch()) == '-' && peek != ']'){		/* range specified */
						c = gch();
						if(c == '\\') c = usescape(gch());
						k = c;
						if(j > k) {
							n = j;
							j = k;
							k = n;
						}
						if(!(('A' <= j && k <= 'Z') ||
						     ('a' <= j && k <= 'z') ||
						     ('0' <= j && k <= '9')))
							warning("Non-portable Character Class");
						for(n=j+1;n<=k;n++)
							symbol[n] = 1;		/* implementation dependent */
						c = gch();
					}
				}
				/* try to pack ccl's */
				i = 0;
				for(j=0;j<NCH;j++)
					if(symbol[j])token[i++] = j;
				token[i] = 0;
				p = ccl;
				while(p <ccptr && strcmp((char*)token,(char*)p) != 0)p++;
				if(p < ccptr)	/* found it */
					yylval.cp = p;
				else {
					yylval.cp = ccptr;
					strcpy((char*)ccptr,(char*)token);
					ccptr += strlen((char*)token) + 1;
					if(ccptr >= ccl+CCLSIZE)
						error("Too many large character classes");
				}
				cclinter(x==CCL);
				break;
			case '\\':
				c = usescape(gch());
			default:
			character:
				if(iter){	/* second part of an iteration */
					iter = FALSE;
					if('0' <= c && c <= '9')
						goto ieval;
				}
				if(isalpha(peek)){
					i = 0;
					yylval.cp = token;
					token[i++] = c;
					while(isalpha(peek))
						token[i++] = gch();
					if(peek == '?' || peek == '*' || peek == '+')
						munputc(token[--i]);
					token[i] = 0;
					if(i == 1){
						yylval.i = token[0];
						x = CHAR;
					}
					else x = STR;
				} else {
					yylval.i = c;
					x = CHAR;
				}
			}
			scon = FALSE;
			if(x == SCON)scon = TRUE;
			sectbegin = FALSE;
			return(freturn(x));
		}
	}
	/* section three */
	ptail();
# ifdef DEBUG
	if(debug)
		Bprint(&fout,"\n/*this comes from section three - debug */\n");
# endif
	while(getl(buf) && !eof)
		Bprint(&fout,"%s\n",(char*)buf);
	return(freturn(0));
}
/* end of yylex */
