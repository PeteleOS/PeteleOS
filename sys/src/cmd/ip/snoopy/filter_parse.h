/*
 * Hand-written recursive-descent parser for ip/snoopy filter expressions.
 * Replaces the former yacc-generated y.tab.h, exposing only the symbols
 * that main.c and other .c files actually need.
 */
typedef Filter *YYSTYPE;

/* tokens (values > 255 to avoid clashing with single-char lexemes).
 * #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'"
 * on macro collision.
 */
#undef LOR
#define LOR 257
#undef LAND
#define LAND 258
#undef NE
#define NE 259
#undef WORD
#define WORD 260

extern YYSTYPE yylval;
extern Filter *filter;

extern void	yyinit(char*);
extern int	yyparse(void);
extern Filter*	newfilter(void);
