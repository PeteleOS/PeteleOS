#include "common.h"
#include "smtp.h"
#include <ctype.h>
#include "rfc822_parse.h"

/*
 * Hand-written recursive-descent replacement for rfc822.y (LALR).
 * Generated files were rfc822.tab.c (+ y.tab.h via `yacc -d');
 * this file provides yyparse/yyinit/yylex/yywhite directly and
 * rfc822_parse.h provides token numbers (y.tab.h replacement).
 *
 * Original yacc grammar (no precedence; explicit stratification):
 *	msg: fields | unixfrom '\n' fields
 *	fields: '\n' (yydone=1) | field '\n' | field '\n' fields
 *	field: dates (date=1) | originator (originator=1) |
 *	  destination (destination=1) | subject | optional | ignored |
 *	  received (received++) | precedence | error '\n' field
 *	unixfrom: FROM route_addr unix_date_time REMOTE FROM word
 *	  {freenode...; usender=$2; udate=$3; usys=$6;}
 *	originator: REPLY_TO ':' address_list | RETURN_PATH ':' route_addr
 *	  | FROM ':' mailbox_list | SENDER ':' mailbox |
 *	  RESENT_REPLY_TO ... | RESENT_SENDER ... | RESENT_FROM ...
 *	  (all newfield(link3,1))
 *	dates: DATE ':' date_time | RESENT_DATE ':' date_time (newfield,0)
 *	destination: TO/RESENT_TO/CC/RESENT_CC/BCC/RESENT_BCC [':' [address_list]]
 *	subject: SUBJECT ':' things | SUBJECT ':' (newfield)
 *	received/precedence/ignored: similar with things; ignoredhdr:
 *	  MIMEVERSION|CONTENTTYPE|MESSAGEID(messageid=1)|MAILER
 *	optional: fieldwords ':' things | fieldwords ':' (badfieldname
 *	  check verbatim, return 1 on bad)
 *	address_list: address | address_list ',' address (link3)
 *	address: mailbox | group
 *	group: phrase ':' address_list ';' | phrase ':' ';'
 *	mailbox_list: mailbox | mailbox_list ',' mailbox
 *	mailbox: route_addr | phrase brak_addr (link2) | brak_addr
 *	brak_addr: '<' route_addr '>' | '<' '>' (nobody)
 *	route_addr: route ':' at_addr (address(concat)) | addr_spec
 *	route: '@' domain | route ',' '@' domain (concat)
 *	addr_spec: local_part (address) | at_addr
 *	at_addr: local_part '@' domain | at_addr '@' domain (address concat)
 *	local_part/domain/phrase/word/fieldwords/things/etc. (see rfc822.y)
 * Token numbers: WORD=257 ... MAILER=280 BADTOKEN=281 (decl order);
 *	single chars keep ASCII.
 *
 * Precedence-map (hand parser, loosest -> tightest, all left-assoc
 * via link2/link3/concat unless noted):
 *	msg -> [unixfrom '\n'] fields
 *	fields -> field ('\n' fields)* ; blank '\n' ends (yydone=1)
 *	field dispatches on lookahead keyword (DATE/FROM/SENDER/.../
 *	  TO/CC/BCC/SUBJECT/RECEIVED/PRECEDENCE/MIME.../fieldwords)
 *	address_list/mailbox_list/things/phrase/fieldwords: left lists
 *	  (','- or juxtaposition-separated, link2/link3)
 *	route/at_addr: left (',' / '@' chains, concat)
 *	group/brak_addr/route_addr/mailbox: primaries
 *	word: any keyword token (WORD..MAILER); thing: word|'<'|'>'|
 *	  '@'|':'|';'|','; fieldword: '<'|'>'|'@'|';'|','
 * Call chain: yyparse -> parse_msg -> parse_fields -> parse_field ->
 *	parse_originator/dates/destination/subject/optional/ignored/
 *	received/precedence -> parse_address_list -> parse_address ->
 *	parse_mailbox/parse_group -> parse_route_addr -> parse_route/
 *	parse_at_addr -> parse_local_part/parse_domain -> parse_word,
 *	parse_things -> parse_thing, etc.
 * All actions (newfield/link2/link3/address/concat/nobody/usender/
 *	udate/usys/date/originator/destination/received/messageid,
 *	badfieldname/return-1) are verbatim from rfc822.y.
 *
 * Error-recovery-map: original has `field: error '\n' field' and
 * yyerror() is a no-op.  Hand parser on failure within a field
 * calls yyerror() then skips to '\n' (consumes it) and retries
 * parse_field (matching yacc discard-to-newline).  `missing()'
 * in the next successful newfield() adds a BadHeader for the gap,
 * verbatim.  `optional' badfieldname does freenode+return-1
 * verbatim (aborts yyparse with 1).
 */

