/*
 * Token numbers for grap (hand-parser replacement).
 * Replaces yacc-generated y.tab.h (same order: FRAME 257 through
 * ST 320 in %token order, then OR 321 through UMINUS 330 for tokens
 * first declared in %left/%right lines); single-char tokens keep
 * ASCII codes. YYSTYPE and yylval/yyval live in grap.h as before.
 * Included directly by grap_parse.c and indirectly by the rest of
 * grap via the static y.tab.h shim.
 */
#ifndef GRAP_PARSE_H
#define GRAP_PARSE_H

extern int yyparse(void);

/* Token numbers: #define (like y.tab.h), not enum, because Plan 9 cc
 * treats enum members as LNAME and any prior #define makes the enum
 * fail with "expected '}'". #undef + #define exactly mimics yacc.
 */
#undef FRAME
#define FRAME 257
#undef TICKS
#define TICKS 258
#undef GRID
#define GRID 259
#undef LABEL
#define LABEL 260
#undef COORD
#define COORD 261
#undef LINE
#define LINE 262
#undef ARROW
#define ARROW 263
#undef CIRCLE
#define CIRCLE 264
#undef DRAW
#define DRAW 265
#undef NEW
#define NEW 266
#undef PLOT
#define PLOT 267
#undef NEXT
#define NEXT 268
#undef PIC
#define PIC 269
#undef COPY
#define COPY 270
#undef THRU
#define THRU 271
#undef UNTIL
#define UNTIL 272
#undef FOR
#define FOR 273
#undef FROM
#define FROM 274
#undef TO
#define TO 275
#undef BY
#define BY 276
#undef AT
#define AT 277
#undef WITH
#define WITH 278
#undef IF
#define IF 279
#undef GRAPH
#define GRAPH 280
#undef THEN
#define THEN 281
#undef ELSE
#define ELSE 282
#undef DOSTR
#define DOSTR 283
#undef DOT
#define DOT 284
#undef DASH
#define DASH 285
#undef INVIS
#define INVIS 286
#undef SOLID
#define SOLID 287
#undef TEXT
#define TEXT 288
#undef JUST
#define JUST 289
#undef SIZE
#define SIZE 290
#undef LOG
#define LOG 291
#undef EXP
#define EXP 292
#undef SIN
#define SIN 293
#undef COS
#define COS 294
#undef ATAN2
#define ATAN2 295
#undef SQRT
#define SQRT 296
#undef RAND
#define RAND 297
#undef MAX
#define MAX 298
#undef MIN
#define MIN 299
#undef INT
#define INT 300
#undef PRINT
#define PRINT 301
#undef SPRINTF
#define SPRINTF 302
#undef X
#define X 303
#undef Y
#define Y 304
#undef SIDE
#define SIDE 305
#undef IN
#define IN 306
#undef OUT
#define OUT 307
#undef OFF
#define OFF 308
#undef UP
#define UP 309
#undef DOWN
#define DOWN 310
#undef ACROSS
#define ACROSS 311
#undef HEIGHT
#define HEIGHT 312
#undef WIDTH
#define WIDTH 313
#undef RADIUS
#define RADIUS 314
#undef NUMBER
#define NUMBER 315
#undef NAME
#define NAME 316
#undef VARNAME
#define VARNAME 317
#undef DEFNAME
#define DEFNAME 318
#undef STRING
#define STRING 319
#undef ST
#define ST 320
#undef OR
#define OR 321
#undef AND
#define AND 322
#undef GT
#define GT 323
#undef LT
#define LT 324
#undef LE
#define LE 325
#undef GE
#define GE 326
#undef EQ
#define EQ 327
#undef NE
#define NE 328
#undef NOT
#define NOT 329
#undef UMINUS
#define UMINUS 330

#endif
