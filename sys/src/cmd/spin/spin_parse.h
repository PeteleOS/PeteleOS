/*
 * Token numbers for spin (hand-parser replacement).
 * Replaces yacc-generated y.tab.h (same order: ASSERT 257 through
 * LTL 322 in %token order, then ASGN 323 through DOT 350 for tokens
 * first declared in %left/%right lines); single-char tokens keep
 * ASCII codes. YYSTYPE is Lextok* (spin.h) as before.
 * Included directly by spin_parse.c and indirectly by the rest of
 * spin via the static y.tab.h shim.
 */
#ifndef SPIN_PARSE_H
#define SPIN_PARSE_H

/* YYSTYPE is defined in spin.h as Lexptr (= Lextok *) */
extern YYSTYPE yylval;
extern int yyparse(void); 

/* Token numbers: #define (like y.tab.h), not enum, because Plan 9 cc
 * treats enum members as LNAME and any prior #define makes the enum
 * fail with "expected '}'". #undef + #define exactly mimics yacc.
 */
#undef ASSERT
#define ASSERT 257
#undef PRINT
#define PRINT 258
#undef PRINTM
#define PRINTM 259
#undef PREPROC
#define PREPROC 260
#undef C_CODE
#define C_CODE 261
#undef C_DECL
#define C_DECL 262
#undef C_EXPR
#define C_EXPR 263
#undef C_STATE
#define C_STATE 264
#undef C_TRACK
#define C_TRACK 265
#undef RUN
#define RUN 266
#undef LEN
#define LEN 267
#undef ENABLED
#define ENABLED 268
#undef SET_P
#define SET_P 269
#undef GET_P
#define GET_P 270
#undef EVAL
#define EVAL 271
#undef PC_VAL
#define PC_VAL 272
#undef TYPEDEF
#define TYPEDEF 273
#undef MTYPE
#define MTYPE 274
#undef INLINE
#define INLINE 275
#undef RETURN
#define RETURN 276
#undef LABEL
#define LABEL 277
#undef OF
#define OF 278
#undef GOTO
#define GOTO 279
#undef BREAK
#define BREAK 280
#undef ELSE
#define ELSE 281
#undef SEMI
#define SEMI 282
#undef ARROW
#define ARROW 283
#undef IF
#define IF 284
#undef FI
#define FI 285
#undef DO
#define DO 286
#undef OD
#define OD 287
#undef FOR
#define FOR 288
#undef SELECT
#define SELECT 289
#undef IN
#define IN 290
#undef SEP
#define SEP 291
#undef DOTDOT
#define DOTDOT 292
#undef ATOMIC
#define ATOMIC 293
#undef NON_ATOMIC
#define NON_ATOMIC 294
#undef D_STEP
#define D_STEP 295
#undef UNLESS
#define UNLESS 296
#undef TIMEOUT
#define TIMEOUT 297
#undef NONPROGRESS
#define NONPROGRESS 298
#undef ACTIVE
#define ACTIVE 299
#undef PROCTYPE
#define PROCTYPE 300
#undef D_PROCTYPE
#define D_PROCTYPE 301
#undef HIDDEN
#define HIDDEN 302
#undef SHOW
#define SHOW 303
#undef ISLOCAL
#define ISLOCAL 304
#undef PRIORITY
#define PRIORITY 305
#undef PROVIDED
#define PROVIDED 306
#undef FULL
#define FULL 307
#undef EMPTY
#define EMPTY 308
#undef NFULL
#define NFULL 309
#undef NEMPTY
#define NEMPTY 310
#undef CONST
#define CONST 311
#undef TYPE
#define TYPE 312
#undef XU
#define XU 313
#undef NAME
#define NAME 314
#undef UNAME
#define UNAME 315
#undef PNAME
#define PNAME 316
#undef INAME
#define INAME 317
#undef STRING
#define STRING 318
#undef CLAIM
#define CLAIM 319
#undef TRACE
#define TRACE 320
#undef INIT
#define INIT 321
#undef LTL
#define LTL 322
#undef ASGN
#define ASGN 323
#undef SND
#define SND 324
#undef O_SND
#define O_SND 325
#undef RCV
#define RCV 326
#undef R_RCV
#define R_RCV 327
#undef IMPLIES
#define IMPLIES 328
#undef EQUIV
#define EQUIV 329
#undef OR
#define OR 330
#undef AND
#define AND 331
#undef ALWAYS
#define ALWAYS 332
#undef EVENTUALLY
#define EVENTUALLY 333
#undef UNTIL
#define UNTIL 334
#undef WEAK_UNTIL
#define WEAK_UNTIL 335
#undef RELEASE
#define RELEASE 336
#undef NEXT
#define NEXT 337
#undef EQ
#define EQ 338
#undef NE
#define NE 339
#undef GT
#define GT 340
#undef LT
#define LT 341
#undef GE
#define GE 342
#undef LE
#define LE 343
#undef LSHIFT
#define LSHIFT 344
#undef RSHIFT
#define RSHIFT 345
#undef INCR
#define INCR 346
#undef DECR
#define DECR 347
#undef UMIN
#define UMIN 348
#undef NEG
#define NEG 349
#undef DOT
#define DOT 350

#endif