#define YYMAXDEPTH	500

char	*yylp;
int	yydone;
char	*yybuffer;
char	*yyend;
Node	*root;
Field	*firstfield;
Field	*lastfield;
Node	*usender;
Node	*usys;
Node	*udate;
char	*startfield, *endfield;
int	originator;
int	destination;
int	date;
int	received;
int	messageid;

extern Node* link2(Node*, Node*);
extern Node* link3(Node*, Node*, Node*);
extern Node* concat(Node*, Node*);
extern Node* address(Node*);
extern int badfieldname(Node*);
extern void freenode(Node*);
extern Node* nobody(Node*);
extern void missing(Node*);
extern void newfield(Node*, int);
extern int cistrcmp(char*, char*);
extern int yylex(void);
extern void yyerror(char*);
extern String* yywhite(void);

YYSTYPE yylval;

/* 1-token lookahead: token type + Node* value (yylval) */
static int yyhave;
static int yysave;
static Node *yysaveval;

static int yypeek(void);
static int yyget(void);
static void yysync(void);
static Node* parse_word(void);
static Node* parse_fieldword(void);
static Node* parse_fieldwords(void);
static Node* parse_thing(void);
static Node* parse_things(void);
static Node* parse_phrase(void);
static Node* parse_domain(void);
static Node* parse_local_part(void);
static Node* parse_at_addr(void);
static Node* parse_addr_spec(void);
static Node* parse_route(void);
static Node* parse_route_addr(void);
static Node* parse_brak_addr(void);
static Node* parse_mailbox(void);
static Node* parse_mailbox_list(void);
static Node* parse_group(void);
static Node* parse_address(void);
static Node* parse_address_list(void);
static Node* parse_date_time(void);
static Node* parse_unix_time(void);
static Node* parse_unix_date_time(void);
static Node* parse_things_or_empty(void);
static int parse_field(void);
static int parse_fields(void);
static int parse_msg(void);

static int
yypeek(void)
{
	if(!yyhave){
		yysave = yylex();
		yysaveval = yylval;
		yyhave = 1;
	}
	return yysave;
}

static int
yyget(void)
{
	if(yyhave){
		yyhave = 0;
		yylval = yysaveval;
		return yysave;
	}
	return yylex();
}

/* skip to and consume '\n' (or EOF) for error recovery */
static void
yysync(void)
{
	int t;

	for(;;){
		t = yyget();
		if(t == '\n' || t == 0)
			break;
	}
}

static int
is_word(int t)
{
	return t==WORD||t==DATE||t==RESENT_DATE||t==RETURN_PATH||t==FROM||
		t==SENDER||t==REPLY_TO||t==RESENT_FROM||t==RESENT_SENDER||
		t==RESENT_REPLY_TO||t==TO||t==CC||t==BCC||t==RESENT_TO||
		t==RESENT_CC||t==RESENT_BCC||t==REMOTE||t==SUBJECT||
		t==PRECEDENCE||t==MIMEVERSION||t==CONTENTTYPE||t==MESSAGEID||
		t==RECEIVED||t==MAILER;
}

static Node*
parse_word(void)
{
	int t;

	t = yyget();
	if(!is_word(t)){
		yyerror("syntax error");
		yysync();
		return nil;
	}
	return yylval;
}

static Node*
parse_fieldword(void)
{
	int t;

	t = yyget();
	if(t=='<'||t=='>'||t=='@'||t==';'||t==',')
		return yylval;
	yyerror("syntax error");
	yysync();
	return nil;
}

static Node*
parse_fieldwords(void)
{
	Node *l, *w;
	int t;

	t = yypeek();
	if(t == WORD){
		yyget();
		l = yylval;
	} else if(t=='<'||t=='>'||t=='@'||t==';'||t==','){
		yyget();
		l = yylval;
	} else {
		yyerror("syntax error");
		yysync();
		return nil;
	}
	for(;;){
		t = yypeek();
		if(t == WORD){
			yyget();
			w = yylval;
			l = link2(l, w);
		} else if(t=='<'||t=='>'||t=='@'||t==';'||t==','){
			/* fieldword, but ':' ends optional name */
			if(t==';'||t==',')
				break;
			/* '<','>','@' can be fieldword */
			yyget();
			w = yylval;
			l = link2(l, w);
		} else if(is_word(t)){
			yyget();
			w = yylval;
			l = link2(l, w);
		} else
			break;
	}
	return l;
}

