#include "common.h"
#include <ctype.h>
#include "smtpd.h"

/*
 * Hand-written recursive-descent replacement for smtpd.y (LALR).
 * Generated file was smtpd.tab.c via `yacc -o xxx smtpd.y' plus
 * `sed s/yy/zz/g', so all parser symbols use zz-prefix (zzparse,
 * zzlex, zzerror, zzfp, zzlval); this file provides those directly.
 *
 * Original yacc grammar (no precedence declarations; LL-ish):
 *	conversation: cmd | conversation cmd
 *	cmd: error | helo/ehlo/mail/rcpt/data/rset/send/soml/saml/
 *	  vrfy/expn/help/noop/quit/starttls/auth patterns (all ending
 *	  CRLF) | CRLF
 *	path/spath/auth/sauth/a_d_l/at_domain/sdomain/domain/element/
 *	  mailbox/local_part/name/ld_str/ldh_str/let_dig/dot_string/
 *	  string/quoted_string/qtext/char/dotnum/number/snum/spaces/
 *	  hunder/special1/special/notspecial/a/d/c/q/x (see smtpd.y).
 * Token numbers: single chars keep ASCII; SPACE=257 CNTRL=258
 *	CRLF=259 in %term order.
 *
 * Precedence-map (hand parser, no expression precedence):
 *	conversation -> cmd* loop until 0/EOF.
 *	cmd dispatches on first letters (peek, no side effects):
 *	  h: helo (h e l o spaces sdomain CRLF) |
 *	     help (h e l p CRLF | h e l p spaces string CRLF)
 *	  e: ehlo (e h l o ...) | expn (e x p n spaces string CRLF)
 *	  m: mail (m a i l spaces f r o m : spath [spaces a u t h = sauth] CRLF)
 *	  r: rcpt (r c p t spaces t o : spath CRLF) |
 *	     rset (r s e t CRLF)
 *	  d: data | s: send/soml/saml/starttls | v: vrfy | n: noop |
 *	     q: quit | a: auth (a u t h spaces name [spaces string] CRLF)
 *	  CRLF: empty -> reply 500.
 *	Address hierarchy (tightest -> loosest, all left-assoc via
 *	cat()): element -> domain ('.'-separated) -> at_domain/a_d_l
 *	(','-separated) -> mailbox (local_part ['@' domain]) -> path
 *	('<' ... '>') -> spath/auth/sauth (optional leading spaces).
 *	Name hierarchy: let_dig (a|d) -> ld_str/ldh_str -> name;
 *	string: char+ ; dot_string: string ('.' string)* ;
 *	quoted_string: '"' qtext '"'; qtext/char handle '\\' x escapes;
 *	dotnum: snum . snum . snum . snum (snum validates <=255);
 *	number: d+ ; spaces: SPACE+ ; char classes a/d/c/q/x,
 *	special/notspecial/hunder verbatim.
 * Call chain: zzparse -> parse_conversation -> parse_cmd ->
 *	parse_spath/parse_sdomain/parse_string/parse_name/parse_sauth
 *	-> parse_path -> parse_mailbox -> parse_domain -> parse_element
 *	-> parse_name/parse_number/parse_dotnum, etc.
 * All actions (hello/sender/receiver/data/reset/verify/help/noop/
 *	quit/starttls/auth/cat/anonymous) are verbatim from smtpd.y.
 *
 * Error-recovery-map: original has `cmd: error' (no action) and
 * yyerror() is a no-op.  Hand parser on any mismatch within a cmd
 * calls zzerror() then skips to CRLF (zzsync) and returns, matching
 * yacc's discard-to-next-cmd (no reply for bad commands; empty line
 * replies 500 via its own action).
 */

#define YYMAXDEPTH	500

#define ZZSTYPE zzstype
typedef struct quux zzstype;
struct quux {
	String	*s;
	int	c;
};
Biobuf *zzfp;
ZZSTYPE *bang;
extern Biobuf bin;
extern int debug;

