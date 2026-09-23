#include "a.h"
#include "a_parse.h"

YYSTYPE yylval;
/*
 * Hand-written recursive-descent replacement for a.y (LALR, 9a/ppc64).
 * Original precedence low->high: '|' '^' '&' '<' '>' '+' '-' '*' '/' '%'.
 * prog: | prog line
 * line: LLAB ':' line | LNAME ':' line | LNAME '=' expr ';' | LVAR '=' expr ';'
 *     | LSCHED ';' | ';' | inst ';' | error ';'
 * inst: LMOVW rreg ',' rreg | LMOVW addr ',' rreg | LMOVW regaddr ',' rreg
 *     | LMOVB rreg ',' rreg | LMOVB addr ',' rreg | LMOVB regaddr ',' rreg
 *     | LFMOV addr ',' freg | LFMOV regaddr ',' freg | LFMOV fimm ',' freg
 *     | LFMOV freg ',' freg | LFMOV freg ',' addr | LFMOV freg ',' regaddr
 *     | LMOVW rreg ',' addr | LMOVW rreg ',' regaddr | LMOVB rreg ',' addr
 *     | LMOVB rreg ',' regaddr | LMOVW freg ',' addr | LMOVW freg ',' regaddr
 *     | LMOVW fpscr ',' freg | LMOVW freg ',' fpscr | LMOVW freg ',' imm ',' fpscr
 *     | LMOVW fpscr ',' creg | LMOVW imm ',' fpscrf | LMTFSB imm ',' con
 *     | LMOVW rreg ',' imm ',' lcr | LMOVW rreg ',' creg | LMOVW rreg ',' lcr
 *     | LADDW rreg ',' sreg ',' rreg | LADDW imm ',' sreg ',' rreg
 *     | LADDW rreg ',' imm ',' rreg | LADDW rreg ',' rreg | LADDW imm ',' rreg
 *     | LLOGW rreg ',' sreg ',' rreg | LLOGW rreg ',' rreg
 *     | LSHW rreg ',' sreg ',' rreg | LSHW rreg ',' rreg
 *     | LSHW imm ',' sreg ',' rreg | LSHW imm ',' rreg | LABS rreg ',' rreg
 *     | LABS rreg | LMA rreg ',' sreg ',' rreg | LMOVW imm ',' rreg
 *     | LMOVW ximm ',' rreg | LCROP cbit ',' cbit | LCROP cbit ',' con ',' cbit
 *     | LMOVW creg ',' creg | LMOVW psr ',' creg | LMOVW lcr ',' rreg
 *     | LMOVW psr ',' rreg | LMOVW xlreg ',' rreg | LMOVW rreg ',' xlreg
 *     | LMOVW creg ',' psr | LMOVW rreg ',' psr | LBRA rel | LBRA addr
 *     | LBRA '(' xlreg ')' | LBRA ',' rel | LBRA ',' addr | LBRA ',' '(' xlreg ')'
 *     | LBRA creg ',' rel | LBRA creg ',' addr | LBRA creg ',' '(' xlreg ')'
 *     | LBRA con ',' rel | LBRA con ',' addr | LBRA con ',' '(' xlreg ')'
 *     | LBRA con ',' con ',' rel | LBRA con ',' con ',' addr
 *     | LBRA con ',' con ',' '(' xlreg ')' | LTRAP rreg ',' sreg
 *     | LTRAP imm ',' sreg | LTRAP rreg comma | LTRAP comma
 *     | LFCONV freg ',' freg | LFADD freg ',' freg | LFADD freg ',' freg ',' freg
 *     | LFMA freg ',' freg ',' freg ',' freg | LFCMP freg ',' freg
 *     | LFCMP freg ',' freg ',' creg | LCMP rreg ',' rreg | LCMP rreg ',' imm
 *     | LCMP rreg ',' rreg ',' creg | LCMP rreg ',' imm ',' creg
 *     | LRLWM imm ',' rreg ',' imm ',' rreg | LRLWM imm ',' rreg ',' mask ',' rreg
 *     | LRLWM rreg ',' rreg ',' imm ',' rreg | LRLWM rreg ',' rreg ',' mask ',' rreg
 *     | LMOVMW addr ',' rreg | LMOVMW rreg ',' addr | LXLD regaddr ',' rreg
 *     | LXLD regaddr ',' imm ',' rreg | LXST rreg ',' regaddr
 *     | LXST rreg ',' imm ',' regaddr | LXMV regaddr ',' rreg
 *     | LXMV rreg ',' regaddr | LXOP regaddr | LNOP comma | LNOP rreg comma
 *     | LNOP freg comma | LNOP ',' rreg | LNOP ',' freg | LWORD imm comma
 *     | LWORD ximm comma | LEND comma | LTEXT name ',' imm
 *     | LTEXT name ',' con ',' imm | LTEXT name ',' imm ':' imm
 *     | LTEXT name ',' con ',' imm ':' imm | LDATA name '/' con ',' imm
 *     | LDATA name '/' con ',' ximm | LDATA name '/' con ',' fimm | LRETRN comma
 * Precedence-map: '|' (1) '^' (2) '&' (3) '<<' '>>' (4) '+' '-' (5) '*' '/' '%' (6),
 * tightest unary '-' '+' '~' and '(' expr ')'.
 * Call chain: parse_expr->parse_or->parse_xor->parse_and->parse_shift->parse_add->parse_mul->parse_con.
 * Error-recovery-map: `error ;' in line; syntax error calls yyerror then skips to ';' or EOF.
 */