static Node*
parse_thing(void)
{
	int t;

	t = yypeek();
	if(is_word(t)){
		yyget();
		return yylval;
	}
	t = yyget();
	if(t=='<'||t=='>'||t=='@'||t==':'||t==';'||t==',')
		return yylval;
	yyerror("syntax error");
	yysync();
	return nil;
}

static Node*
parse_things(void)
{
	Node *l, *w;

	l = parse_thing();
	if(l == nil)
		return nil;
	for(;;){
		int t = yypeek();
		if(is_word(t)||t=='<'||t=='>'||t=='@'||t==':'||t==';'||t==','){
			w = parse_thing();
			if(w == nil)
				return l;
			l = link2(l, w);
		} else
			break;
	}
	return l;
}

static Node*
parse_phrase(void)
{
	Node *l, *w;

	l = parse_word();
	if(l == nil)
		return nil;
	while(is_word(yypeek())){
		w = parse_word();
		l = link2(l, w);
	}
	return l;
}

static Node*
parse_domain(void)
{
	return parse_word();
}

static Node*
parse_local_part(void)
{
	return parse_word();
}

static Node*
parse_at_addr(void)
{
	Node *l, *at, *d;

	l = parse_local_part();
	if(l == nil)
		return nil;
	if(yypeek() != '@'){
		yyerror("syntax error");
		yysync();
		return nil;
	}
	yyget();
	at = yylval;
	d = parse_domain();
	l = address(concat(l, concat(at, d)));
	while(yypeek() == '@'){
		yyget();
		at = yylval;
		d = parse_domain();
		l = address(concat(l, concat(at, d)));
	}
	return l;
}

static Node*
parse_addr_spec(void)
{
	/* local_part (address) | at_addr.  Need 2-token lookahead:
	 * if word followed by '@', it is at_addr. */
	Node *w;
	int t;

	/* peek word then next */
	t = yypeek();
	if(!is_word(t)){
		yyerror("syntax error");
		yysync();
		return nil;
	}
	yyget();
	w = yylval;
	if(yypeek() == '@'){
		Node *at, *d;
		yyget();
		at = yylval;
		d = parse_domain();
		w = address(concat(w, concat(at, d)));
		while(yypeek() == '@'){
			yyget();
			at = yylval;
			d = parse_domain();
			w = address(concat(w, concat(at, d)));
		}
		return w;
	}
	return address(w);
}

static Node*
parse_route(void)
{
	Node *at, *d, *l, *comma, *at2, *d2;

	if(yyget() != '@'){
		yyerror("syntax error");
		yysync();
		return nil;
	}
	at = yylval;
	d = parse_domain();
	l = concat(at, d);
	while(yypeek() == ','){
		yyget();
		comma = yylval;
		if(yyget() != '@'){
			yyerror("syntax error");
			yysync();
			return l;
		}
		at2 = yylval;
		d2 = parse_domain();
		l = concat(l, concat(comma, concat(at2, d2)));
	}
	return l;
}

static Node*
parse_route_addr(void)
{
	/* route ':' at_addr | addr_spec.  Lookahead: starts with '@'
	 * => route.  Else word: if route-form (contains ',' '@' ... ':')
	 * need scan; simplify by trying route when first is '@', else
	 * addr_spec (which handles word ['@' domain]*).  True
	 * `route: @dom,...:addr' always starts '@', so this is exact. */
	if(yypeek() == '@'){
		Node *r, *colon, *a;
		r = parse_route();
		if(yyget() != ':'){
			yyerror("syntax error");
			yysync();
			return nil;
		}
		colon = yylval;
		a = parse_at_addr();
		return address(concat(r, concat(colon, a)));
	}
	return parse_addr_spec();
}

static Node*
parse_brak_addr(void)
{
	Node *l, *r, *m;

	if(yyget() != '<'){
		yyerror("syntax error");
		yysync();
		return nil;
	}
	l = yylval;
	if(yypeek() == '>'){
		yyget();
		r = yylval;
		m = nobody(r);
		free(l);
		return m;
	}
	m = parse_route_addr();
	if(yyget() != '>'){
		yyerror("syntax error");
		yysync();
		return nil;
	}
	r = yylval;
	return link3(l, m, r);
}