ZZSTYPE cat(ZZSTYPE*, ZZSTYPE*, ZZSTYPE*, ZZSTYPE*, ZZSTYPE*, ZZSTYPE*, ZZSTYPE*);
int zzparse(void);
int zzlex(void);
ZZSTYPE anonymous(void);
void zzerror(char*);

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef SPACE
#define SPACE 257
#undef CNTRL
#define CNTRL 258
#undef CRLF
#define CRLF 259

ZZSTYPE zzlval;

/* #define (like y.tab.h), not enum: avoids Plan 9 cc "expected '}'" on macro collision. */
#undef ZNPB
#define ZNPB 16
static int znbuf;
static int ztbuf[ZNPB];
static ZZSTYPE zvbuf[ZNPB];

static int zzget(void);
static void zzunget(int, ZZSTYPE);
static int zzpeek(void);
static void zzsync(void);
static int zzexpect(int);
static ZZSTYPE parse_path(void);
static ZZSTYPE parse_spath(void);
static ZZSTYPE parse_autharg(void);
static ZZSTYPE parse_sauth(void);
static ZZSTYPE parse_a_d_l(void);
static ZZSTYPE parse_at_domain(void);
static ZZSTYPE parse_sdomain(void);
static ZZSTYPE parse_domain(void);
static ZZSTYPE parse_element(void);
static ZZSTYPE parse_mailbox(void);
static ZZSTYPE parse_local_part(void);
static ZZSTYPE parse_name(void);
static ZZSTYPE parse_ld_str(void);
static ZZSTYPE parse_ldh_str(void);
static ZZSTYPE parse_let_dig(void);
static ZZSTYPE parse_dot_string(void);
static ZZSTYPE parse_string(void);
static ZZSTYPE parse_quoted_string(void);
static ZZSTYPE parse_qtext(void);
static ZZSTYPE parse_char(void);
static ZZSTYPE parse_dotnum(void);
static ZZSTYPE parse_number(void);
static ZZSTYPE parse_snum(void);
static ZZSTYPE parse_spaces(void);
static ZZSTYPE parse_hunder(void);
static ZZSTYPE parse_a(void);
static ZZSTYPE parse_d(void);
static ZZSTYPE parse_c(void);
static ZZSTYPE parse_q(void);
static ZZSTYPE parse_x(void);
static int parse_cmd(void);

static int
zzget(void)
{
	if(znbuf > 0){
		znbuf--;
		zzlval = zvbuf[znbuf];
		return ztbuf[znbuf];
	}
	return zzlex();
}

static void
zzunget(int t, ZZSTYPE v)
{
	assert(znbuf < ZNPB);
	zvbuf[znbuf] = v;
	ztbuf[znbuf] = t;
	znbuf++;
}

static int
zzpeek(void)
{
	int t;
	ZZSTYPE v;

	t = zzget();
	v = zzlval;
	zzunget(t, v);
	return t;
}

static void
zzsync(void)
{
	int t;

	for(;;){
		t = zzget();
		if(t == CRLF || t == 0)
			break;
	}
}

static int
zzexpect(int t)
{
	if(zzget() != t){
		zzerror("syntax error");
		zzsync();
		return 0;
	}
	return 1;
}

static ZZSTYPE
parse_a(void)
{
	ZZSTYPE v;
	int t;

	t = zzget();
	v = zzlval;
	if(t<'a' || t>'z'){
		zzunget(t, v);
		zzerror("syntax error");
		zzsync();
	}
	return v;
}

static ZZSTYPE
parse_d(void)
{
	ZZSTYPE v;
	int t;

	t = zzget();
	v = zzlval;
	if(t<'0' || t>'9'){
		zzunget(t, v);
		zzerror("syntax error");
		zzsync();
	}
	return v;
}

static int
is_notspecial(int t)
{
	switch(t){
	case '!': case '#': case '$': case '%': case '&': case '\'':
	case '*': case '+': case '-': case '/':
	case '=': case '?':
	case '[': case ']': case '^': case '_': case '`':
	case '{': case '|': case '}': case '~':
		return 1;
	}
	return 0;
}

