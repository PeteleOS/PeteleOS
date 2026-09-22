/*
 * Token numbers for eqn (hand-parser replacement).
 * Replaces yacc-generated y.tab.h: same numbers as prevy.tab.h
 * (CONTIG 57346 through UTILDE 57402); single-char tokens '{' '}'
 * keep ASCII codes. Included directly by eqn_parse.c and indirectly
 * by the rest of eqn via the static y.tab.h shim.
 */
#ifndef EQN_PARSE_H
#define EQN_PARSE_H

#ifndef YYSTYPE
#define YYSTYPE int
#endif
extern YYSTYPE yylval;
extern YYSTYPE yyval;
extern int yyparse(void);

/* Token numbers: #define (like y.tab.h), not enum, because Plan 9 cc
 * treats enum members as LNAME and any prior #define makes the enum
 * fail with "expected '}'". #undef + #define exactly mimics yacc.
 */
#undef CONTIG
#define CONTIG 57346
#undef QTEXT
#define QTEXT 57347
#undef SPACE
#define SPACE 57348
#undef THIN
#define THIN 57349
#undef TAB
#define TAB 57350
#undef MATRIX
#define MATRIX 57351
#undef LCOL
#define LCOL 57352
#undef CCOL
#define CCOL 57353
#undef RCOL
#define RCOL 57354
#undef COL
#define COL 57355
#undef ABOVE
#define ABOVE 57356
#undef MARK
#define MARK 57357
#undef LINEUP
#define LINEUP 57358
#undef SUM
#define SUM 57359
#undef INT
#define INT 57360
#undef PROD
#define PROD 57361
#undef UNION
#define UNION 57362
#undef INTER
#define INTER 57363
#undef DEFINE
#define DEFINE 57364
#undef TDEFINE
#define TDEFINE 57365
#undef NDEFINE
#define NDEFINE 57366
#undef DELIM
#define DELIM 57367
#undef GSIZE
#define GSIZE 57368
#undef GFONT
#define GFONT 57369
#undef INCLUDE
#define INCLUDE 57370
#undef IFDEF
#define IFDEF 57371
#undef DOTEQ
#define DOTEQ 57372
#undef DOTEN
#define DOTEN 57373
#undef FROM
#define FROM 57374
#undef TO
#define TO 57375
#undef OVER
#define OVER 57376
#undef SQRT
#define SQRT 57377
#undef SUP
#define SUP 57378
#undef SUB
#define SUB 57379
#undef SIZE
#define SIZE 57380
#undef FONT
#define FONT 57381
#undef ROMAN
#define ROMAN 57382
#undef ITALIC
#define ITALIC 57383
#undef BOLD
#define BOLD 57384
#undef FAT
#define FAT 57385
#undef UP
#define UP 57386
#undef DOWN
#define DOWN 57387
#undef BACK
#define BACK 57388
#undef FWD
#define FWD 57389
#undef LEFT
#define LEFT 57390
#undef RIGHT
#define RIGHT 57391
#undef DOT
#define DOT 57392
#undef DOTDOT
#define DOTDOT 57393
#undef HAT
#define HAT 57394
#undef TILDE
#define TILDE 57395
#undef BAR
#define BAR 57396
#undef LOWBAR
#define LOWBAR 57397
#undef HIGHBAR
#define HIGHBAR 57398
#undef UNDER
#define UNDER 57399
#undef VEC
#define VEC 57400
#undef DYAD
#define DYAD 57401
#undef UTILDE
#define UTILDE 57402

#endif
