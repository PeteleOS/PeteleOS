#ifndef HOC_PARSE_H
#define HOC_PARSE_H
/* Token defines compatible with old y.tab.h (single-char = ASCII).
 * #define (like yacc), not enum: Plan 9 cc treats enum members as LNAME
 * and any prior #define makes the enum fail with "expected '}'".
 */
#undef NUMBER
#define NUMBER 257
#undef STRING
#define STRING 258
#undef PRINT
#define PRINT 259
#undef VAR
#define VAR 260
#undef BLTIN
#define BLTIN 261
#undef UNDEF
#define UNDEF 262
#undef WHILE
#define WHILE 263
#undef FOR
#define FOR 264
#undef IF
#define IF 265
#undef ELSE
#define ELSE 266
#undef FUNCTION
#define FUNCTION 267
#undef PROCEDURE
#define PROCEDURE 268
#undef RETURN
#define RETURN 269
#undef FUNC
#define FUNC 270
#undef PROC
#define PROC 271
#undef READ
#define READ 272
#undef ADDEQ
#define ADDEQ 273
#undef SUBEQ
#define SUBEQ 274
#undef MULEQ
#define MULEQ 275
#undef DIVEQ
#define DIVEQ 276
#undef MODEQ
#define MODEQ 277
#undef OR
#define OR 278
#undef AND
#define AND 279
#undef GT
#define GT 280
#undef GE
#define GE 281
#undef LT
#define LT 282
#undef LE
#define LE 283
#undef EQ
#define EQ 284
#undef NE
#define NE 285
#undef UNARYMINUS
#define UNARYMINUS 286
#undef NOT
#define NOT 287
#undef INC
#define INC 288
#undef DEC
#define DEC 289
typedef union {
	Symbol	*sym;
	Inst	*inst;
	int	narg;
	Formal	*formals;
} YYSTYPE;
extern YYSTYPE yylval;
#endif