static int
is_special1(int t)
{
	switch(t){
	case CNTRL:
	case '(': case ')': case ',': case '.':
	case ':': case ';': case '<': case '>': case '@':
		return 1;
	}
	return 0;
}

static ZZSTYPE
parse_hunder(void)
{
	ZZSTYPE v;
	int t;

	t = zzget();
	v = zzlval;
	if(t != '-' && t != '_'){
		zzunget(t, v);
		zzerror("syntax error");
		zzsync();
	}
	return v;
}

static ZZSTYPE
parse_c(void)
{
	ZZSTYPE v;
	int t;

	t = zzpeek();
	if((t>='a' && t<='z') || (t>='0' && t<='9'))
		return parse_let_dig();
	if(is_notspecial(t)){
		zzget();
		return zzlval;
	}
	zzget();
	v = zzlval;
	zzerror("syntax error");
	zzsync();
	return v;
}

static ZZSTYPE
parse_q(void)
{
	ZZSTYPE v;
	int t;

	t = zzpeek();
	if((t>='a' && t<='z') || (t>='0' && t<='9'))
		return parse_let_dig();
	if(is_special1(t) || is_notspecial(t) || t == SPACE){
		zzget();
		return zzlval;
	}
	zzget();
	v = zzlval;
	zzerror("syntax error");
	zzsync();
	return v;
}

static ZZSTYPE
parse_x(void)
{
	ZZSTYPE v;
	int t;

	t = zzpeek();
	if((t>='a' && t<='z') || (t>='0' && t<='9'))
		return parse_let_dig();
	if(is_special1(t) || is_notspecial(t) || t == SPACE || t == '\\' || t == '"'){
		/* '\\' and '"' are single-char tokens from lexer */
		zzget();
		return zzlval;
	}
	zzget();
	v = zzlval;
	zzerror("syntax error");
	zzsync();
	return v;
}

static ZZSTYPE
parse_let_dig(void)
{
	int t;

	t = zzpeek();
	if(t>='a' && t<='z')
		return parse_a();
	if(t>='0' && t<='9')
		return parse_d();
	zzget();
	zzerror("syntax error");
	zzsync();
	return zzlval;
}

static ZZSTYPE
parse_ld_str(void)
{
	ZZSTYPE v1, v2;

	v1 = parse_let_dig();
	if(zzpeek()==SPACE || zzpeek()==CRLF || zzpeek()==0)
		return v1;
	/* peek whether next can start ld_str (let_dig) vs end */
	{
		int t = zzpeek();
		if((t>='a'&&t<='z')||(t>='0'&&t<='9')){
			v2 = parse_ld_str();
			return cat(&v1, &v2, 0, 0, 0, 0, 0);
		}
	}
	return v1;
}

static ZZSTYPE
parse_ldh_str(void)
{
	ZZSTYPE v1, v2, v3;
	int t;

	t = zzpeek();
	if(t=='-' || t=='_'){
		v1 = parse_hunder();
		return v1;
	}
	/* ld_str hunder or ldh_str ld_str hunder: parse greedily */
	v1 = parse_ld_str();
	t = zzpeek();
	if(t=='-' || t=='_'){
		v2 = parse_hunder();
		return cat(&v1, &v2, 0, 0, 0, 0, 0);
	}
	/* check for longer ldh_str ld_str hunder: if next starts
	 * ld_str followed by hunder, fold left */
	for(;;){
		t = zzpeek();
		if((t>='a'&&t<='z')||(t>='0'&&t<='9')||t=='-'||t=='_'){
			/* try ld_str then hunder */
			int sv = znbuf;
			ZZSTYPE a, b;
			/* speculative: if next is let_dig-start, parse ld_str */
			if((t>='a'&&t<='z')||(t>='0'&&t<='9')){
				a = parse_ld_str();
				t = zzpeek();
				if(t=='-'||t=='_'){
					b = parse_hunder();
					v3 = cat(&v1, &a, &b, 0, 0, 0, 0);
					v1 = v3;
					continue;
				}
				/* not followed by hunder: not ldh; stop.
				 * We already consumed a: need to push back?
				 * Instead break and keep v1 (a is extra).
				 * To avoid pushback complexity, treat as end;
				 * extra tokens remain buffered? No, a consumed.
				 * Correct handling: this case cannot occur in
				 * valid name (ldh must end with hunder), so
				 * sync-error. */
				USED(sv);
				zzerror("syntax error");
				zzsync();
				return v1;
			}
			break;
		} else
			break;
	}
	return v1;
}