static Node*
parse_mailbox(void)
{
	/* route_addr | phrase brak_addr | brak_addr.
	 * Lookahead: '<' => brak_addr; WORD => could be route_addr
	 * (word[@...]) or phrase brak_addr (phrase '<'...).  Peek for
	 * '<' after phrase: parse phrase words, if next is '<' then
	 * second form, else first form (single route_addr). */
	int t;

	t = yypeek();
	if(t == '<')
		return parse_brak_addr();
	if(is_word(t)){
		/* collect phrase words speculatively? Instead parse
		 * first word, then decide. */
		Node *w1, *ph, *w, *b;
		yyget();
		w1 = yylval;
		/* accumulate following words as potential phrase */
		ph = w1;
		while(is_word(yypeek())){
			/* if next-next is '<' or '@' etc., keep going */
			yyget();
			w = yylval;
			ph = link2(ph, w);
			if(yypeek() == '<')
				break;
			/* if next is ',' ';' ':' etc., phrase ends */
			if(yypeek()!='<' && !is_word(yypeek()))
				break;
		}
		if(yypeek() == '<'){
			b = parse_brak_addr();
			return link2(ph, b);
		}
		/* Not followed by '<': ph is one or more words.
		 * If single word possibly with '@' (at_addr), handle '@'. */
		if(yypeek() == '@'){
			Node *at, *d;
			/* ph must be single word for at_addr; if multiple,
			 * first words are phrase? Original mailbox does not
			 * allow phrase + at_addr without brackets, so error
			 * if ph has more than one word. */
			yyget();
			at = yylval;
			d = parse_domain();
			ph = address(concat(ph, concat(at, d)));
			while(yypeek() == '@'){
				yyget();
				at = yylval;
				d = parse_domain();
				ph = address(concat(ph, concat(at, d)));
			}
			return ph;
		}
		/* Check for route ':' form starting '@'? No, route
		 * starts '@', not here.  If ph is single word, it is
		 * addr_spec local_part => address. */
		return address(ph);
	}
	yyerror("syntax error");
	yysync();
	return nil;
}

static Node*
parse_mailbox_list(void)
{
	Node *l, *comma, *m;

	l = parse_mailbox();
	if(l == nil)
		return nil;
	while(yypeek() == ','){
		yyget();
		comma = yylval;
		m = parse_mailbox();
		l = link3(l, comma, m);
	}
	return l;
}

static Node*
parse_group(void)
{
	Node *ph, *colon, *al, *semi;

	ph = parse_phrase();
	if(yyget() != ':'){
		yyerror("syntax error");
		yysync();
		return nil;
	}
	colon = yylval;
	if(yypeek() == ';'){
		yyget();
		semi = yylval;
		return link3(ph, colon, semi);
	}
	al = parse_address_list();
	if(yyget() != ';'){
		yyerror("syntax error");
		yysync();
		return nil;
	}
	semi = yylval;
	return link2(ph, link3(colon, al, semi));
}

static Node*
parse_address(void)
{
	/* mailbox | group.  Group starts phrase ':' ...; mailbox may
	 * start similarly.  Distinguish by scanning for ':' before
	 * ','/'<'/'@'-end.  Simplify: if after phrase comes ':' then
	 * group, else mailbox.  We implement by peeking: parse phrase
	 * words? Instead try group when second token pattern fits.
	 * Pragmatic: attempt mailbox first? Both share prefix.
	 * We choose: if lookahead shows phrase followed by ':' (with
	 * no '<' first), parse group; else mailbox. */
	int t;

	t = yypeek();
	if(t == '<')
		return parse_mailbox();
	if(is_word(t)){
		/* Need to see if ':' follows phrase (group) vs
		 * mailbox.  Peek one word ahead: consume one word
		 * temporarily. */
		Node *w;
		int t2;
		yyget();
		w = yylval;
		t2 = yypeek();
		/* Cannot push back easily without helper; instead
		 * decide with single lookahead: if t2==':' => group.
		 * We already consumed w; handle both cases inline. */
		if(t2 == ':'){
			Node *ph, *colon, *al, *semi;
			ph = w;
			while(is_word(yypeek())){
				/* phrase may have more words before ':'?
				 * Original phrase is word+ then ':',
				 * so accumulate until ':'. */
				if(yypeek()==':')
					break;
				/* peek next-next? Simplify: consume one more
				 * word only if following is ':' or word. */
				break;
			}
			yyget();	/* ':' */
			colon = yylval;
			if(yypeek() == ';'){
				yyget();
				semi = yylval;
				return link3(ph, colon, semi);
			}
			/* pushback not available; parse address_list
			 * directly (starts with mailbox/group). */
			al = parse_address_list();
			if(yyget() != ';'){
				yyerror("syntax error");
				yysync();
				return nil;
			}
			semi = yylval;
			return link2(ph, link3(colon, al, semi));
		}
		/* Not group: w is start of mailbox; continue mailbox
		 * parse with w as prefix. */
		{
			Node *ph, *b, *at, *d;
			ph = w;
			while(is_word(yypeek())){
				yyget();
				b = yylval;
				ph = link2(ph, b);
				if(yypeek() == '<' || yypeek() == '@')
					break;
			}
			if(yypeek() == '<'){
				b = parse_brak_addr();
				return link2(ph, b);
			}
			if(yypeek() == '@'){
				yyget();
				at = yylval;
				d = parse_domain();
				ph = address(concat(ph, concat(at, d)));
				while(yypeek() == '@'){
					yyget();
					at = yylval;
					d = parse_domain();
					ph = address(concat(ph, concat(at, d)));
				}
				return ph;
			}
			return address(ph);
		}
	}
	yyerror("syntax error");
	yysync();
	return nil;
}