static long yypeek(int n);
static long yyget(void);
static void yysyntax_error(void);
static void yyexpect(long t);
static void yyskip_to_semi(void);
static void parse_comma(void);
static int isconstart(long t);
static vlong parse_expr(void);
static vlong parse_or(void);
static vlong parse_xor(void);
static vlong parse_and(void);
static vlong parse_shift(void);
static vlong parse_add(void);
static vlong parse_mul(void);
static vlong parse_con(void);
static vlong parse_offset(void);
static vlong parse_pointer(void);
static vlong parse_sreg(void);
static Gen parse_rel(void);
static Gen parse_rreg(void);
static Gen parse_lr(void);
static Gen parse_lcr(void);
static Gen parse_ctr(void);
static Gen parse_msr(void);
static Gen parse_psr(void);
static Gen parse_fpscr(void);
static Gen parse_fpscrf(void);
static Gen parse_freg(void);
static Gen parse_creg(void);
static Gen parse_cbit(void);
static Gen parse_mask(void);
static Gen parse_ximm(void);
static Gen parse_fimm(void);
static Gen parse_imm(void);
static Gen parse_regaddr(void);
static Gen parse_addr(void);
static Gen parse_name(void);
static Gen parse_xlreg(void);
static void parse_inst(void);
static void parse_line(void);
static int yyhave;
static long yytok[256];
static YYSTYPE yyval[256];
static jmp_buf yyrec;
static long yypeek(int n){if(n<0||n>=256)yysyntax_error();while(yyhave<=n){yytok[yyhave]=yylex();yyval[yyhave]=yylval;yyhave++;}return yytok[n];}
static long yyget(void){long t;int i;yypeek(0);t=yytok[0];yylval=yyval[0];for(i=1;i<yyhave;i++){yytok[i-1]=yytok[i];yyval[i-1]=yyval[i];}yyhave--;return t;}
static void yysyntax_error(void){yyerror("syntax error");longjmp(yyrec,1);}
static void yyexpect(long t){if(yyget()!=t)yysyntax_error();}
static void yyskip_to_semi(void){long t;for(;;){t=yyget();if(t==';'||t==0||t==EOF)break;}}
static void parse_comma(void){if(yypeek(0)==',')yyget();}
static int isconstart(long t){return t==LCONST||t==LVAR||t=='-'||t=='+'||t=='~'||t=='(';}
static vlong parse_or(void){vlong l,r;l=parse_xor();while(yypeek(0)=='|'){yyget();r=parse_xor();l=l|r;}return l;}
static vlong parse_xor(void){vlong l,r;l=parse_and();while(yypeek(0)=='^'){yyget();r=parse_and();l=l^r;}return l;}
static vlong parse_and(void){vlong l,r;l=parse_shift();while(yypeek(0)=='&'){yyget();r=parse_shift();l=l&r;}return l;}
static vlong parse_shift(void){vlong l,r;l=parse_add();for(;;){if(yypeek(0)=='<'&&yypeek(1)=='<'){yyget();yyget();r=parse_add();l=l<<r;continue;}if(yypeek(0)=='>'&&yypeek(1)=='>'){yyget();yyget();r=parse_add();l=l>>r;continue;}break;}return l;}
static vlong parse_add(void){vlong l,r;long t;l=parse_mul();for(;;){t=yypeek(0);if(t!='+'&&t!='-')break;yyget();r=parse_mul();if(t=='+')l=l+r;else l=l-r;}return l;}
static vlong parse_mul(void){vlong l,r;long t;l=parse_con();for(;;){t=yypeek(0);if(t!='*'&&t!='/'&&t!='%')break;yyget();r=parse_con();if(t=='*')l=l*r;else if(t=='/')l=l/r;else l=l%r;}return l;}
static vlong parse_expr(void){return parse_or();}
static vlong parse_con(void){long t;vlong v;Sym *s;t=yyget();switch(t){case LCONST:return yylval.lval;case LVAR:s=yylval.sym;return s->value;case '-':v=parse_con();return -v;case '+':v=parse_con();return v;case '~':v=parse_con();return ~v;case '(':v=parse_expr();yyexpect(')');return v;default:yysyntax_error();return 0;}}
static vlong parse_offset(void){long t;vlong v;t=yypeek(0);if(t=='+'){yyget();v=parse_con();return v;}if(t=='-'){yyget();v=parse_con();return -v;}return 0;}
static vlong parse_pointer(void){long t;t=yyget();switch(t){case LSB:case LSP:case LFP:return yylval.lval;default:yysyntax_error();return 0;}}
static vlong parse_sreg(void){long t;vlong v;t=yypeek(0);if(t==LREG){yyget();return yylval.lval;}if(t==LR){yyget();yyexpect('(');v=parse_con();yyexpect(')');if(v<0||v>=NREG)print("register value out of range\n");return v;}yysyntax_error();return 0;}
static Gen parse_rel(void){long t;Sym *s;vlong c,off;Gen g;t=yypeek(0);if(t==LLAB){yyget();s=yylval.sym;off=parse_offset();g=nullgen;g.type=D_BRANCH;g.sym=s;g.offset=s->value+off;return g;}if(t==LNAME){yyget();s=yylval.sym;off=parse_offset();if(pass==2)yyerror("undefined label: %s",s->name);g=nullgen;g.type=D_BRANCH;g.sym=s;g.offset=off;return g;}c=parse_con();yyexpect('(');t=yyget();if(t!=LPC)yysyntax_error();yyexpect(')');g=nullgen;g.type=D_BRANCH;g.offset=c+pc;return g;}
static Gen parse_rreg(void){vlong r;Gen g;r=parse_sreg();g=nullgen;g.type=D_REG;g.reg=r;return g;}
static Gen parse_lr(void){long t;Gen g;t=yyget();if(t!=LLR)yysyntax_error();g=nullgen;g.type=D_SPR;g.offset=yylval.lval;return g;}
static Gen parse_lcr(void){long t;Gen g;t=yyget();if(t!=LCR)yysyntax_error();g=nullgen;g.type=D_CREG;g.reg=NREG;return g;}
static Gen parse_ctr(void){long t;Gen g;t=yyget();if(t!=LCTR)yysyntax_error();g=nullgen;g.type=D_SPR;g.offset=yylval.lval;return g;}
static Gen parse_xlreg(void){long t;t=yypeek(0);if(t==LLR)return parse_lr();return parse_ctr();}
static Gen parse_msr(void){long t;Gen g;t=yyget();if(t!=LMSR)yysyntax_error();g=nullgen;g.type=D_MSR;return g;}
static Gen parse_psr(void){long t;vlong c;Gen g;t=yypeek(0);if(t==LSPREG){yyget();g=nullgen;g.type=D_SPR;g.offset=yylval.lval;return g;}if(t==LSPR){yyget();yyexpect('(');c=parse_con();yyexpect(')');g=nullgen;g.type=t;g.offset=c;return g;}return parse_msr();}
static Gen parse_fpscr(void){long t;Gen g;t=yyget();if(t!=LFPSCR)yysyntax_error();g=nullgen;g.type=D_FPSCR;g.reg=NREG;return g;}
static Gen parse_fpscrf(void){vlong c;Gen g;yyexpect(LFPSCR);yyexpect('(');c=parse_con();yyexpect(')');g=nullgen;g.type=D_FPSCR;g.reg=c;return g;}
static Gen parse_freg(void){long t;vlong c;Gen g;t=yypeek(0);if(t==LFREG){yyget();g=nullgen;g.type=D_FREG;g.reg=yylval.lval;return g;}yyget();if(t!=LF)yysyntax_error();yyexpect('(');c=parse_con();yyexpect(')');g=nullgen;g.type=D_FREG;g.reg=c;return g;}
static Gen parse_creg(void){long t;vlong c;Gen g;t=yypeek(0);if(t==LCREG){yyget();g=nullgen;g.type=D_CREG;g.reg=yylval.lval;return g;}yyget();if(t!=LCR)yysyntax_error();yyexpect('(');c=parse_con();yyexpect(')');g=nullgen;g.type=D_CREG;g.reg=c;return g;}
static Gen parse_cbit(void){vlong c;Gen g;c=parse_con();g=nullgen;g.type=D_REG;g.reg=c;return g;}
static Gen parse_mask(void){vlong a,b;int mb,me;ulong v;Gen g;a=parse_con();yyexpect(',');b=parse_con();g=nullgen;g.type=D_CONST;mb=a;me=b;if(mb<0||mb>31||me<0||me>31){yyerror("illegal mask start/end value(s)");mb=me=0;}if(mb<=me)v=((ulong)~0L>>mb)&(~0L<<(31-me));else v=~(((ulong)~0L>>(me+1))&(~0L<<(31-(mb-1))));g.offset=v;return g;}
static Gen parse_ximm(void){long t;Gen g;Gen a;t=yypeek(0);if(t!='$')yysyntax_error();if(yypeek(1)==LSCONST){yyget();yyget();g=nullgen;g.type=D_SCONST;memcpy(g.sval,yylval.sval,sizeof(g.sval));return g;}yyget();a=parse_addr();g=a;g.type=D_CONST;return g;}
static Gen parse_fimm(void){long t;double d;Gen g;yyexpect('$');t=yypeek(0);if(t==LFCONST){yyget();d=yylval.dval;g=nullgen;g.type=D_FCONST;g.dval=d;return g;}if(t=='-'){yyget();t=yyget();if(t!=LFCONST)yysyntax_error();d=yylval.dval;g=nullgen;g.type=D_FCONST;g.dval=-d;return g;}yysyntax_error();g=nullgen;return g;}
static Gen parse_imm(void){vlong c;Gen g;yyexpect('$');c=parse_con();g=nullgen;g.type=D_CONST;g.offset=c;return g;}
static Gen parse_name(void){Sym *s;vlong c,off,ptr;long t;Gen g;t=yypeek(0);if(t==LNAME&&yypeek(1)=='<'){yyget();s=yylval.sym;yyget();yyexpect('>');off=parse_offset();yyexpect('(');t=yyget();if(t!=LSB)yysyntax_error();yyexpect(')');g=nullgen;g.type=D_OREG;g.name=D_STATIC;g.sym=s;g.offset=off;return g;}if(t==LNAME){yyget();s=yylval.sym;off=parse_offset();yyexpect('(');ptr=parse_pointer();yyexpect(')');g=nullgen;g.type=D_OREG;g.name=ptr;g.sym=s;g.offset=off;return g;}c=parse_con();yyexpect('(');ptr=parse_pointer();yyexpect(')');g=nullgen;g.type=D_OREG;g.name=ptr;g.sym=S;g.offset=c;return g;}
static Gen parse_regaddr(void){vlong r,x;Gen g;yyexpect('(');r=parse_sreg();if(yypeek(0)=='+'){yyget();x=parse_sreg();yyexpect(')');g=nullgen;g.type=D_OREG;g.reg=r;g.xreg=x;g.offset=0;return g;}yyexpect(')');g=nullgen;g.type=D_OREG;g.reg=r;g.offset=0;return g;}
static Gen parse_addr(void){long t;t=yypeek(0);if(t==LNAME||(isconstart(t)&&t!='(')){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<yyhave;i++){st[i]=yytok[i];sv[i]=yyval[i];}{(void)parse_con();if(yypeek(0)=='('){long pin;pin=yypeek(1);if(pin==LSB||pin==LSP||pin==LFP){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}return parse_name();}if(pin==LREG||pin==LR){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];} {Gen g;vlong cc,rr;cc=parse_con();yyexpect('(');rr=parse_sreg();yyexpect(')');g=nullgen;g.type=D_OREG;g.reg=rr;g.offset=cc;return g;}}}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}return parse_name();}if(t=='(')return parse_regaddr();return parse_name();}
static void parse_inst(void){long op;Gen a,b,c;vlong s;long t;op=yypeek(0);if(op==LMOVW||op==LMOVB){yyget();op=yylval.lval;{int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<yyhave;i++){st[i]=yytok[i];sv[i]=yyval[i];}if(yypeek(0)==LREG||yypeek(0)==LR){a=parse_rreg();if(yypeek(0)==','){yyget();if(yypeek(0)==LCREG||yypeek(0)==LCR||yypeek(0)==LSPREG||yypeek(0)==LSPR||yypeek(0)==LMSR||yypeek(0)==LLR||yypeek(0)==LCTR||yypeek(0)==LFPSCR){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}goto movw_reg_first;}}}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}movw_reg_first:;if(0){}t=yypeek(0);if(t==LREG||t==LR||t==LFREG||t==LF){int sh2,i2;long st2[256];YYSTYPE sv2[256];sh2=yyhave;for(i2=0;i2<yyhave;i2++){st2[i2]=yytok[i2];sv2[i2]=yyval[i2];}if(t==LREG||t==LR)a=parse_rreg();else a=parse_freg();if(yypeek(0)==','){yyget();t=yypeek(0);if(t==LCREG||t==LCR||t==LSPREG||t==LSPR||t==LMSR||t==LLR||t==LCTR||t==LFPSCR||t==LFP||t==LF||t=='('||t==LNAME||isconstart(t)){yyhave=sh2;for(i2=0;i2<sh2;i2++){yytok[i2]=st2[i2];yyval[i2]=sv2[i2];}goto movw_general;}}yyhave=sh2;for(i2=0;i2<sh2;i2++){yytok[i2]=st2[i2];yyval[i2]=sv2[i2];}}movw_general:;{Gen f1,f2;int isf1,isf2;isf1=(yypeek(0)==LFREG||yypeek(0)==LF);if(isf1)f1=parse_freg();else if(yypeek(0)==LREG||yypeek(0)==LR)f1=parse_rreg();else if(yypeek(0)==LFPSCR){if(yypeek(1)=='(')f1=parse_fpscrf();else f1=parse_fpscr();}else if(yypeek(0)==LCREG||yypeek(0)==LCR)f1=parse_creg();else if(yypeek(0)==LSPREG||yypeek(0)==LSPR||yypeek(0)==LMSR)f1=parse_psr();else if(yypeek(0)==LLR)f1=parse_lr();else if(yypeek(0)==LCTR)f1=parse_ctr();else if(yypeek(0)=='$')f1=parse_imm();else f1=parse_addr();yyexpect(',');if(yypeek(0)==LFPSCR&&yypeek(1)!='('){b=parse_fpscr();outcode(op,&f1,NREG,&b);return;}if(yypeek(0)==LFPSCR){b=parse_fpscrf();outcode(op,&f1,NREG,&b);return;}if((yypeek(0)==LCREG||yypeek(0)==LCR)){b=parse_creg();outcode(op,&f1,NREG,&b);return;}if(yypeek(0)==LSPREG||yypeek(0)==LSPR||yypeek(0)==LMSR||yypeek(0)==LLR||yypeek(0)==LCTR){if(isf1&&(f1.type==D_FREG)){b=parse_psr();if(b.type==D_SPR||b.type==D_MSR){outcode(op,&f1,NREG,&b);return;}}b=parse_psr();if(b.type==D_SPR||b.type==D_MSR){outcode(op,&f1,NREG,&b);return;}}if(yypeek(0)=='$'&&op==LMOVW){b=parse_imm();if(yypeek(0)==','){yyget();c=parse_fpscr();outgcode(op,&f1,NREG,&b,&c);return;}outcode(op,&f1,NREG,&b);return;}isf2=(yypeek(0)==LFREG||yypeek(0)==LF);if(isf2){b=parse_freg();outcode(op,&f1,NREG,&b);return;}if(yypeek(0)==LREG||yypeek(0)==LR){b=parse_rreg();outcode(op,&f1,NREG,&b);return;}b=parse_addr();if(yypeek(0)==','){yyget();if(yypeek(0)==LCREG||yypeek(0)==LCR){c=parse_creg();outgcode(op,&f1,NREG,&b,&c);return;}c=parse_regaddr();outcode(op,&f1,NREG,&c);return;}outcode(op,&f1,NREG,&b);return;}}switch(op){case LABS:yyget();op=yylval.lval;a=parse_rreg();yyexpect(',');b=parse_rreg();outcode(op,&a,NREG,&b);return;case LLOGW:case LSHW:case LADDW:{yyget();op=yylval.lval;if(yypeek(0)=='$'){a=parse_imm();yyexpect(',');if((yypeek(0)==LREG||yypeek(0)==LR)&&yypeek(1)==','){s=parse_sreg();yyexpect(',');b=parse_rreg();outcode(op,&a,s,&b);return;}b=parse_rreg();outcode(op,&a,NREG,&b);return;}a=parse_rreg();yyexpect(',');if(yypeek(0)=='$'){b=parse_imm();yyexpect(',');c=parse_rreg();outgcode(op,&a,NREG,&b,&c);return;}if((yypeek(0)==LREG||yypeek(0)==LR)){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<yyhave;i++){st[i]=yytok[i];sv[i]=yyval[i];}s=parse_sreg();if(yypeek(0)==','){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}s=parse_sreg();yyexpect(',');b=parse_rreg();outcode(op,&a,s,&b);return;}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}b=parse_rreg();outcode(op,&a,NREG,&b);return;}case LMA:yyget();op=yylval.lval;a=parse_rreg();yyexpect(',');s=parse_sreg();yyexpect(',');b=parse_rreg();outcode(op,&a,s,&b);return;case LCROP:yyget();op=yylval.lval;a=parse_cbit();yyexpect(',');if(yypeek(0)==LCONST||yypeek(0)==LVAR||yypeek(0)=='-'||yypeek(0)=='+'||yypeek(0)=='~'||yypeek(0)=='('){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<yyhave;i++){st[i]=yytok[i];sv[i]=yyval[i];}s=parse_con();if(yypeek(0)==','){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}s=parse_con();yyexpect(',');b=parse_cbit();outcode(op,&a,s,&b);return;}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}b=parse_cbit();outcode(op,&a,b.reg,&b);return;case LBRA:{yyget();op=yylval.lval;if(yypeek(0)==','){yyget();if(yypeek(0)=='('){yyget();a=parse_xlreg();yyexpect(')');outcode(op,&nullgen,NREG,&a);return;}if(isconstart(yypeek(0))||yypeek(0)==LNAME||yypeek(0)==LLAB){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<yyhave;i++){st[i]=yytok[i];sv[i]=yyval[i];}{vlong cc;if(isconstart(yypeek(0))){cc=parse_con();if(yypeek(0)=='('){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}a=parse_addr();outcode(op,&nullgen,NREG,&a);return;}}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}a=parse_rel();outcode(op,&nullgen,NREG,&a);return;}a=parse_addr();outcode(op,&nullgen,NREG,&a);return;}if(yypeek(0)=='('){yyget();a=parse_xlreg();yyexpect(')');outcode(op,&nullgen,NREG,&a);return;}if(yypeek(0)==LCREG||yypeek(0)==LCR){a=parse_creg();yyexpect(',');if(yypeek(0)=='('){yyget();b=parse_xlreg();yyexpect(')');outcode(op,&a,NREG,&b);return;}b=parse_addr();if(yypeek(0)==','||yypeek(0)==';'||yypeek(0)==0||yypeek(0)==EOF){if(yypeek(0)!=','){outcode(op,&a,NREG,&b);return;}}b=parse_rel();outcode(op,&a,NREG,&b);return;}if(isconstart(yypeek(0))){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<yyhave;i++){st[i]=yytok[i];sv[i]=yyval[i];}s=parse_con();if(yypeek(0)==','){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}s=parse_con();yyexpect(',');if(yypeek(0)=='('){yyget();if(yypeek(0)==LLR||yypeek(0)==LCTR){b=parse_xlreg();yyexpect(')');outcode(op,&nullgen,s,&b);return;}}if(isconstart(yypeek(0))){vlong s2;s2=parse_con();yyexpect(',');if(yypeek(0)=='('){yyget();b=parse_xlreg();yyexpect(')');{Gen gg;gg=nullgen;gg.type=D_CONST;gg.offset=s;outcode(op,&gg,s2,&b);return;}}b=parse_rel();{Gen gg;gg=nullgen;gg.type=D_CONST;gg.offset=s;outcode(op,&gg,s2,&b);return;}}b=parse_addr();{Gen gg;gg=nullgen;gg.type=D_CONST;gg.offset=s;outcode(op,&gg,NREG,&b);return;}}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}a=parse_rel();outcode(op,&nullgen,NREG,&a);return;a=parse_addr();outcode(op,&nullgen,NREG,&a);return;}case LTRAP:yyget();op=yylval.lval;if(yypeek(0)==','){yyget();outcode(op,&nullgen,NREG,&nullgen);return;}if(yypeek(0)=='$'){a=parse_imm();yyexpect(',');s=parse_sreg();outcode(op,&a,s,&nullgen);return;}a=parse_rreg();if(yypeek(0)==','){yyget();if(yypeek(0)==';'||yypeek(0)==0||yypeek(0)==EOF){outcode(op,&a,NREG,&nullgen);return;}s=parse_sreg();outcode(op,&a,s,&nullgen);return;}parse_comma();outcode(op,&a,NREG,&nullgen);return;case LFCONV:case LFADD:case LFCMP:yyget();op=yylval.lval;a=parse_freg();yyexpect(',');b=parse_freg();if(yypeek(0)==','){yyget();if(op==LFMA){c=parse_freg();yyexpect(',');{Gen d;d=parse_freg();outgcode(op,&a,b.reg,&c,&d);return;}}c=parse_freg();if(op==LFCMP&&yypeek(0)!=','&&yypeek(0)!=';'&&yypeek(0)!=0&&yypeek(0)!=EOF){}if(op==LFCMP&&(c.type==D_CREG||yypeek(0)==',')){if(c.type!=D_CREG){Gen cr;cr=parse_creg();outcode(op,&a,cr.reg,&b);return;}}outcode(op,&a,b.reg,&c);return;}outcode(op,&a,NREG,&b);return;case LFMA:yyget();op=yylval.lval;a=parse_freg();yyexpect(',');b=parse_freg();yyexpect(',');c=parse_freg();yyexpect(',');{Gen d;d=parse_freg();outgcode(op,&a,b.reg,&c,&d);return;}case LCMP:yyget();op=yylval.lval;a=parse_rreg();yyexpect(',');if(yypeek(0)=='$'){b=parse_imm();if(yypeek(0)==','){yyget();c=parse_creg();outcode(op,&a,c.reg,&b);return;}outcode(op,&a,NREG,&b);return;}b=parse_rreg();if(yypeek(0)==','){yyget();c=parse_creg();outcode(op,&a,c.reg,&b);return;}outcode(op,&a,NREG,&b);return;case LRLWM:yyget();op=yylval.lval;if(yypeek(0)=='$'){a=parse_imm();}else{a=parse_rreg();}yyexpect(',');b=parse_rreg();yyexpect(',');if((yypeek(0)==LCONST||yypeek(0)==LVAR||yypeek(0)=='-'||yypeek(0)=='+'||yypeek(0)=='~'||yypeek(0)=='(')&&yypeek(1)==',' ){c=parse_imm();yyexpect(',');{Gen d;d=parse_rreg();outgcode(op,&a,b.reg,&c,&d);return;}}c=parse_mask();yyexpect(',');{Gen d;d=parse_rreg();outgcode(op,&a,b.reg,&c,&d);return;}case LMOVMW:yyget();op=yylval.lval;if(yypeek(0)==LREG||yypeek(0)==LR){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<yyhave;i++){st[i]=yytok[i];sv[i]=yyval[i];}a=parse_rreg();if(yypeek(0)==','){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}a=parse_rreg();yyexpect(',');b=parse_addr();outcode(op,&a,NREG,&b);return;}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}a=parse_addr();yyexpect(',');b=parse_rreg();outcode(op,&a,NREG,&b);return;case LXLD:case LXST:case LXMV:case LXOP:{long o2;o2=op;yyget();op=yylval.lval;if(op==LXOP){a=parse_regaddr();outcode(op,&a,NREG,&nullgen);return;}if(op==LXLD){a=parse_regaddr();yyexpect(',');if(yypeek(0)=='$'||((yypeek(0)==LCONST||yypeek(0)==LVAR)&&0)){ }if(isconstart(yypeek(0))||yypeek(0)=='$'){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<yyhave;i++){st[i]=yytok[i];sv[i]=yyval[i];}if(yypeek(0)=='$'){b=parse_imm();if(yypeek(0)==','){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}a=parse_regaddr();yyexpect(',');b=parse_imm();yyexpect(',');c=parse_rreg();outgcode(op,&a,NREG,&b,&c);return;}}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}b=parse_rreg();outcode(op,&a,NREG,&b);return;}if(op==LXST){a=parse_rreg();yyexpect(',');if(yypeek(0)=='('){b=parse_regaddr();if(yypeek(0)==';'||yypeek(0)==0||yypeek(0)==EOF){outcode(op,&a,NREG,&b);return;}}if(yypeek(0)=='$'||isconstart(yypeek(0))){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<yyhave;i++){st[i]=yytok[i];sv[i]=yyval[i];}if(yypeek(0)=='$'){b=parse_imm();if(yypeek(0)==','){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}a=parse_rreg();yyexpect(',');b=parse_imm();yyexpect(',');c=parse_regaddr();outgcode(op,&a,NREG,&b,&c);return;}}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}b=parse_regaddr();outcode(op,&a,NREG,&b);return;}if(op==LXMV){if(yypeek(0)=='('){a=parse_regaddr();yyexpect(',');b=parse_rreg();outcode(op,&a,NREG,&b);return;}a=parse_rreg();yyexpect(',');b=parse_regaddr();outcode(op,&a,NREG,&b);return;}yysyntax_error();return;}case LNOP:yyget();op=yylval.lval;if(yypeek(0)==','){yyget();parse_comma();outcode(op,&nullgen,NREG,&nullgen);return;}if(yypeek(0)==LREG||yypeek(0)==LR){a=parse_rreg();parse_comma();outcode(op,&a,NREG,&nullgen);return;}if(yypeek(0)==LFREG||yypeek(0)==LF){a=parse_freg();parse_comma();outcode(op,&a,NREG,&nullgen);return;}if(yypeek(0)==';'||yypeek(0)==0||yypeek(0)==EOF){parse_comma();outcode(op,&nullgen,NREG,&nullgen);return;}yysyntax_error();return;case LWORD:yyget();op=yylval.lval;if(yypeek(0)=='$'&&(yypeek(1)==LSCONST||0)){b=parse_ximm();}else if(yypeek(0)=='$'){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<sh;i++){st[i]=yytok[i];sv[i]=yyval[i];} {Gen xa;xa=parse_ximm();if(yypeek(0)==','||yypeek(0)==';'||yypeek(0)==0||yypeek(0)==EOF){yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}b=parse_ximm();if(op==ADWORD&&b.type==D_CONST)b.type=D_DCONST;parse_comma();outcode(op,&b,NREG,&nullgen);return;}}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}b=parse_imm();if(op==ADWORD&&b.type==D_CONST)b.type=D_DCONST;parse_comma();outcode(op,&b,NREG,&nullgen);return;}else{b=parse_imm();if(op==ADWORD&&b.type==D_CONST)b.type=D_DCONST;parse_comma();outcode(op,&b,NREG,&nullgen);return;}parse_comma();if(op==ADWORD&&b.type==D_CONST)b.type=D_DCONST;outcode(op,&b,NREG,&nullgen);return;case LEND:yyget();op=yylval.lval;parse_comma();outcode(op,&nullgen,NREG,&nullgen);return;case LTEXT:yyget();op=yylval.lval;a=parse_name();yyexpect(',');if(yypeek(0)=='$'){b=parse_imm();if(yypeek(0)==':'){yyget();c=parse_imm();outgcode(op,&a,NREG,&c,&b);return;}outcode(op,&a,NREG,&b);return;}s=parse_con();yyexpect(',');b=parse_imm();if(yypeek(0)==':'){yyget();c=parse_imm();outgcode(op,&a,s,&c,&b);return;}outcode(op,&a,s,&b);return;case LDATA:yyget();op=yylval.lval;a=parse_name();yyexpect('/');s=parse_con();yyexpect(',');if(yypeek(0)=='$'&&(yypeek(1)==LFCONST||(yypeek(1)=='-'&&yypeek(2)==LFCONST))){b=parse_fimm();outcode(op,&a,s,&b);return;}if(yypeek(0)=='$'){int sh,i;long st[256];YYSTYPE sv[256];sh=yyhave;for(i=0;i<sh;i++){st[i]=yytok[i];sv[i]=yyval[i];}if(yypeek(1)==LSCONST){b=parse_ximm();outcode(op,&a,s,&b);return;}yyhave=sh;for(i=0;i<sh;i++){yytok[i]=st[i];yyval[i]=sv[i];}}if(yypeek(0)=='$'){b=parse_imm();outcode(op,&a,s,&b);return;}b=parse_ximm();outcode(op,&a,s,&b);return;case LRETRN:yyget();op=yylval.lval;parse_comma();outcode(op,&nullgen,NREG,&nullgen);return;default:yysyntax_error();return;}}
static void parse_line(void){long t;Sym *s;vlong v;if(setjmp(yyrec)){yyskip_to_semi();return;}for(;;){if(yypeek(0)==LLAB&&yypeek(1)==':'){yyget();s=yylval.sym;yyget();if(s->value!=pc)yyerror("redeclaration of %s",s->name);s->value=pc;continue;}if(yypeek(0)==LNAME&&yypeek(1)==':'){yyget();s=yylval.sym;yyget();s->type=LLAB;s->value=pc;continue;}break;}t=yypeek(0);if(t==';'){yyget();return;}if(t==0||t==EOF)return;if(t==LNAME&&yypeek(1)=='='){yyget();s=yylval.sym;yyget();v=parse_expr();yyexpect(';');s->type=LVAR;s->value=v;return;}if(t==LVAR&&yypeek(1)=='='){yyget();s=yylval.sym;yyget();v=parse_expr();yyexpect(';');if(s->value!=v)yyerror("redeclaration of %s",s->name);s->value=v;return;}if(t==LSCHED){yyget();v=yylval.lval;yyexpect(';');nosched=v;return;}parse_inst();t=yyget();if(t!=';')yysyntax_error();}
int yyparse(void){long t;yyhave=0;for(;;){t=yypeek(0);if(t==0||t==EOF)break;parse_line();}return 0;}