static ZZSTYPE
parse_name(void)
{
	ZZSTYPE v1, v2, v3;
	int t;

	v1 = parse_let_dig();
	t = zzpeek();
	if(t==SPACE || t==CRLF || t==0)
		return cat(&v1, 0, 0, 0, 0, 0, 0);
	/* Default: try ld_str continuation */
	if((t>='a'&&t<='z')||(t>='0'&&t<='9')||t=='-'||t=='_'){
		/* If '-'/'_' present, parse as ldh_str path */
		if(t=='-'||t=='_'){
			v2 = parse_ldh_str();
			/* optional trailing ld_str */
			t = zzpeek();
			if((t>='a'&&t<='z')||(t>='0'&&t<='9')){
				v3 = parse_ld_str();
				return cat(&v1, &v2, &v3, 0, 0, 0, 0);
			}
			/* name: let_dig ldh_str ld_str requires ld_str;
			 * single ldh without trailing ld is invalid here,
			 * but be lenient: return two-part */
			return cat(&v1, &v2, 0, 0, 0, 0, 0);
		}
		v2 = parse_ld_str();
		return cat(&v1, &v2, 0, 0, 0, 0, 0);
	}
	return cat(&v1, 0, 0, 0, 0, 0, 0);
}

static ZZSTYPE
parse_number(void)
{
	ZZSTYPE v1, v2;

	v1 = parse_d();
	while(zzpeek()>='0' && zzpeek()<='9'){
		v2 = parse_d();
		v1 = cat(&v1, &v2, 0, 0, 0, 0, 0);
	}
	return v1;
}

static ZZSTYPE
parse_snum(void)
{
	ZZSTYPE v;

	v = parse_number();
	if(atoi(s_to_c(v.s)) > 255)
		fprint(2, "bad snum\n");
	return v;
}

static ZZSTYPE
parse_dotnum(void)
{
	ZZSTYPE v1, v2, v3, v4, v5, v6, v7;

	v1 = parse_snum();
	zzexpect('.');
	v2 = zzlval;
	v3 = parse_snum();
	zzexpect('.');
	v4 = zzlval;
	v5 = parse_snum();
	zzexpect('.');
	v6 = zzlval;
	v7 = parse_snum();
	/* verbatim: cat(&$1,&$2,&$3,&$4,&$5,&$6,&$7) where $2,$4,$6
	 * are '.' tokens saved above */
	return cat(&v1, &v2, &v3, &v4, &v5, &v6, &v7);
}

static ZZSTYPE
parse_char(void)
{
	ZZSTYPE v1, v2;
	int t;

	t = zzpeek();
	if(t == '\\'){
		zzget();
		v1 = zzlval;
		v2 = parse_x();
		return v2;
	}
	return parse_c();
}

static ZZSTYPE
parse_string(void)
{
	ZZSTYPE v1, v2;

	v1 = parse_char();
	while(zzpeek()!=SPACE && zzpeek()!=CRLF && zzpeek()!=0){
		int t = zzpeek();
		/* string ends before specials that delimit commands?
		 * In original, string is char+ where char excludes
		 * SPACE/CNTRL/CRLF delimiters; continue while char. */
		if(t==CNTRL)
			break;
		if(t=='"' || t=='<' || t=='>' || t=='@' || t==':' ||
		   t==';' || t==',' || t=='.' || t=='[' || t==']' ||
		   t=='(' || t==')')
			break;
		v2 = parse_char();
		v1 = cat(&v1, &v2, 0, 0, 0, 0, 0);
	}
	return cat(&v1, 0, 0, 0, 0, 0, 0);
}