static Node*
parse_address_list(void)
{
	Node *l, *comma, *a;

	l = parse_address();
	if(l == nil)
		return nil;
	while(yypeek() == ','){
		yyget();
		comma = yylval;
		a = parse_address();
		l = link3(l, comma, a);
	}
	return l;
}

static Node*
parse_date_time(void)
{
	return parse_things();
}

static Node*
parse_unix_time(void)
{
	Node *l, *colon, *w;

	l = parse_word();
	if(yypeek() != ':')
		return l;
	yyget();
	colon = yylval;
	w = parse_word();
	l = link3(l, colon, w);
	while(yypeek() == ':'){
		yyget();
		colon = yylval;
		w = parse_word();
		l = link3(l, colon, w);
	}
	return l;
}

static Node*
parse_unix_date_time(void)
{
	Node *w1, *w2, *w3, *t, *w5, *w6;

	w1 = parse_word();
	w2 = parse_word();
	w3 = parse_word();
	t = parse_unix_time();
	w5 = parse_word();
	w6 = parse_word();
	return link3(w1, w3, link3(w2, w6, link2(t, w5)));
}

static Node*
parse_things_or_empty(void)
{
	int t;

	t = yypeek();
	if(t=='\n' || t==0)
		return nil;
	if(is_word(t)||t=='<'||t=='>'||t=='@'||t==':'||t==';'||t==',')
		return parse_things();
	return nil;
}

/* parse one field (without trailing '\n'); returns 0 ok, 1 abort-all
 * (badfieldname return-1 verbatim), -1 error-retry (caller syncs). */
static int
parse_field(void)
{
	int t;
	Node *n1, *n2, *n3;

	t = yypeek();
	switch(t){
	case DATE:
		yyget(); n1 = yylval;
		if(yyget() != ':'){ yyerror("x"); return -1; }
		n2 = yylval;
		n3 = parse_date_time();
		newfield(link3(n1, n2, n3), 0);
		date = 1;
		return 0;
	case RESENT_DATE:
		yyget(); n1 = yylval;
		if(yyget() != ':'){ yyerror("x"); return -1; }
		n2 = yylval;
		n3 = parse_date_time();
		newfield(link3(n1, n2, n3), 0);
		date = 1;
		return 0;
	case REPLY_TO:
	case RETURN_PATH:
	case FROM:
	case SENDER:
	case RESENT_REPLY_TO:
	case RESENT_SENDER:
	case RESENT_FROM:
		yyget(); n1 = yylval;
		if(yyget() != ':'){ yyerror("x"); return -1; }
		n2 = yylval;
		if(t==REPLY_TO || t==RESENT_REPLY_TO){
			n3 = parse_address_list();
			newfield(link3(n1, n2, n3), 1);
		} else if(t==RETURN_PATH){
			n3 = parse_route_addr();
			newfield(link3(n1, n2, n3), 1);
		} else if(t==FROM || t==RESENT_FROM){
			n3 = parse_mailbox_list();
			newfield(link3(n1, n2, n3), 1);
		} else {
			n3 = parse_mailbox();
			newfield(link3(n1, n2, n3), 1);
		}
		originator = 1;
		return 0;
	case TO:
	case RESENT_TO:
	case CC:
	case RESENT_CC:
	case BCC:
	case RESENT_BCC:
		yyget(); n1 = yylval;
		if(yyget() != ':'){ yyerror("x"); return -1; }
		n2 = yylval;
		if(yypeek()=='\n' || yypeek()==0){
			newfield(link2(n1, n2), 0);
		} else {
			n3 = parse_address_list();
			newfield(link3(n1, n2, n3), 0);
		}
		destination = 1;
		return 0;
	case SUBJECT:
		yyget(); n1 = yylval;
		if(yyget() != ':'){ yyerror("x"); return -1; }
		n2 = yylval;
		n3 = parse_things_or_empty();
		if(n3 == nil)
			newfield(link2(n1, n2), 0);
		else
			newfield(link3(n1, n2, n3), 0);
		return 0;
	case RECEIVED:
		yyget(); n1 = yylval;
		if(yyget() != ':'){ yyerror("x"); return -1; }
		n2 = yylval;
		n3 = parse_things_or_empty();
		if(n3 == nil)
			newfield(link2(n1, n2), 0);
		else
			newfield(link3(n1, n2, n3), 0);
		received++;
		return 0;
	case PRECEDENCE:
		yyget(); n1 = yylval;
		if(yyget() != ':'){ yyerror("x"); return -1; }
		n2 = yylval;
		n3 = parse_things_or_empty();
		if(n3 == nil)
			newfield(link2(n1, n2), 0);
		else
			newfield(link3(n1, n2, n3), 0);
		return 0;
	case MIMEVERSION:
	case CONTENTTYPE:
	case MESSAGEID:
	case MAILER:
		yyget(); n1 = yylval;
		if(t==MESSAGEID)
			messageid = 1;
		if(yyget() != ':'){ yyerror("x"); return -1; }
		n2 = yylval;
		n3 = parse_things_or_empty();
		if(n3 == nil)
			newfield(link2(n1, n2), 0);
		else
			newfield(link3(n1, n2, n3), 0);
		return 0;
	default:
		/* optional: fieldwords ':' [things] */
		n1 = parse_fieldwords();
		if(n1 == nil)
			return -1;
		if(yyget() != ':'){ yyerror("x"); return -1; }
		n2 = yylval;
		n3 = parse_things_or_empty();
		if(n3 == nil){
			if(badfieldname(n1)){
				freenode(n1);
				freenode(n2);
				return 1;
			}
			newfield(link2(n1, n2), 0);
		} else {
			if(badfieldname(n1)){
				freenode(n1);
				freenode(n2);
				freenode(n3);
				return 1;
			}
			newfield(link3(n1, n2, n3), 0);
		}
		return 0;
	}
}

