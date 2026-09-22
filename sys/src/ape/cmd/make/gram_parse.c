#include "defs.h"

/*
 * Hand-written recursive-descent replacement for gram.y (LALR).
 *
 * Original yacc grammar (no precedence declarations; stratification
 * is explicit):
 *	file: (empty) | file comline
 *	comline: START | MACRODEF | START namelist deplist shellist | error
 *	namelist: NAME | namelist NAME
 *	deplist: (empty-fatal) | dlist
 *	dlist: sepchar | sepchar AMPER | dlist NAME | dlist AMPER
 *		| dlist AMPERAMPER
 *	sepchar: COLON | DOUBLECOLON
 *	shellist: (empty) | shlist
 *	shlist: SHELLINE | shlist SHELLINE
 * ("=" before actions in gram.y is yacc's action marker for '{...}',
 * not a grammar symbol; e.g. "crs: = {...}" means empty with action.)
 *
 * Precedence-map (hand parser, no expression precedence; LL(1) chain):
 *	file -> comline* (loop until 0/EOF)
 *	comline: MACRODEF => consume; START =>
 *	  peek NAME ? rule-line (namelist deplist shellist + big action)
 *	  : lone START (consume, no action)
 *	namelist: NAME+ (left-assoc list build, verbatim)
 *	deplist: dlist (empty alternative fatals verbatim)
 *	dlist: sepchar [AMPER] (NAME|AMPER|AMPERAMPER)* (left)
 *	sepchar: COLON (ALLDEPS) | DOUBLECOLON (SOMEDEPS)
 *	shellist: shlist | empty (0)
 *	shlist: SHELLINE+ (left, linked via prevshp, verbatim)
 * Call chain: yyparse -> parse_file -> parse_comline ->
 *	parse_namelist/parse_deplist/parse_shellist ->
 *	parse_dlist -> parse_sepchar / parse_shlist.
 * All actions are verbatim from gram.y.
 *
 * Error-recovery-map: original has `comline: error' but yyerror()
 * calls fatal() (exits), so recovery is unreachable.  Hand parser
 * calls yyerror() at the exact failure point (unexpected token,
 * missing separator, etc.) which fatals, matching yacc (no resync).
 */

struct depblock *pp;
static struct shblock *prevshp;

static struct nameblock *lefts[NLEFTS];
struct nameblock *leftp;
static int nlefts;

struct lineblock *lp, *lpp;
static struct depblock *prevdep;
static int sepc;
static int allnowait;

static struct fstack
	{
	FILE *fin;
	char *fname;
	int lineno;
	} filestack[MAXINCLUDE];
static int ninclude = 0;

static char *zznextc;
static int yylineno;
static FILE *fin;

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef NAME
#define NAME 257
#undef SHELLINE
#define SHELLINE 258
#undef START
#define START 259
#undef MACRODEF
#define MACRODEF 260
#undef COLON
#define COLON 261
#undef DOUBLECOLON
#define DOUBLECOLON 262
#undef GREATER
#define GREATER 263
#undef AMPER
#define AMPER 264
#undef AMPERAMPER
#define AMPERAMPER 265

typedef union
	{
	struct shblock *yshblock;
	depblkp ydepblock;
	nameblkp ynameblock;
	} YYSTYPE;
YYSTYPE yylval;

static int yyhave;
static int yysave;
static YYSTYPE yysaveval;

static int yypeek(void);
static int yyget(void);
int yyerror(char*, ...);
static void parse_file(void);
static void parse_comline(void);
static nameblkp parse_namelist(void);
static depblkp parse_deplist(void);
static depblkp parse_dlist(void);
static void parse_sepchar(void);
static struct shblock *parse_shellist(void);
static struct shblock *parse_shlist(void);

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

static nameblkp
parse_namelist(void)
{
	nameblkp n;
	int t;

	t = yyget();
	if(t != NAME)
		yyerror("syntax error");
	n = yylval.ynameblock;
	lefts[0] = n;
	nlefts = 1;
	while(yypeek() == NAME){
		yyget();
		n = yylval.ynameblock;
		lefts[nlefts++] = n;
		if(nlefts >= NLEFTS)
			fatal("Too many lefts");
	}
	return n;
}

static void
parse_sepchar(void)
{
	int t;

	t = yyget();
	if(t == COLON)
		sepc = ALLDEPS;
	else if(t == DOUBLECOLON)
		sepc = SOMEDEPS;
	else
		yyerror("syntax error");
}