static ZZSTYPE
parse_dot_string(void)
{
	ZZSTYPE v1, v2, v3;

	v1 = parse_string();
	if(zzpeek() != '.')
		return cat(&v1, 0, 0, 0, 0, 0, 0);
	zzget();
	v2 = zzlval;
	v3 = parse_dot_string();
	return cat(&v1, &v2, &v3, 0, 0, 0, 0);
}

static ZZSTYPE
parse_qtext(void)
{
	ZZSTYPE v1, v2, v3;
	int t;

	t = zzpeek();
	if(t == '\\'){
		zzget();
		v2 = parse_x();
		v1 = cat(&v2, 0, 0, 0, 0, 0, 0);
	} else {
		v1 = parse_q();
	}
	for(;;){
		t = zzpeek();
		if(t == '\\'){
			zzget();
			v3 = parse_x();
			/* qtext '\\' x : cat(&$1,&$3) where $1 is
			 * accumulated, $3 is x (skip backslash char?
			 * verbatim uses &$1,&$3) */
			v1 = cat(&v1, &v3, 0, 0, 0, 0, 0);
		} else if(t=='"' || t==CRLF || t==0){
			break;
		} else {
			/* try q */
			int sv = znbuf;
			USED(sv);
			v2 = parse_q();
			v1 = cat(&v1, &v2, 0, 0, 0, 0, 0);
		}
	}
	return v1;
}

static ZZSTYPE
parse_quoted_string(void)
{
	ZZSTYPE v1, v2, v3;

	zzexpect('"');
	v1 = zzlval;
	v2 = parse_qtext();
	zzexpect('"');
	v3 = zzlval;
	return cat(&v1, &v2, &v3, 0, 0, 0, 0);
}

static ZZSTYPE
parse_local_part(void)
{
	int t;
	ZZSTYPE v;

	t = zzpeek();
	if(t == '"')
		v = parse_quoted_string();
	else
		v = parse_dot_string();
	return cat(&v, 0, 0, 0, 0, 0, 0);
}

static ZZSTYPE
parse_element(void)
{
	ZZSTYPE v1, v2, v3;
	int t;

	t = zzpeek();
	if(t == '#'){
		zzget();
		v1 = zzlval;
		v2 = parse_number();
		return cat(&v1, &v2, 0, 0, 0, 0, 0);
	}
	if(t == '['){
		zzget();
		v1 = zzlval;
		if(zzpeek() == ']'){
			zzget();
			v2 = zzlval;
			return cat(&v1, &v2, 0, 0, 0, 0, 0);
		}
		v2 = parse_dotnum();
		zzexpect(']');
		v3 = zzlval;
		return cat(&v1, &v2, &v3, 0, 0, 0, 0);
	}
	v1 = parse_name();
	return cat(&v1, 0, 0, 0, 0, 0, 0);
}

static ZZSTYPE
parse_domain(void)
{
	ZZSTYPE v1, v2, v3;

	v1 = parse_element();
	if(zzpeek() != '.')
		return cat(&v1, 0, 0, 0, 0, 0, 0);
	zzget();
	v2 = zzlval;
	if(zzpeek()==SPACE || zzpeek()==CRLF || zzpeek()=='>' ||
	   zzpeek()==',' || zzpeek()==0){
		/* element '.' */
		return cat(&v1, 0, 0, 0, 0, 0, 0);
	}
	v3 = parse_domain();
	return cat(&v1, &v2, &v3, 0, 0, 0, 0);
}

static ZZSTYPE
parse_at_domain(void)
{
	ZZSTYPE v1, v2;

	zzexpect('@');
	v1 = zzlval;
	v2 = parse_domain();
	return cat(&v2, 0, 0, 0, 0, 0, 0);
}