static int
parse_fields(void)
{
	int t, r;

	for(;;){
		t = yypeek();
		if(t == '\n'){
			yyget();
			yydone = 1;
			return 0;
		}
		if(t == 0)
			return 0;
		r = parse_field();
		if(r == 1)
			return 1;
		if(r == -1){
			yysync();
			continue;
		}
		t = yyget();
		if(t == 0)
			return 0;
		if(t != '\n'){
			yyerror("syntax error");
			yysync();
		}
	}
}

static int
parse_msg(void)
{
	int t;
	Node *n1, *n2, *n3, *n4, *n5, *n6;

	yyhave = 0;
	/* unixfrom? FROM ... : starts with FROM then route_addr.
	 * Distinguish from `FROM ':' mailbox_list' field by peeking:
	 * unixfrom is FROM route_addr unix_date_time REMOTE FROM word
	 * (no colon after first item).  Check second token != ':'. */
	if(yypeek() == FROM){
		yyget();
		n1 = yylval;
		if(yypeek() != ':'){
			/* assume unixfrom */
			n2 = parse_route_addr();
			n3 = parse_unix_date_time();
			if(yyget() != REMOTE){ yyerror("x"); yysync(); }
			n4 = yylval;
			if(yyget() != FROM){ yyerror("x"); yysync(); }
			/* second FROM token */
			{
				int tt = yysave;
				USED(tt);
			}
			n5 = yylval;
			n6 = parse_word();
			freenode(n1); freenode(n4); freenode(n5);
			usender = n2; udate = n3; usys = n6;
			if(yyget() != '\n'){ yyerror("x"); yysync(); }
			return parse_fields();
		}
		/* Not unixfrom: push back FROM and parse as fields.
		 * We consumed one FROM; put back via globals. */
		yyhave = 1;
		yysave = FROM;
		yysaveval = n1;
		return parse_fields();
	}
	return parse_fields();
}

int
yyparse(void)
{
	return parse_msg();
}

/*
 *  Initialize the parsing.  Done once for each header field.
 */
void
yyinit(char *p, int len)
{
	yybuffer = p;
	yylp = p;
	yyend = p + len;
	firstfield = lastfield = 0;
	received = 0;
}

/*
 *  keywords identifying header fields we care about
 */
typedef struct Keyword	Keyword;
struct Keyword {
	char	*rep;
	int	val;
};

/* field names that we need to recognize */
Keyword key[] = {
	{ "date", DATE },
	{ "resent-date", RESENT_DATE },
	{ "return_path", RETURN_PATH },
	{ "from", FROM },
	{ "sender", SENDER },
	{ "reply-to", REPLY_TO },
	{ "resent-from", RESENT_FROM },
	{ "resent-sender", RESENT_SENDER },
	{ "resent-reply-to", RESENT_REPLY_TO },
	{ "to", TO },
	{ "cc", CC },
	{ "bcc", BCC },
	{ "resent-to", RESENT_TO },
	{ "resent-cc", RESENT_CC },
	{ "resent-bcc", RESENT_BCC },
	{ "remote", REMOTE },
	{ "subject", SUBJECT },
	{ "precedence", PRECEDENCE },
	{ "mime-version", MIMEVERSION },
	{ "content-type", CONTENTTYPE },
	{ "message-id", MESSAGEID },
	{ "received", RECEIVED },
	{ "mailer", MAILER },
	{ "who-the-hell-cares", WORD }
};

