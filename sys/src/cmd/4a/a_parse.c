#include "a.h"
#include "a_parse.h"

YYSTYPE yylval;
/*
 * Hand-written recursive-descent replacement for a.y (LALR, 4a/arm).
 * Original precedence low->high: '|' '^' '&' '<' '>' '+' '-' '*' '/' '%'.
 * prog: | prog line
 * line: LLAB ':' line | LNAME ':' line | LNAME '=' expr ';' | LVAR '=' expr ';'
 *     | LSCHED ';' | ';' | inst ';' | error ';'
 * inst: LTYPE1 imr ',' sreg ',' reg | LTYPE1 imr ',' reg
 *     | LTYPE2 imr ',' sreg ',' imr | LTYPE2 imr ',' imr
 *     | LTYPE3 lgen ',' gen | LTYPE4 comma | LTYPE5 vlgen ',' vgen
 *     | LTYPE6 reg ',' sreg comma | LTYPE6 reg ',' sreg ',' reg
 *     | LTYPE7 comma rel | LTYPE7 comma nireg | LTYPE8 comma rel
 *     | LTYPE8 comma nireg | LTYPE8 sreg ',' nireg
 *     | LTYPE9 gen ',' rel | LTYPE9 gen ',' sreg ',' rel
 *     | LTYPEA gen ',' rel | LTYPEB name ',' imm | LTYPEB name ',' con ',' imm
 *     | LTYPEC name '/' con ',' ximm | LTYPED freg ',' freg
 *     | LTYPEE freg ',' freg | LTYPEE freg ',' LFREG ',' freg
 *     | LTYPEF freg ',' LFREG comma | LTYPEG comma rel
 *     | LTYPEH comma ximm | LTYPEI comma | LTYPEI ',' vgen | LTYPEI vgen comma
 *     | LTYPEJ comma | LTYPEJ vgen ',' vgen
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
static int isvconst(vlong con);
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
static Gen parse_reg(void);
static Gen parse_freg(void);
static Gen parse_mreg(void);
static Gen parse_fcreg(void);
static Gen parse_fgen(void);
static Gen parse_gen(void);
static Gen parse_oreg(void);
static Gen parse_name(void);
static Gen parse_imm(void);
static Gen parse_ximm(void);
static Gen parse_imr(void);
static Gen parse_ireg(void);
static Gen parse_nireg(void);
static Gen parse_lgen(void);
static Gen parse_vlgen(void);
static Gen parse_vgen(void);
static void parse_inst(void);
static void parse_line(void);
static int yyhave;
static long yytok[256];
static YYSTYPE yyval[256];
static jmp_buf yyrec;
static long yypeek(int n)
{
    if (n < 0 || n >= 256)
        yysyntax_error();
    while (yyhave <= n)
    {
        yytok[yyhave] = yylex();
        yyval[yyhave] = yylval;
        yyhave++;
    }
    return yytok[n];
}
static long yyget(void)
{
    long t;
    int i;
    yypeek(0);
    t = yytok[0];
    yylval = yyval[0];
    for (i = 1; i < yyhave; i++)
    {
        yytok[i - 1] = yytok[i];
        yyval[i - 1] = yyval[i];
    }
    yyhave--;
    return t;
}
static void yysyntax_error(void)
{
    yyerror("syntax error");
    longjmp(yyrec, 1);
}
static void yyexpect(long t)
{
    if (yyget() != t)
        yysyntax_error();
}
static void yyskip_to_semi(void)
{
    long t;
    for (;;)
    {
        t = yyget();
        if (t == ';' || t == 0 || t == EOF)
            break;
    }
}
static void parse_comma(void)
{
    if (yypeek(0) == ',')
        yyget();
}
static int isconstart(long t) { return t == LCONST || t == LVAR || t == '-' || t == '+' || t == '~' || t == '('; }
static int isvconst(vlong con)
{
    long l;
    l = con;
    return (vlong)l != con;
}
static vlong parse_or(void)
{
    vlong l, r;
    l = parse_xor();
    while (yypeek(0) == '|')
    {
        yyget();
        r = parse_xor();
        l = l | r;
    }
    return l;
}
static vlong parse_xor(void)
{
    vlong l, r;
    l = parse_and();
    while (yypeek(0) == '^')
    {
        yyget();
        r = parse_and();
        l = l ^ r;
    }
    return l;
}
static vlong parse_and(void)
{
    vlong l, r;
    l = parse_shift();
    while (yypeek(0) == '&')
    {
        yyget();
        r = parse_shift();
        l = l & r;
    }
    return l;
}
static vlong parse_shift(void)
{
    vlong l, r;
    l = parse_add();
    for (;;)
    {
        if (yypeek(0) == '<' && yypeek(1) == '<')
        {
            yyget();
            yyget();
            r = parse_add();
            l = l << r;
            continue;
        }
        if (yypeek(0) == '>' && yypeek(1) == '>')
        {
            yyget();
            yyget();
            r = parse_add();
            l = l >> r;
            continue;
        }
        break;
    }
    return l;
}
static vlong parse_add(void)
{
    vlong l, r;
    long t;
    l = parse_mul();
    for (;;)
    {
        t = yypeek(0);
        if (t != '+' && t != '-')
            break;
        yyget();
        r = parse_mul();
        if (t == '+')
            l = l + r;
        else
            l = l - r;
    }
    return l;
}
static vlong parse_mul(void)
{
    vlong l, r;
    long t;
    l = parse_con();
    for (;;)
    {
        t = yypeek(0);
        if (t != '*' && t != '/' && t != '%')
            break;
        yyget();
        r = parse_con();
        if (t == '*')
            l = l * r;
        else if (t == '/')
            l = l / r;
        else
            l = l % r;
    }
    return l;
}
static vlong parse_expr(void) { return parse_or(); }
static vlong parse_con(void)
{
    long t;
    vlong v;
    Sym *s;
    t = yyget();
    switch (t)
    {
    case LCONST:
        return yylval.lval;
    case LVAR:
        s = yylval.sym;
        return s->value;
    case '-':
        v = parse_con();
        return -v;
    case '+':
        v = parse_con();
        return v;
    case '~':
        v = parse_con();
        return ~v;
    case '(':
        v = parse_expr();
        yyexpect(')');
        return v;
    default:
        yysyntax_error();
        return 0;
    }
}
static vlong parse_offset(void)
{
    long t;
    vlong v;
    t = yypeek(0);
    if (t == '+')
    {
        yyget();
        v = parse_con();
        return v;
    }
    if (t == '-')
    {
        yyget();
        v = parse_con();
        return -v;
    }
    return 0;
}
static vlong parse_pointer(void)
{
    long t;
    t = yyget();
    switch (t)
    {
    case LSB:
    case LSP:
    case LFP:
        return yylval.lval;
    default:
        yysyntax_error();
        return 0;
    }
}
static vlong parse_sreg(void)
{
    long t;
    vlong v;
    t = yypeek(0);
    if (t == LREG)
    {
        yyget();
        return yylval.lval;
    }
    if (t == LR)
    {
        yyget();
        yyexpect('(');
        v = parse_con();
        yyexpect(')');
        if (v < 0 || v >= NREG)
            print("register value out of range\n");
        return v;
    }
    yysyntax_error();
    return 0;
}
static Gen parse_rel(void)
{
    long t;
    Sym *s;
    vlong c, off;
    Gen g;
    t = yypeek(0);
    if (t == LLAB)
    {
        yyget();
        s = yylval.sym;
        off = parse_offset();
        g = nullgen;
        g.type = D_BRANCH;
        g.sym = s;
        g.offset = s->value + off;
        return g;
    }
    if (t == LNAME)
    {
        yyget();
        s = yylval.sym;
        off = parse_offset();
        if (pass == 2)
            yyerror("undefined label: %s", s->name);
        g = nullgen;
        g.type = D_BRANCH;
        g.sym = s;
        g.offset = off;
        return g;
    }
    c = parse_con();
    yyexpect('(');
    t = yyget();
    if (t != LPC)
        yysyntax_error();
    yyexpect(')');
    g = nullgen;
    g.type = D_BRANCH;
    g.offset = c + pc;
    return g;
}
static Gen parse_reg(void)
{
    vlong r;
    Gen g;
    r = parse_sreg();
    g = nullgen;
    g.type = D_REG;
    g.reg = r;
    return g;
}
static Gen parse_freg(void)
{
    long t;
    vlong c;
    Gen g;
    t = yypeek(0);
    if (t == LFREG)
    {
        yyget();
        g = nullgen;
        g.type = D_FREG;
        g.reg = yylval.lval;
        return g;
    }
    yyget();
    if (t != LF)
        yysyntax_error();
    yyexpect('(');
    c = parse_con();
    yyexpect(')');
    g = nullgen;
    g.type = D_FREG;
    g.reg = c;
    return g;
}
static Gen parse_mreg(void)
{
    long t;
    vlong c;
    Gen g;
    t = yypeek(0);
    if (t == LMREG)
    {
        yyget();
        g = nullgen;
        g.type = D_MREG;
        g.reg = yylval.lval;
        return g;
    }
    yyget();
    if (t != LM)
        yysyntax_error();
    yyexpect('(');
    c = parse_con();
    yyexpect(')');
    g = nullgen;
    g.type = D_MREG;
    g.reg = c;
    return g;
}
static Gen parse_fcreg(void)
{
    long t;
    vlong c;
    Gen g;
    t = yypeek(0);
    if (t == LFCREG)
    {
        yyget();
        g = nullgen;
        g.type = D_FCREG;
        g.reg = yylval.lval;
        return g;
    }
    yyget();
    if (t != LFCR)
        yysyntax_error();
    yyexpect('(');
    c = parse_con();
    yyexpect(')');
    g = nullgen;
    g.type = D_FCREG;
    g.reg = c;
    return g;
}
static Gen parse_fgen(void)
{
    Gen g;
    g = parse_freg();
    return g;
}
static Gen parse_name(void)
{
    Sym *s;
    vlong c, off, ptr;
    long t;
    Gen g;
    t = yypeek(0);
    if (t == LNAME && yypeek(1) == '<')
    {
        yyget();
        s = yylval.sym;
        yyget();
        yyexpect('>');
        off = parse_offset();
        yyexpect('(');
        t = yyget();
        if (t != LSB)
            yysyntax_error();
        yyexpect(')');
        g = nullgen;
        g.type = D_OREG;
        g.name = D_STATIC;
        g.sym = s;
        g.offset = off;
        return g;
    }
    if (t == LNAME)
    {
        yyget();
        s = yylval.sym;
        off = parse_offset();
        yyexpect('(');
        ptr = parse_pointer();
        yyexpect(')');
        g = nullgen;
        g.type = D_OREG;
        g.name = ptr;
        g.sym = s;
        g.offset = off;
        return g;
    }
    c = parse_con();
    yyexpect('(');
    ptr = parse_pointer();
    yyexpect(')');
    g = nullgen;
    g.type = D_OREG;
    g.name = ptr;
    g.sym = S;
    g.offset = c;
    return g;
}
static Gen parse_oreg(void)
{
    long t;
    vlong c, r;
    Gen g;
    Sym *s;
    vlong off;
    t = yypeek(0);
    if (t == LNAME)
    {
        int savehave, i;
        long svt[256];
        YYSTYPE svv[256];
        savehave = yyhave;
        for (i = 0; i < yyhave; i++)
        {
            svt[i] = yytok[i];
            svv[i] = yyval[i];
        }
        yyget();
        s = yylval.sym;
        off = parse_offset();
        USED(s);
        USED(off);
        if (yypeek(0) == '(')
        {
            yyhave = savehave;
            for (i = 0; i < savehave; i++)
            {
                yytok[i] = svt[i];
                yyval[i] = svv[i];
            }
            g = parse_name();
            if (yypeek(0) == '(')
            {
                yyget();
                r = parse_sreg();
                yyexpect(')');
                g.type = D_OREG;
                g.reg = r;
                return g;
            }
            return g;
        }
        yyhave = savehave;
        for (i = 0; i < savehave; i++)
        {
            yytok[i] = svt[i];
            yyval[i] = svv[i];
        }
    }
    if (t == '(')
    {
        yyget();
        r = parse_sreg();
        yyexpect(')');
        g = nullgen;
        g.type = D_OREG;
        g.reg = r;
        g.offset = 0;
        return g;
    }
    c = parse_con();
    yyexpect('(');
    r = parse_sreg();
    yyexpect(')');
    g = nullgen;
    g.type = D_OREG;
    g.reg = r;
    g.offset = c;
    return g;
}
static Gen parse_gen(void)
{
    long t;
    t = yypeek(0);
    if (t == LREG || t == LR)
        return parse_reg();
    if (isconstart(t))
    {
        int sh, i;
        long st[256];
        YYSTYPE sv[256];
        sh = yyhave;
        for (i = 0; i < yyhave; i++)
        {
            st[i] = yytok[i];
            sv[i] = yyval[i];
        }
        {
            vlong c;
            c = parse_con();
            USED(c);
            if (yypeek(0) == '(')
            {
                yyhave = sh;
                for (i = 0; i < sh; i++)
                {
                    yytok[i] = st[i];
                    yyval[i] = sv[i];
                }
                return parse_oreg();
            }
            {
                Gen g;
                g = nullgen;
                g.type = D_OREG;
                g.offset = c;
                return g;
            }
        }
    }
    return parse_oreg();
}
static Gen parse_imm(void)
{
    vlong c;
    Gen g;
    yyexpect('$');
    c = parse_con();
    g = nullgen;
    if (isvconst(c))
        g.type = D_VCONST;
    else
        g.type = D_CONST;
    g.offset = c;
    return g;
}
static Gen parse_ximm(void)
{
    long t;
    vlong c;
    Gen g;
    Gen o;
    t = yypeek(0);
    if (t != '$')
        yysyntax_error();
    if (yypeek(1) == LSCONST)
    {
        yyget();
        yyget();
        g = nullgen;
        g.type = D_SCONST;
        memcpy(g.sval, yylval.sval, sizeof(g.sval));
        return g;
    }
    if (yypeek(1) == LFCONST)
    {
        yyget();
        yyget();
        g = nullgen;
        g.type = D_FCONST;
        g.dval = yylval.dval;
        return g;
    }
    if (yypeek(1) == '-' && yypeek(2) == LFCONST)
    {
        yyget();
        yyget();
        yyget();
        g = nullgen;
        g.type = D_FCONST;
        g.dval = -yylval.dval;
        return g;
    }
    if (yypeek(1) == LNAME || yypeek(1) == '(')
    {
        yyget();
        if (yypeek(0) == LNAME || yypeek(0) == '(' || isconstart(yypeek(0)))
        {
            int sh, i;
            long st[256];
            YYSTYPE sv[256];
            sh = yyhave;
            for (i = 0; i < yyhave; i++)
            {
                st[i] = yytok[i];
                sv[i] = yyval[i];
            }
            o = parse_oreg();
            if (yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF || yypeek(0) == ',')
            {
                g = o;
                g.type = D_CONST;
                return g;
            }
            yyhave = sh;
            for (i = 0; i < sh; i++)
            {
                yytok[i] = st[i];
                yyval[i] = sv[i];
            }
        }
    }
    yyget();
    c = parse_con();
    g = nullgen;
    if (isvconst(c))
        g.type = D_VCONST;
    else
        g.type = D_CONST;
    g.offset = c;
    return g;
}
static Gen parse_imr(void)
{
    long t;
    t = yypeek(0);
    if (t == '$')
        return parse_imm();
    return parse_reg();
}
static Gen parse_ireg(void)
{
    vlong r;
    Gen g;
    yyexpect('(');
    r = parse_sreg();
    yyexpect(')');
    g = nullgen;
    g.type = D_OREG;
    g.reg = r;
    g.offset = 0;
    return g;
}
static Gen parse_nireg(void)
{
    long t;
    t = yypeek(0);
    if (t == '(')
        return parse_ireg();
    if (t == LCONST || t == LVAR || t == '-' || t == '+' || t == '~')
    {
        vlong c;
        c = parse_con();
        if (c != 0)
            yyerror("offset must be zero");
        return parse_ireg();
    }
    return parse_name();
}
static Gen parse_lgen(void)
{
    long t;
    t = yypeek(0);
    if (t == '$')
        return parse_ximm();
    return parse_gen();
}
static Gen parse_vlgen(void)
{
    long t;
    Gen g;
    t = yypeek(0);
    if (t == LHI)
    {
        yyget();
        g = nullgen;
        g.type = D_HI;
        return g;
    }
    if (t == LLO)
    {
        yyget();
        g = nullgen;
        g.type = D_LO;
        return g;
    }
    if (t == LMREG || t == LM)
        return parse_mreg();
    if (t == LFCREG || t == LFCR)
        return parse_fcreg();
    if (t == LFREG || t == LF)
        return parse_fgen();
    return parse_lgen();
}
static Gen parse_vgen(void)
{
    long t;
    Gen g;
    t = yypeek(0);
    if (t == LHI)
    {
        yyget();
        g = nullgen;
        g.type = D_HI;
        return g;
    }
    if (t == LLO)
    {
        yyget();
        g = nullgen;
        g.type = D_LO;
        return g;
    }
    if (t == LMREG || t == LM)
        return parse_mreg();
    if (t == LFCREG || t == LFCR)
        return parse_fcreg();
    if (t == LFREG || t == LF)
        return parse_fgen();
    return parse_gen();
}
static void parse_inst(void)
{
    long op;
    Gen a, b;
    vlong s;
    long t;
    op = yypeek(0);
    switch (op)
    {
    case LTYPE1:
        yyget();
        op = yylval.lval;
        a = parse_imr();
        yyexpect(',');
        {
            int sh, i;
            long st[256];
            YYSTYPE sv[256];
            sh = yyhave;
            for (i = 0; i < yyhave; i++)
            {
                st[i] = yytok[i];
                sv[i] = yyval[i];
            }
            s = parse_sreg();
            USED(s);
            if (yypeek(0) == ',')
            {
                yyhave = sh;
                for (i = 0; i < sh; i++)
                {
                    yytok[i] = st[i];
                    yyval[i] = sv[i];
                }
                a = parse_imr();
                yyexpect(',');
                s = parse_sreg();
                yyexpect(',');
                b = parse_reg();
                outcode(op, &a, s, &b);
                return;
            }
            yyhave = sh;
            for (i = 0; i < sh; i++)
            {
                yytok[i] = st[i];
                yyval[i] = sv[i];
            }
        }
        b = parse_reg();
        outcode(op, &a, NREG, &b);
        return;
    case LTYPE2:
        yyget();
        op = yylval.lval;
        a = parse_imr();
        yyexpect(',');
        {
            int sh, i;
            long st[256];
            YYSTYPE sv[256];
            sh = yyhave;
            for (i = 0; i < yyhave; i++)
            {
                st[i] = yytok[i];
                sv[i] = yyval[i];
            }
            s = parse_sreg();
            USED(s);
            if (yypeek(0) == ',')
            {
                yyhave = sh;
                for (i = 0; i < sh; i++)
                {
                    yytok[i] = st[i];
                    yyval[i] = sv[i];
                }
                a = parse_imr();
                yyexpect(',');
                s = parse_sreg();
                yyexpect(',');
                b = parse_imr();
                outcode(op, &a, s, &b);
                return;
            }
            yyhave = sh;
            for (i = 0; i < sh; i++)
            {
                yytok[i] = st[i];
                yyval[i] = sv[i];
            }
        }
        b = parse_imr();
        outcode(op, &a, NREG, &b);
        return;
    case LTYPE3:
        yyget();
        op = yylval.lval;
        a = parse_lgen();
        yyexpect(',');
        b = parse_gen();
        if (!isreg(&a) && !isreg(&b))
            print("one side must be register\n");
        outcode(op, &a, NREG, &b);
        return;
    case LTYPE4:
        yyget();
        op = yylval.lval;
        parse_comma();
        outcode(op, &nullgen, NREG, &nullgen);
        return;
    case LTYPE5:
        yyget();
        op = yylval.lval;
        a = parse_vlgen();
        yyexpect(',');
        b = parse_vgen();
        if (!isreg(&a) && !isreg(&b))
            print("one side must be register\n");
        outcode(op, &a, NREG, &b);
        return;
    case LTYPE6:
        yyget();
        op = yylval.lval;
        a = parse_reg();
        yyexpect(',');
        s = parse_sreg();
        if (yypeek(0) == ',')
        {
            yyget();
            if (yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF)
            {
                outcode(op, &a, s, &nullgen);
                return;
            }
            b = parse_reg();
            outcode(op, &a, s, &b);
            return;
        }
        parse_comma();
        outcode(op, &a, s, &nullgen);
        return;
    case LTYPE7:
        yyget();
        op = yylval.lval;
        parse_comma();
        if (yypeek(0) == '(' || yypeek(0) == LNAME)
        {
            int sh, i;
            long st[256];
            YYSTYPE sv[256];
            sh = yyhave;
            for (i = 0; i < yyhave; i++)
            {
                st[i] = yytok[i];
                sv[i] = yyval[i];
            }
            if (yypeek(0) == LNAME)
            {
                Sym *sx;
                vlong off;
                yyget();
                sx = yylval.sym;
                off = parse_offset();
                USED(sx);
                USED(off);
                if (yypeek(0) == '(' || yypeek(0) == '<')
                {
                    yyhave = sh;
                    for (i = 0; i < sh; i++)
                    {
                        yytok[i] = st[i];
                        yyval[i] = sv[i];
                    }
                    b = parse_nireg();
                    outcode(op, &nullgen, NREG, &b);
                    return;
                }
                yyhave = sh;
                for (i = 0; i < sh; i++)
                {
                    yytok[i] = st[i];
                    yyval[i] = sv[i];
                }
            }
        }
        if (yypeek(0) == '(')
        {
            b = parse_nireg();
            outcode(op, &nullgen, NREG, &b);
            return;
        }
        b = parse_rel();
        outcode(op, &nullgen, NREG, &b);
        return;
    case LTYPE8:
        yyget();
        op = yylval.lval;
        if ((yypeek(0) == LREG || yypeek(0) == LR) && yypeek(1) != ',' && yypeek(1) != ';' && yypeek(1) != 0 && yypeek(1) != EOF)
        {
            int sh, i;
            long st[256];
            YYSTYPE sv[256];
            sh = yyhave;
            for (i = 0; i < yyhave; i++)
            {
                st[i] = yytok[i];
                sv[i] = yyval[i];
            }
            s = parse_sreg();
            USED(s);
            if (yypeek(0) == ',')
            {
                yyhave = sh;
                for (i = 0; i < sh; i++)
                {
                    yytok[i] = st[i];
                    yyval[i] = sv[i];
                }
                s = parse_sreg();
                yyexpect(',');
                b = parse_nireg();
                outcode(op, &nullgen, s, &b);
                return;
            }
            yyhave = sh;
            for (i = 0; i < sh; i++)
            {
                yytok[i] = st[i];
                yyval[i] = sv[i];
            }
        }
        parse_comma();
        if (yypeek(0) == '(' || yypeek(0) == LNAME)
        {
            int sh, i;
            long st[256];
            YYSTYPE sv[256];
            sh = yyhave;
            for (i = 0; i < yyhave; i++)
            {
                st[i] = yytok[i];
                sv[i] = yyval[i];
            }
            if (yypeek(0) == LNAME)
            {
                yyget();
                Sym *sx;
                vlong off;
                sx = yylval.sym;
                off = parse_offset();
                USED(sx);
                USED(off);
                if (yypeek(0) == '(' || yypeek(0) == '<')
                {
                    yyhave = sh;
                    for (i = 0; i < sh; i++)
                    {
                        yytok[i] = st[i];
                        yyval[i] = sv[i];
                    }
                    b = parse_nireg();
                    outcode(op, &nullgen, NREG, &b);
                    return;
                }
                yyhave = sh;
                for (i = 0; i < sh; i++)
                {
                    yytok[i] = st[i];
                    yyval[i] = sv[i];
                }
            }
            if (yypeek(0) == '(')
            {
                b = parse_nireg();
                outcode(op, &nullgen, NREG, &b);
                return;
            }
        }
        b = parse_rel();
        outcode(op, &nullgen, NREG, &b);
        return;
    case LTYPE9:
        yyget();
        op = yylval.lval;
        a = parse_gen();
        yyexpect(',');
        if (yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF)
            yysyntax_error();
        {
            int sh, i;
            long st[256];
            YYSTYPE sv[256];
            sh = yyhave;
            for (i = 0; i < yyhave; i++)
            {
                st[i] = yytok[i];
                sv[i] = yyval[i];
            }
            if (!isconstart(yypeek(0)))
            {
                yyhave = sh;
                for (i = 0; i < sh; i++)
                {
                    yytok[i] = st[i];
                    yyval[i] = sv[i];
                }
                b = parse_rel();
                if (!isreg(&a))
                    print("left side must be register\n");
                outcode(op, &a, NREG, &b);
                return;
            }
            {
                vlong c;
                c = parse_con();
                USED(c);
                if (yypeek(0) == ',')
                {
                    yyhave = sh;
                    for (i = 0; i < sh; i++)
                    {
                        yytok[i] = st[i];
                        yyval[i] = sv[i];
                    }
                    s = parse_sreg();
                    yyexpect(',');
                    b = parse_rel();
                    if (!isreg(&a))
                        print("left side must be register\n");
                    outcode(op, &a, s, &b);
                    return;
                }
            }
            yyhave = sh;
            for (i = 0; i < sh; i++)
            {
                yytok[i] = st[i];
                yyval[i] = sv[i];
            }
        }
        {
            Gen rb;
            rb = parse_rel();
            if (!isreg(&a))
                print("left side must be register\n");
            outcode(op, &a, NREG, &rb);
            return;
        }
    case LTYPEA:
        yyget();
        op = yylval.lval;
        a = parse_gen();
        yyexpect(',');
        b = parse_rel();
        if (!isreg(&a))
            print("left side must be register\n");
        outcode(op, &a, NREG, &b);
        return;
    case LTYPEB:
        yyget();
        op = yylval.lval;
        a = parse_name();
        yyexpect(',');
        if (yypeek(0) == '$')
        {
            b = parse_imm();
            outcode(op, &a, NREG, &b);
            return;
        }
        s = parse_con();
        yyexpect(',');
        b = parse_imm();
        outcode(op, &a, s, &b);
        return;
    case LTYPEC:
        yyget();
        op = yylval.lval;
        a = parse_name();
        yyexpect('/');
        s = parse_con();
        yyexpect(',');
        b = parse_ximm();
        outcode(op, &a, s, &b);
        return;
    case LTYPED:
        yyget();
        op = yylval.lval;
        a = parse_freg();
        yyexpect(',');
        b = parse_freg();
        outcode(op, &a, NREG, &b);
        return;
    case LTYPEE:
        yyget();
        op = yylval.lval;
        a = parse_freg();
        yyexpect(',');
        if (yypeek(0) == LFREG || yypeek(0) == LF)
        {
            int sh, i;
            long st[256];
            YYSTYPE sv[256];
            sh = yyhave;
            for (i = 0; i < yyhave; i++)
            {
                st[i] = yytok[i];
                sv[i] = yyval[i];
            }
            {
                Gen fr;
                long lv;
                fr = parse_freg();
                if (yypeek(0) == ',')
                {
                    yyhave = sh;
                    for (i = 0; i < sh; i++)
                    {
                        yytok[i] = st[i];
                        yyval[i] = sv[i];
                    }
                    a = parse_freg();
                    yyexpect(',');
                    t = yyget();
                    if (t != LFREG)
                        yysyntax_error();
                    lv = yylval.lval;
                    yyexpect(',');
                    b = parse_freg();
                    outcode(op, &a, lv, &b);
                    return;
                }
            }
            yyhave = sh;
            for (i = 0; i < sh; i++)
            {
                yytok[i] = st[i];
                yyval[i] = sv[i];
            }
        }
        b = parse_freg();
        outcode(op, &a, NREG, &b);
        return;
    case LTYPEF:
        yyget();
        op = yylval.lval;
        a = parse_freg();
        yyexpect(',');
        t = yyget();
        if (t != LFREG)
            yysyntax_error();
        s = yylval.lval;
        parse_comma();
        outcode(op, &a, s, &nullgen);
        return;
    case LTYPEG:
        yyget();
        op = yylval.lval;
        parse_comma();
        b = parse_rel();
        outcode(op, &nullgen, NREG, &b);
        return;
    case LTYPEH:
        yyget();
        op = yylval.lval;
        parse_comma();
        b = parse_ximm();
        outcode(op, &nullgen, NREG, &b);
        return;
    case LTYPEI:
        yyget();
        op = yylval.lval;
        if (yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF)
        {
            parse_comma();
            outcode(op, &nullgen, NREG, &nullgen);
            return;
        }
        if (yypeek(0) == ',')
        {
            yyget();
            b = parse_vgen();
            outcode(op, &nullgen, NREG, &b);
            return;
        }
        b = parse_vgen();
        parse_comma();
        outcode(op, &b, NREG, &nullgen);
        return;
    case LTYPEJ:
        yyget();
        op = yylval.lval;
        parse_comma();
        if (yypeek(0) == ';' || yypeek(0) == 0 || yypeek(0) == EOF)
        {
            outcode(op, &nullgen, NREG, &nullgen);
            return;
        }
        a = parse_vgen();
        yyexpect(',');
        b = parse_vgen();
        outcode(op, &a, NREG, &b);
        return;
    default:
        yysyntax_error();
        return;
    }
}
static void parse_line(void)
{
    long t;
    Sym *s;
    vlong v;
    if (setjmp(yyrec))
    {
        yyskip_to_semi();
        return;
    }
    for (;;)
    {
        if (yypeek(0) == LLAB && yypeek(1) == ':')
        {
            yyget();
            s = yylval.sym;
            yyget();
            if (s->value != pc)
                yyerror("redeclaration of %s", s->name);
            s->value = pc;
            continue;
        }
        if (yypeek(0) == LNAME && yypeek(1) == ':')
        {
            yyget();
            s = yylval.sym;
            yyget();
            s->type = LLAB;
            s->value = pc;
            continue;
        }
        break;
    }
    t = yypeek(0);
    if (t == ';')
    {
        yyget();
        return;
    }
    if (t == 0 || t == EOF)
        return;
    if (t == LNAME && yypeek(1) == '=')
    {
        yyget();
        s = yylval.sym;
        yyget();
        v = parse_expr();
        yyexpect(';');
        s->type = LVAR;
        s->value = v;
        return;
    }
    if (t == LVAR && yypeek(1) == '=')
    {
        yyget();
        s = yylval.sym;
        yyget();
        v = parse_expr();
        yyexpect(';');
        if (s->value != v)
            yyerror("redeclaration of %s", s->name);
        s->value = v;
        return;
    }
    if (t == LSCHED)
    {
        yyget();
        v = yylval.lval;
        yyexpect(';');
        nosched = v;
        return;
    }
    parse_inst();
    t = yyget();
    if (t != ';')
        yysyntax_error();
}
int yyparse(void)
{
    long t;
    yyhave = 0;
    for (;;)
    {
        t = yypeek(0);
        if (t == 0 || t == EOF)
            break;
        parse_line();
    }
    return 0;
}