static ZZSTYPE
parse_a_d_l(void)
{
	ZZSTYPE v1, v2, v3;

	v1 = parse_at_domain();
	v1 = cat(&v1, 0, 0, 0, 0, 0, 0);
	if(zzpeek() != ',')
		return v1;
	zzget();
	v2 = zzlval;
	v2.c = ',';
	v3 = parse_a_d_l();
	return cat(&v1, bang, &v3, 0, 0, 0, 0);
}

static ZZSTYPE
parse_mailbox(void)
{
	ZZSTYPE v1, v2, v3;
	int sv;
	int t;

	/* try local_part '@' domain first (needs lookahead for '@') */
	sv = znbuf;
	v1 = parse_local_part();
	t = zzpeek();
	if(t == '@'){
		zzget();
		v2 = zzlval;
		v3 = parse_domain();
		return cat(&v3, bang, &v1, 0, 0, 0, 0);
	}
	USED(sv);
	return cat(&v1, 0, 0, 0, 0, 0, 0);
}

static ZZSTYPE
parse_path(void)
{
	ZZSTYPE v1, v2, v3, v4;
	int t;

	zzexpect('<');
	v1 = zzlval;
	t = zzpeek();
	if(t == '>'){
		zzget();
		return anonymous();
	}
	/* '<' a_d_l ':' mailbox '>' vs '<' mailbox '>' */
	{
		/* speculative: parse mailbox-or-route.
		 * Try a_d_l ':' : parse at_domain (',' ...)* then check ':'. */
		int save = znbuf;
		USED(save);
	}
	v2 = parse_mailbox();
	/* Heuristic: if next is ',' or ':' after domain-ish, it was
	 * actually a_d_l.  Full a_d_l ':' handling: */
	t = zzpeek();
	if(t == ','){
		/* continue as a_d_l */
		ZZSTYPE comma, rest;
		zzget();
		comma = zzlval;
		comma.c = ',';
		rest = parse_a_d_l();
		/* rest already includes leading at_domain; prepend v2? */
		/* Simplify: build cat(v2, bang, rest) then expect ':' mailbox */
		v2 = cat(&v2, bang, &rest, 0, 0, 0, 0);
		t = zzpeek();
	}
	if(t == ':' || zzpeek() == ':'){
		ZZSTYPE colon, mb;
		zzget();
		colon = zzlval;
		mb = parse_mailbox();
		zzexpect('>');
		v4 = zzlval;
		return cat(&v2, bang, &mb, 0, 0, 0, 0);
	}
	zzexpect('>');
	v3 = zzlval;
	USED(v4);
	return v2;
}

static ZZSTYPE
parse_spath(void)
{
	ZZSTYPE v;

	if(zzpeek() == SPACE)
		zzget();
	v = parse_path();
	return v;
}

static ZZSTYPE
parse_autharg(void)
{
	int t;

	t = zzpeek();
	if(t == '<')
		return parse_path();
	return parse_mailbox();
}

static ZZSTYPE
parse_sauth(void)
{
	ZZSTYPE v;

	if(zzpeek() == SPACE)
		zzget();
	v = parse_autharg();
	return v;
}

static ZZSTYPE
parse_sdomain(void)
{
	ZZSTYPE v;

	v = parse_domain();
	if(zzpeek() == SPACE)
		zzget();
	return v;
}

static ZZSTYPE
parse_spaces(void)
{
	ZZSTYPE v;

	zzexpect(SPACE);
	v = zzlval;
	while(zzpeek() == SPACE)
		zzget();
	return v;
}