static depblkp
parse_dlist(void)
{
	int t;
	depblkp head;
	nameblkp nm;

	/* first must be sepchar [AMPER] */
	parse_sepchar();
	head = 0;
	prevdep = 0;
	allnowait = NO;
	if(yypeek() == AMPER){
		yyget();
		allnowait = YES;
	}
	for(;;){
		t = yypeek();
		if(t == NAME){
			yyget();
			nm = yylval.ynameblock;
			pp = ALLOC(depblock);
			pp->nxtdepblock = NULL;
			pp->depname = nm;
			pp->nowait = allnowait;
			if(prevdep == 0)
				head = pp;
			else
				prevdep->nxtdepblock = pp;
			prevdep = pp;
		} else if(t == AMPER){
			yyget();
			if(prevdep)
				prevdep->nowait = YES;
		} else if(t == AMPERAMPER){
			yyget();
			/* verbatim: no action */
		} else
			break;
	}
	return head;
}

static depblkp
parse_deplist(void)
{
	/* deplist: empty-fatals | dlist.  Empty never occurs in valid
	 * input (caller always has sepchar next); if no sepchar, fatal
	 * verbatim like the empty alternative. */
	int t;

	t = yypeek();
	if(t != COLON && t != DOUBLECOLON){
		char junk[100];
		sprintf(junk, "%s:%d", filestack[ninclude-1].fname, yylineno);
		fatal1("Must be a separator on rules line %s", junk);
	}
	return parse_dlist();
}

static struct shblock *
parse_shlist(void)
{
	struct shblock *head, *s;
	int t;

	t = yyget();
	if(t != SHELLINE)
		yyerror("syntax error");
	head = yylval.yshblock;
	prevshp = head;
	while(yypeek() == SHELLINE){
		yyget();
		s = yylval.yshblock;
		prevshp->nxtshblock = s;
		prevshp = s;
	}
	return head;
}

static struct shblock *
parse_shellist(void)
{
	if(yypeek() == SHELLINE)
		return parse_shlist();
	return 0;
}

static void
parse_comline(void)
{
	int t;
	depblkp deps;
	struct shblock *sh;

	t = yyget();
	if(t == MACRODEF)
		return;
	if(t != START)
		yyerror("syntax error");
	if(yypeek() != NAME){
		/* lone START */
		return;
	}
	parse_namelist();
	deps = parse_deplist();
	sh = parse_shellist();
	/* verbatim action from START namelist deplist shellist */
	{
	    while( --nlefts >= 0)
		{
		wildp wp;

		leftp = lefts[nlefts];
		if(wp = iswild(leftp->namep))
			{
			leftp->septype = SOMEDEPS;
			if(lastwild)
				lastwild->next = wp;
			else
				firstwild = wp;
			lastwild = wp;
			}

		if(leftp->septype == 0)
			leftp->septype = sepc;
		else if(leftp->septype != sepc)
			{
			if(! wp)
				fprintf(stderr,
					"Inconsistent rules lines for `%s'\n",
					leftp->namep);
			}
		else if(sepc==ALLDEPS && leftp->namep[0]!='.' && sh!=0)
			{
			for(lp=leftp->linep; lp->nxtlineblock; lp=lp->nxtlineblock)
			if(lp->shp)
				fprintf(stderr,
					"Multiple rules lines for `%s'\n",
					leftp->namep);
			}

		lp = ALLOC(lineblock);
		lp->nxtlineblock = NULL;
		lp->depp = deps;
		lp->shp = sh;
		if(wp)
			wp->linep = lp;

		if(equal(leftp->namep, ".SUFFIXES") && deps==0)
			leftp->linep = 0;
		else if(leftp->linep == 0)
			leftp->linep = lp;
		else	{
			for(lpp = leftp->linep; lpp->nxtlineblock;
				lpp = lpp->nxtlineblock) ;
				if(sepc==ALLDEPS && leftp->namep[0]=='.')
					lpp->shp = 0;
			lpp->nxtlineblock = lp;
			}
		}
	}
}

static void
parse_file(void)
{
	yyhave = 0;
	for(;;){
		if(yypeek() == 0)
			break;
		parse_comline();
	}
}

int
yyparse(void)
{
	parse_file();
	return 0;
}

static int retsh(char *);
static int nextlin(void);
static int isinclude(char *);

int
parse(char *name)
{
FILE *stream;

if(name == CHNULL)
	{
	stream = NULL;
	name = "(builtin-rules)";
	}
else if(equal(name, "-"))
	{
	stream = stdin;
	name = "(stdin)";
	}
else if( (stream = fopen(name, "r")) == NULL)
	return NO;
filestack[0].fname = copys(name);
ninclude = 1;
fin = stream;
yylineno = 0;
zznextc = 0;

if( yyparse() )
	fatal("Description file error");

if(fin)
	fclose(fin);
return YES;
}