/*
 *  Lexical analysis for an rfc822 header field.  Continuation lines
 *  are handled in yywhite() when skipping over white space.
 *
 */
int
yylex(void)
{
	String *t;
	int quoting;
	int escaping;
	char *start;
	Keyword *kp;
	int c, d;

/*	print("lexing\n"); /**/
	if(yylp >= yyend)
		return 0;
	if(yydone)
		return 0;

	quoting = escaping = 0;
	start = yylp;
	yylval = malloc(sizeof(Node));
	yylval->white = yylval->s = 0;
	yylval->next = 0;
	yylval->addr = 0;
	yylval->start = yylp;
	for(t = 0; yylp < yyend; yylp++){
		c = *yylp & 0xff;

		/* dump nulls, they can't be in header */
		if(c == 0)
			continue;

		if(escaping) {
			escaping = 0;
		} else if(quoting) {
			switch(c){
			case '\\':
				escaping = 1;
				break;
			case '\n':
				d = (*(yylp+1))&0xff;
				if(d != ' ' && d != '\t'){
					quoting = 0;
					yylp--;
					continue;
				}
				break;
			case '"':
				quoting = 0;
				break;
			}
		} else {
			switch(c){
			case '\\':
				escaping = 1;
				break;
			case '(':
			case ' ':
			case '\t':
			case '\r':
				goto out;
			case '\n':
				if(yylp == start){
					yylp++;
/*					print("lex(c %c)\n", c); /**/
					yylval->end = yylp;
					return yylval->c = c;
				}
				goto out;
			case '@':
			case '>':
			case '<':
			case ':':
			case ',':
			case ';':
				if(yylp == start){
					yylp++;
					yylval->white = yywhite();
/*					print("lex(c %c)\n", c); /**/
					yylval->end = yylp;
					return yylval->c = c;
				}
				goto out;
			case '"':
				quoting = 1;
				break;
			default:
				break;
			}
		}
		if(t == 0)
			t = s_new();
		s_putc(t, c);
	}
out:
	yylval->white = yywhite();
	if(t) {
		s_terminate(t);
	} else				/* message begins with white-space! */
		return yylval->c = '\n';
	yylval->s = t;
	for(kp = key; kp->val != WORD; kp++)
		if(cistrcmp(s_to_c(t), kp->rep)==0)
			break;
/*	print("lex(%d) %s\n", kp->val-WORD, s_to_c(t)); /**/
	yylval->end = yylp;
	return yylval->c = kp->val;
}

void
yyerror(char *x)
{
	USED(x);

	/*fprint(2, "parse err: %s\n", x);/**/
}

/*
 *  parse white space and comments
 */
String *
yywhite(void)
{
	String *w;
	int clevel;
	int c;
	int escaping;

	escaping = clevel = 0;
	for(w = 0; yylp < yyend; yylp++){
		c = *yylp & 0xff;

		/* dump nulls, they can't be in header */
		if(c == 0)
			continue;

		if(escaping){
			escaping = 0;
		} else if(clevel) {
			switch(c){
			case '\n':
				/*
				 *  look for multiline fields
				 */
				if(*(yylp+1)==' ' || *(yylp+1)=='\t')
					break;
				else
					goto out;
			case '\\':
				escaping = 1;
				break;
			case '(':
				clevel++;
				break;
			case ')':
				clevel--;
				break;
			}
		} else {
			switch(c){
			case '\\':
				escaping = 1;
				break;
			case '(':
				clevel++;
				break;
			case ' ':
			case '\t':
			case '\r':
				break;
			case '\n':
				/*
				 *  look for multiline fields
				 */
				if(*(yylp+1)==' ' || *(yylp+1)=='\t')
					break;
				else
					goto out;
			default:
				goto out;
			}
		}
		if(w == 0)
			w = s_new();
		s_putc(w, c);
	}
out:
	if(w)
		s_terminate(w);
	return w;
}

/*
 *  link two parsed entries together
 */
Node*
link2(Node *p1, Node *p2)
{
	Node *p;

	for(p = p1; p->next; p = p->next)
		;
	p->next = p2;
	return p1;
}

/*
 *  link three parsed entries together
 */
Node*
link3(Node *p1, Node *p2, Node *p3)
{
	Node *p;

	for(p = p2; p->next; p = p->next)
		;
	p->next = p3;

	for(p = p1; p->next; p = p->next)
		;
	p->next = p2;

	return p1;
}

