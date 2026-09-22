/*
 * Token numbers and YYSTYPE for awk (hand-parser replacement).
 * Replaces yacc-generated y.tab.h (same numbers: FIRSTTOKEN 257
 * through LASTTOKEN 349, single-char tokens keep ASCII codes).
 * Included directly by awkgram_parse.c and indirectly by the rest
 * of awk via the static y.tab.h shim.
 */
#ifndef AWKGRAM_PARSE_H
#define AWKGRAM_PARSE_H

typedef union {
	struct Node	*p;
	struct Cell	*cp;
	int	i;
	char	*s;
} YYSTYPE;
extern YYSTYPE yylval;
extern int yyparse(void);

/* Token numbers: #define (like y.tab.h), not enum, because Plan 9 cc
 * treats enum members as LNAME and any prior #define makes the enum
 * fail with "expected '}'". #undef + #define exactly mimics yacc.
 */
#undef FIRSTTOKEN
#define FIRSTTOKEN 257
#undef PROGRAM
#define PROGRAM 258
#undef PASTAT
#define PASTAT 259
#undef PASTAT2
#define PASTAT2 260
#undef XBEGIN
#define XBEGIN 261
#undef XEND
#define XEND 262
#undef NL
#define NL 263
#undef ARRAY
#define ARRAY 264
#undef MATCH
#define MATCH 265
#undef NOTMATCH
#define NOTMATCH 266
#undef MATCHOP
#define MATCHOP 267
#undef FINAL
#define FINAL 268
#undef DOT
#define DOT 269
#undef ALL
#define ALL 270
#undef CCL
#define CCL 271
#undef NCCL
#define NCCL 272
#undef CHAR
#define CHAR 273
#undef OR
#define OR 274
#undef STAR
#define STAR 275
#undef QUEST
#define QUEST 276
#undef PLUS
#define PLUS 277
#undef EMPTYRE
#define EMPTYRE 278
#undef AND
#define AND 279
#undef BOR
#define BOR 280
#undef APPEND
#define APPEND 281
#undef EQ
#define EQ 282
#undef GE
#define GE 283
#undef GT
#define GT 284
#undef LE
#define LE 285
#undef LT
#define LT 286
#undef NE
#define NE 287
#undef IN
#define IN 288
#undef ARG
#define ARG 289
#undef BLTIN
#define BLTIN 290
#undef BREAK
#define BREAK 291
#undef CLOSE
#define CLOSE 292
#undef CONTINUE
#define CONTINUE 293
#undef DELETE
#define DELETE 294
#undef DO
#define DO 295
#undef EXIT
#define EXIT 296
#undef FOR
#define FOR 297
#undef FUNC
#define FUNC 298
#undef SUB
#define SUB 299
#undef GSUB
#define GSUB 300
#undef IF
#define IF 301
#undef INDEX
#define INDEX 302
#undef LSUBSTR
#define LSUBSTR 303
#undef MATCHFCN
#define MATCHFCN 304
#undef NEXT
#define NEXT 305
#undef NEXTFILE
#define NEXTFILE 306
#undef ADD
#define ADD 307
#undef MINUS
#define MINUS 308
#undef MULT
#define MULT 309
#undef DIVIDE
#define DIVIDE 310
#undef MOD
#define MOD 311
#undef ASSIGN
#define ASSIGN 312
#undef ASGNOP
#define ASGNOP 313
#undef ADDEQ
#define ADDEQ 314
#undef SUBEQ
#define SUBEQ 315
#undef MULTEQ
#define MULTEQ 316
#undef DIVEQ
#define DIVEQ 317
#undef MODEQ
#define MODEQ 318
#undef POWEQ
#define POWEQ 319
#undef PRINT
#define PRINT 320
#undef PRINTF
#define PRINTF 321
#undef SPRINTF
#define SPRINTF 322
#undef ELSE
#define ELSE 323
#undef INTEST
#define INTEST 324
#undef CONDEXPR
#define CONDEXPR 325
#undef POSTINCR
#define POSTINCR 326
#undef PREINCR
#define PREINCR 327
#undef POSTDECR
#define POSTDECR 328
#undef PREDECR
#define PREDECR 329
#undef VAR
#define VAR 330
#undef IVAR
#define IVAR 331
#undef VARNF
#define VARNF 332
#undef CALL
#define CALL 333
#undef NUMBER
#define NUMBER 334
#undef STRING
#define STRING 335
#undef REGEXPR
#define REGEXPR 336
#undef GETLINE
#define GETLINE 337
#undef RETURN
#define RETURN 338
#undef SPLIT
#define SPLIT 339
#undef SUBSTR
#define SUBSTR 340
#undef WHILE
#define WHILE 341
#undef CAT
#define CAT 342
#undef NOT
#define NOT 343
#undef UMINUS
#define UMINUS 344
#undef POWER
#define POWER 345
#undef DECR
#define DECR 346
#undef INCR
#define INCR 347
#undef INDIRECT
#define INDIRECT 348
#undef LASTTOKEN
#define LASTTOKEN 349

#endif
