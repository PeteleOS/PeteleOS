/*
 * Token numbers for pic (hand-parser replacement).
 * Replaces yacc-generated y.tab.h: same numbers as prevy.tab.h
 * (BOX 1 through PLACE 13 by explicit %token values, PRINT 270
 * through NOT 353 in declaration order); single-char tokens keep
 * ASCII codes. YYSTYPE and yylval/yyval live in pic.h as before.
 * Included directly by picy_parse.c and indirectly by the rest of
 * pic via the static y.tab.h shim.
 */
#ifndef PICY_PARSE_H
#define PICY_PARSE_H

extern int yyparse(void);

/* Token numbers: #define (like y.tab.h), not enum, because Plan 9 cc
 * treats enum members as LNAME and any prior #define makes the enum
 * fail with "expected '}'". #undef + #define exactly mimics yacc.
 */
#undef BOX
#define BOX 1
#undef LINE
#define LINE 2
#undef ARROW
#define ARROW 3
#undef CIRCLE
#define CIRCLE 4
#undef ELLIPSE
#define ELLIPSE 5
#undef ARC
#define ARC 6
#undef SPLINE
#define SPLINE 7
#undef BLOCK
#define BLOCK 8
#undef TEXT
#define TEXT 9
#undef TROFF
#define TROFF 10
#undef MOVE
#define MOVE 11
#undef BLOCKEND
#define BLOCKEND 12
#undef PLACE
#define PLACE 13
#undef PRINT
#define PRINT 270
#undef RESET
#define RESET 271
#undef THRU
#define THRU 272
#undef UNTIL
#define UNTIL 273
#undef FOR
#define FOR 274
#undef IF
#define IF 275
#undef COPY
#define COPY 276
#undef THENSTR
#define THENSTR 277
#undef ELSESTR
#define ELSESTR 278
#undef DOSTR
#define DOSTR 279
#undef PLACENAME
#define PLACENAME 280
#undef VARNAME
#define VARNAME 281
#undef SPRINTF
#define SPRINTF 282
#undef DEFNAME
#define DEFNAME 283
#undef ATTR
#define ATTR 284
#undef TEXTATTR
#define TEXTATTR 285
#undef LEFT
#define LEFT 286
#undef RIGHT
#define RIGHT 287
#undef UP
#define UP 288
#undef DOWN
#define DOWN 289
#undef FROM
#define FROM 290
#undef TO
#define TO 291
#undef AT
#define AT 292
#undef BY
#define BY 293
#undef WITH
#define WITH 294
#undef HEAD
#define HEAD 295
#undef CW
#define CW 296
#undef CCW
#define CCW 297
#undef THEN
#define THEN 298
#undef HEIGHT
#define HEIGHT 299
#undef WIDTH
#define WIDTH 300
#undef RADIUS
#define RADIUS 301
#undef DIAMETER
#define DIAMETER 302
#undef LENGTH
#define LENGTH 303
#undef SIZE
#define SIZE 304
#undef CORNER
#define CORNER 305
#undef HERE
#define HERE 306
#undef LAST
#define LAST 307
#undef NTH
#define NTH 308
#undef SAME
#define SAME 309
#undef BETWEEN
#define BETWEEN 310
#undef AND
#define AND 311
#undef EAST
#define EAST 312
#undef WEST
#define WEST 313
#undef NORTH
#define NORTH 314
#undef SOUTH
#define SOUTH 315
#undef NE
#define NE 316
#undef NW
#define NW 317
#undef SE
#define SE 318
#undef SW
#define SW 319
#undef START
#define START 320
#undef END
#define END 321
#undef DOTX
#define DOTX 322
#undef DOTY
#define DOTY 323
#undef DOTHT
#define DOTHT 324
#undef DOTWID
#define DOTWID 325
#undef DOTRAD
#define DOTRAD 326
#undef NUMBER
#define NUMBER 327
#undef LOG
#define LOG 328
#undef EXP
#define EXP 329
#undef SIN
#define SIN 330
#undef COS
#define COS 331
#undef ATAN2
#define ATAN2 332
#undef SQRT
#define SQRT 333
#undef RAND
#define RAND 334
#undef MAX
#define MAX 335
#undef MIN
#define MIN 336
#undef INT
#define INT 337
#undef DIR
#define DIR 338
#undef DOT
#define DOT 339
#undef DASH
#define DASH 340
#undef CHOP
#define CHOP 341
#undef FILL
#define FILL 342
#undef ST
#define ST 343
#undef OROR
#define OROR 344
#undef ANDAND
#define ANDAND 345
#undef GT
#define GT 346
#undef LT
#define LT 347
#undef LE
#define LE 348
#undef GE
#define GE 349
#undef EQ
#define EQ 350
#undef NEQ
#define NEQ 351
#undef UMINUS
#define UMINUS 352
#undef NOT
#define NOT 353
#undef NOEDGE
#define NOEDGE 354

#endif