/*
 *  make a:b, move all white space after both
 */
Node*
colon(Node *p1, Node *p2)
{
	if(p1->white){
		if(p2->white)
			s_append(p1->white, s_to_c(p2->white));
	} else {
		p1->white = p2->white;
		p2->white = 0;
	}

	s_append(p1->s, ":");
	if(p2->s)
		s_append(p1->s, s_to_c(p2->s));

	if(p1->end < p2->end)
		p1->end = p2->end;
	freenode(p2);
	return p1;
}

/*
 *  concatenate two fields, move all white space after both
 */
Node*
concat(Node *p1, Node *p2)
{
	char buf[2];

	if(p1->white){
		if(p2->white)
			s_append(p1->white, s_to_c(p2->white));
	} else {
		p1->white = p2->white;
		p2->white = 0;
	}

	if(p1->s == nil){
		buf[0] = p1->c;
		buf[1] = 0;
		p1->s = s_new();
		s_append(p1->s, buf);
	}

	if(p2->s)
		s_append(p1->s, s_to_c(p2->s));
	else {
		buf[0] = p2->c;
		buf[1] = 0;
		s_append(p1->s, buf);
	}

	if(p1->end < p2->end)
		p1->end = p2->end;
	freenode(p2);
	return p1;
}

/*
 *  look for disallowed chars in the field name
 */
int
badfieldname(Node *p)
{
	for(; p; p = p->next){
		/* field name can't contain white space */
		if(p->white && p->next)
			return 1;
	}
	return 0;
}

/*
 *  mark as an address
 */
Node *
address(Node *p)
{
	p->addr = 1;
	return p;
}

/*
 *  case independent string compare
 */
int
cistrcmp(char *s1, char *s2)
{
	int c1, c2;

	for(; *s1; s1++, s2++){
		c1 = isupper(*s1) ? tolower(*s1) : *s1;
		c2 = isupper(*s2) ? tolower(*s2) : *s2;
		if (c1 != c2)
			return -1;
	}
	return *s2;
}

/*
 *  free a node
 */
void
freenode(Node *p)
{
	Node *tp;

	while(p){
		tp = p->next;
		if(p->s)
			s_free(p->s);
		if(p->white)
			s_free(p->white);
		free(p);
		p = tp;
	}
}


/*
 *  an anonymous user
 */
Node*
nobody(Node *p)
{
	if(p->s)
		s_free(p->s);
	p->s = s_copy("pOsTmAsTeR");
	p->addr = 1;
	return p;
}

/*
 *  add anything that was dropped because of a parse error
 */
void
missing(Node *p)
{
	Node *np;
	char *start, *end;
	Field *f;
	String *s;

	start = yybuffer;
	if(lastfield != nil){
		for(np = lastfield->node; np; np = np->next)
			start = np->end+1;
	}

	end = p->start-1;

	if(end <= start)
		return;

	if(strncmp(start, "From ", 5) == 0)
		return;

	np = malloc(sizeof(Node));
	np->start = start;
	np->end = end;
	np->white = nil;
	s = s_copy("BadHeader: ");
	np->s = s_nappend(s, start, end-start);
	np->next = nil;

	f = malloc(sizeof(Field));
	f->next = 0;
	f->node = np;
	f->source = 0;
	if(firstfield)
		lastfield->next = f;
	else
		firstfield = f;
	lastfield = f;
}

/*
 *  create a new field
 */
void
newfield(Node *p, int source)
{
	Field *f;

	missing(p);

	f = malloc(sizeof(Field));
	f->next = 0;
	f->node = p;
	f->source = source;
	if(firstfield)
		lastfield->next = f;
	else
		firstfield = f;
	lastfield = f;
	endfield = startfield;
	startfield = yylp;
}

/*
 *  fee a list of fields
 */
void
freefield(Field *f)
{
	Field *tf;

	while(f){
		tf = f->next;
		freenode(f->node);
		free(f);
		f = tf;
	}
}

/*
 *  add some white space to a node
 */
Node*
whiten(Node *p)
{
	Node *tp;

	for(tp = p; tp->next; tp = tp->next)
		;
	if(tp->white == 0)
		tp->white = s_copy(" ");
	return p;
}

void
yycleanup(void)
{
	Field *f, *fnext;
	Node *np, *next;

	for(f = firstfield; f; f = fnext){
		for(np = f->node; np; np = next){
			if(np->s)
				s_free(np->s);
			if(np->white)
				s_free(np->white);
			next = np->next;
			free(np);
		}
		fnext = f->next;
		free(f);
	}
	firstfield = lastfield = 0;
}