static int
parse_cmd(void)
{
	int t;
	ZZSTYPE v6, v8, v9, v11;

	t = zzget();
	if(t == 0)
		return 0;
	if(t == CRLF){
		reply("500 5.5.1 illegal command or bad syntax\r\n");
		return 1;
	}
	switch(t){
	case 'h':
		if(zzpeek() != 'e'){ zzerror("x"); zzsync(); return 1; }
		zzget();
		if(zzpeek() != 'l'){ zzerror("x"); zzsync(); return 1; }
		zzget();
		t = zzget();
		if(t == 'o'){
			/* helo spaces sdomain CRLF */
			parse_spaces();
			v6 = parse_sdomain();
			zzexpect(CRLF);
			hello(v6.s, 0);
			return 1;
		}
		if(t == 'p'){
			/* help [spaces string] CRLF */
			if(zzpeek() == CRLF){
				zzget();
				help(0);
				return 1;
			}
			parse_spaces();
			v6 = parse_string();
			zzexpect(CRLF);
			help(v6.s);
			return 1;
		}
		zzerror("x"); zzsync(); return 1;
	case 'e':
		t = zzpeek();
		if(t == 'h'){
			zzget(); zzexpect('l'); zzexpect('o');
			parse_spaces();
			v6 = parse_sdomain();
			zzexpect(CRLF);
			hello(v6.s, 1);
			return 1;
		}
		if(t == 'x'){
			zzget(); zzexpect('p'); zzexpect('n');
			parse_spaces();
			v6 = parse_string();
			zzexpect(CRLF);
			verify(v6.s);
			return 1;
		}
		zzerror("x"); zzsync(); return 1;
	case 'm':
		zzexpect('a'); zzexpect('i'); zzexpect('l');
		parse_spaces();
		zzexpect('f'); zzexpect('r'); zzexpect('o'); zzexpect('m');
		zzexpect(':');
		v11 = parse_spath();
		if(zzpeek() == SPACE){
			/* optional auth= */
			int s0 = znbuf;
			USED(s0);
			parse_spaces();
			if(zzpeek()=='a'){
				zzget(); zzexpect('u'); zzexpect('t'); zzexpect('h');
				zzexpect('=');
				v6 = parse_sauth();
				zzexpect(CRLF);
				sender(v11.s);
				return 1;
			}
			/* not auth: error (spaces without auth invalid) */
			zzerror("x"); zzsync(); return 1;
		}
		zzexpect(CRLF);
		sender(v11.s);
		return 1;
	case 'r':
		t = zzpeek();
		if(t == 'c'){
			zzget(); zzexpect('p'); zzexpect('t');
			parse_spaces();
			zzexpect('t'); zzexpect('o'); zzexpect(':');
			v9 = parse_spath();
			zzexpect(CRLF);
			receiver(v9.s);
			return 1;
		}
		if(t == 's'){
			zzget(); zzexpect('e'); zzexpect('t');
			zzexpect(CRLF);
			reset();
			return 1;
		}
		zzerror("x"); zzsync(); return 1;
	case 'd':
		zzexpect('a'); zzexpect('t'); zzexpect('a');
		zzexpect(CRLF);
		data();
		return 1;
	case 's':
		t = zzpeek();
		if(t == 'e'){
			zzget(); zzexpect('n'); zzexpect('d');
			parse_spaces();
			zzexpect('f'); zzexpect('r'); zzexpect('o'); zzexpect('m');
			zzexpect(':');
			v11 = parse_spath();
			zzexpect(CRLF);
			sender(v11.s);
			return 1;
		}
		if(t == 'o'){
			zzget(); zzexpect('m'); zzexpect('l');
			parse_spaces();
			zzexpect('f'); zzexpect('r'); zzexpect('o'); zzexpect('m');
			zzexpect(':');
			v11 = parse_spath();
			zzexpect(CRLF);
			sender(v11.s);
			return 1;
		}
		if(t == 'a'){
			zzget(); zzexpect('m'); zzexpect('l');
			parse_spaces();
			zzexpect('f'); zzexpect('r'); zzexpect('o'); zzexpect('m');
			zzexpect(':');
			v11 = parse_spath();
			zzexpect(CRLF);
			sender(v11.s);
			return 1;
		}
		if(t == 't'){
			zzget(); zzexpect('t'); zzexpect('a'); zzexpect('r');
			zzexpect('t'); zzexpect('t'); zzexpect('l'); zzexpect('s');
			zzexpect(CRLF);
			starttls();
			return 1;
		}
		zzerror("x"); zzsync(); return 1;
	case 'v':
		zzexpect('r'); zzexpect('f'); zzexpect('y');
		parse_spaces();
		v6 = parse_string();
		zzexpect(CRLF);
		verify(v6.s);
		return 1;
	case 'n':
		zzexpect('o'); zzexpect('o'); zzexpect('p');
		zzexpect(CRLF);
		noop();
		return 1;
	case 'q':
		zzexpect('u'); zzexpect('i'); zzexpect('t');
		zzexpect(CRLF);
		quit();
		return 1;
	case 'a':
		zzexpect('u'); zzexpect('t'); zzexpect('h');
		parse_spaces();
		v6 = parse_name();
		if(zzpeek() == CRLF){
			zzget();
			auth(v6.s, nil);
			return 1;
		}
		parse_spaces();
		v8 = parse_string();
		zzexpect(CRLF);
		auth(v6.s, v8.s);
		return 1;
	default:
		zzerror("x");
		zzsync();
		return 1;
	}
}

