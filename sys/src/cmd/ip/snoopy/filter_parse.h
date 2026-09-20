/*
 * Hand-written recursive-descent parser for ip/snoopy filter expressions.
 * Replaces the former yacc-generated y.tab.h, exposing only the symbols
 * that main.c and other .c files actually need.
 */
typedef Filter *YYSTYPE;

/* tokens (values > 255 to avoid clashing with single-char lexemes) */
enum {
	LOR	= 257,
	LAND,
	NE,
	WORD,
};

extern YYSTYPE yylval;
extern Filter *filter;

extern void	yyinit(char*);
extern int	yyparse(void);
extern Filter*	newfilter(void);