int
yylex(void)
{
char *p;
char *q;
char word[INMAX];

if(! zznextc )
	return nextlin() ;

while( isspace(*zznextc) )
	++zznextc;
switch(*zznextc)
	{
	case '\0':
		return nextlin() ;

	case '|':
		if(zznextc[1]==':')
			{
			zznextc += 2;
			return DOUBLECOLON;
			}
		break;
	case ':':
		if(*++zznextc == ':')
			{
			++zznextc;
			return DOUBLECOLON;
			}
		return COLON;
	case '>':
		++zznextc;
		return GREATER;
	case '&':
		if(*++zznextc == '&')
			{
			++zznextc;
			return AMPERAMPER;
			}
		return AMPER;
	case ';':
		return retsh(zznextc) ;
	}

p = zznextc;
q = word;

while( ! ( funny[*p] & TERMINAL) )
	*q++ = *p++;

if(p != zznextc)
	{
	*q = '\0';
	if((yylval.ynameblock=srchname(word))==0)
		yylval.ynameblock = makename(word);
	zznextc = p;
	return NAME;
	}

else	{
	char junk[100];
	sprintf(junk, "Bad character %c (octal %o), line %d of file %s",
		*zznextc, *zznextc, yylineno, filestack[ninclude-1].fname);
	fatal(junk);
	}
return 0;	/* never executed */
}




static int
retsh(char *q)
{
register char *p;
struct shblock *sp;

for(p=q+1 ; *p==' '||*p=='\t' ; ++p)  ;

sp = ALLOC(shblock);
sp->nxtshblock = NULL;
sp->shbp = (fin ? copys(p) : p );
yylval.yshblock = sp;
zznextc = 0;
return SHELLINE;
}

static int
nextlin(void)
{
static char yytext[INMAX];
static char *yytextl	= yytext+INMAX;
char *text, templin[INMAX];
char c;
char *p, *t;
char lastch, *lastchp;
extern char **linesptr;
int incom;
int kc;

again:

	incom = NO;
	zznextc = 0;

if(fin == NULL)
	{
	if( (text = *linesptr++) == 0)
		return 0;
	++yylineno;
	}

else	{
	for(p = text = yytext ; p<yytextl ; *p++ = kc)
		switch(kc = getc(fin))
			{
			case '\t':
				if(p == yytext)
					incom = YES;
				break;

			case ';':
				incom = YES;
				break;

			case '#':
				if(! incom)
					kc = '\0';
				break;

			case '\n':
				++yylineno;
				if(p==yytext || p[-1]!='\\')
					{
					*p = '\0';
					goto endloop;
					}
				p[-1] = ' ';
				while( (kc=getc(fin))=='\t' || kc==' ' || kc=='\n')
					if(kc == '\n')
						++yylineno;
	
				if(kc != EOF)
					break;
			case EOF:
				*p = '\0';
				if(ninclude > 1)
					{
					register struct fstack *stp;
					fclose(fin);
					--ninclude;
					stp = filestack + ninclude;
					fin = stp->fin;
					yylineno = stp->lineno;
					free(stp->fname);
					goto again;
					}
				return 0;
			}

	fatal("line too long");
	}

endloop:

	if((c = text[0]) == '\t')
		return retsh(text) ;
	
	if(isalpha(c) || isdigit(c) || c==' ' || c=='.'|| c=='_')
		for(p=text+1; *p!='\0'; )
			if(*p == ':')
				break;
			else if(*p++ == '=')
				{
				eqsign(text);
				return MACRODEF;
				}

/* substitute for macros on dependency line up to the semicolon if any */

for(t = yytext ; *t!='\0' && *t!=';' ; ++t)
	;

lastchp = t;
lastch = *t;
*t = '\0';	/* replace the semi with a null so subst will stop */

/* Substitute for macros on dependency lines */
subst(yytext, templin, &templin[sizeof templin - 1]);

if(lastch)	/* copy the stuff after the semicolon */
	{
	*lastchp = lastch;
	strcat(templin, lastchp);
	}

strcpy(yytext, templin);

/* process include files after macro substitution */
if(strncmp(text, "include", 7) == 0) {
  	if (isinclude(text+7))
		goto again;
}

for(p = zznextc = text ; *p ; ++p )
	if(*p!=' ' && *p!='\t')
		return START;
goto again;
}


static int
isinclude(char *s)
{
char *t;
struct fstack *p;

for(t=s; *t==' ' || *t=='\t' ; ++t)
	;
if(t == s)
	return NO;

for(s = t; *s!='\n' && *s!='#' && *s!='\0' ; ++s)
	if(*s == ':')
		return NO;
*s = '\0';

if(ninclude >= MAXINCLUDE)
	fatal("include depth exceeded");
p = filestack + ninclude;
p->fin = fin;
p->lineno = yylineno;
p->fname = copys(t);
if( (fin = fopen(t, "r")) == NULL)
	fatal1("Cannot open include file %s", t);
yylineno = 0;
++ninclude;
return YES;
}


int
yyerror(char *s, ...)
{
char buf[100];

sprintf(buf, "line %d of file %s: %s",
		yylineno, filestack[ninclude-1].fname, s);
fatal(buf);
return 0;
}