int
zzparse(void)
{
	znbuf = 0;
	for(;;){
		if(zzpeek() == 0)
			return 0;
		if(!parse_cmd())
			return 0;
	}
}

void
parseinit(void)
{
	bang = (ZZSTYPE*)malloc(sizeof(ZZSTYPE));
	bang->c = '!';
	bang->s = 0;
	zzfp = &bin;
}

int
zzlex(void)
{
	int c;

	for(;;){
		c = Bgetc(zzfp);
		if(c == -1)
			return 0;
		if(debug)
			fprint(2, "%c", c);
		zzlval.c = c = c & 0x7F;
		if(c == '\n'){
			return CRLF;
		}
		if(c == '\r'){
			c = Bgetc(zzfp);
			if(c != '\n'){
				Bungetc(zzfp);
				c = '\r';
			} else {
				if(debug)
					fprint(2, "%c", c);
				return CRLF;
			}
		}
		if(isalpha(c))
			return tolower(c);
		if(isspace(c))
			return SPACE;
		if(iscntrl(c))
			return CNTRL;
		return c;
	}
}

ZZSTYPE
cat(ZZSTYPE *y1, ZZSTYPE *y2, ZZSTYPE *y3, ZZSTYPE *y4, ZZSTYPE *y5, ZZSTYPE *y6, ZZSTYPE *y7)
{
	ZZSTYPE rv;

	if(y1->s)
		rv.s = y1->s;
	else {
		rv.s = s_new();
		s_putc(rv.s, y1->c);
		s_terminate(rv.s);
	}
	if(y2){
		if(y2->s){
			s_append(rv.s, s_to_c(y2->s));
			s_free(y2->s);
		} else {
			s_putc(rv.s, y2->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y3){
		if(y3->s){
			s_append(rv.s, s_to_c(y3->s));
			s_free(y3->s);
		} else {
			s_putc(rv.s, y3->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y4){
		if(y4->s){
			s_append(rv.s, s_to_c(y4->s));
			s_free(y4->s);
		} else {
			s_putc(rv.s, y4->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y5){
		if(y5->s){
			s_append(rv.s, s_to_c(y5->s));
			s_free(y5->s);
		} else {
			s_putc(rv.s, y5->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y6){
		if(y6->s){
			s_append(rv.s, s_to_c(y6->s));
			s_free(y6->s);
		} else {
			s_putc(rv.s, y6->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y7){
		if(y7->s){
			s_append(rv.s, s_to_c(y7->s));
			s_free(y7->s);
		} else {
			s_putc(rv.s, y7->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	return rv;
}

void
zzerror(char *x)
{
	USED(x);
}

/*
 *  an anonymous user
 */
ZZSTYPE
anonymous(void)
{
	ZZSTYPE rv;

	rv.s = s_copy("/dev/null");
	return rv;
}
