/*
 * gs interface for page.
 * ps.c and pdf.c both use these routines.
 * a caveat: if you run more than one gs, only the last 
 * one gets killed by killgs 
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <bio.h>
#include "page.h"

static int gspid;	/* globals for atexit */
static int gsfd;
static void	killgs(void);

static void
killgs(void)
{
	char tmpfile[100];

	close(gsfd);
	postnote(PNGROUP, getpid(), "die");

	/*
	 * from ghostscript's use.txt:
	 * ``Ghostscript currently doesn't do a very good job of deleting temporary
	 * files when it exits; you may have to delete them manually from time to
	 * time.''
	 */
	sprint(tmpfile, "/tmp/gs_%.5da", (gspid+300000)%100000);
	if(chatty) fprint(2, "remove %s...\n", tmpfile);
	remove(tmpfile);
	sleep(100);
	postnote(PNPROC, gspid, "die yankee pig dog");
}

int
spawnwriter(GSInfo *g, Biobuf *b)
{
	char buf[4096];
	int n;
	int fd;

	switch(fork()){
	case -1:	return -1;
	case 0:	break;
	default:	return 0;
	}

	Bseek(b, 0, 0);
	fd = g->gsfd;
	while((n = Bread(b, buf, sizeof buf)) > 0)
		write(fd, buf, n);
	fprint(fd, "(/fd/3) (w) file dup (THIS IS NOT AN INFERNO BITMAP\\n) writestring flushfile\n");
	_exits(0);
	return -1;
}

int
spawnreader(int fd)
{
	int n, pfd[2];
	char buf[1024];

	if(pipe(pfd)<0)
		return -1;
	switch(fork()){
	case -1:
		return -1;
	case 0:
		break;
	default:
		close(pfd[0]);
		return pfd[1];
	}

	close(pfd[1]);
	switch(fork()){
	case -1:
		wexits("fork failed");
	case 0:
		while((n=read(fd, buf, sizeof buf)) > 0) {
			write(1, buf, n);
			write(pfd[0], buf, n);
		}
		break;
	default:
		while((n=read(pfd[0], buf, sizeof buf)) > 0) {
			write(1, buf, n);
			write(fd, buf, n);
		}
		break;
	}
	postnote(PNGROUP, getpid(), "i'm die-ing");
	_exits(0);
	return -1;
}

void
spawnmonitor(int fd)
{
	char buf[4096];
	char *xbuf;
	int n;
	int out;
	int first;

	switch(rfork(RFFDG|RFNOTEG|RFPROC)){
	case -1:
	default:
		return;

	case 0:
		break;
	}

	out = open("/dev/cons", OWRITE);
	if(out < 0)
		out = 2;

	xbuf = buf;	/* for ease of acid */
	first = 1;
	while((n = read(fd, xbuf, sizeof buf)) > 0){
		if(first){
			first = 0;
			fprint(2, "Ghostscript Error:\n");
		}
		write(out, xbuf, n);
		alarm(500);
	}
	_exits(0);
}

int
spawngs(GSInfo *g, char *safer)
{
	/* AFPL Ghostscript removed in commercial build: always fail. */
	USED(g);
	USED(safer);
	werrstr("ghostscript (AFPL) removed in commercial build");
	return -1;
}

int
gscmd(GSInfo *gs, char *fmt, ...)
{
	char buf[1024];
	int n;

	va_list v;
	va_start(v, fmt);
	n = vseprint(buf, buf+sizeof buf, fmt, v) - buf;
	if(n <= 0)
		return n;

	if(chatty) {
		fprint(2, "cmd: ");
		write(2, buf, n);
	}

	if(write(gs->gsfd, buf, n) != 0)
		return -1;

	return n;
}

/*
 * set the dimensions of the bitmap we expect to get back from GS.
 */
void
setdim(GSInfo *gs, Rectangle bbox, int ppi, int landscape)
{
	Rectangle pbox;

	if(chatty)
		fprint(2, "setdim: bbox=%R\n", bbox);

	if(ppi)
		gs->ppi = ppi;

	gscmd(gs, "mark\n");
	if(ppi)
		gscmd(gs, "/HWResolution [%d %d]\n", ppi, ppi);

	if(!Dx(bbox))
		bbox = Rect(0, 0, 612, 792);	/* 8½×11 */

	switch(landscape){
	case 0:
		pbox = bbox;
		break;
	case 1:
		pbox = Rect(bbox.min.y, bbox.min.x, bbox.max.y, bbox.max.x);
		break;
	}
	gscmd(gs, "/PageSize [%d %d]\n", Dx(pbox), Dy(pbox));
	gscmd(gs, "/Margins [%d %d]\n", -pbox.min.x, -pbox.min.y);
	gscmd(gs, "currentdevice putdeviceprops pop\n");
	gscmd(gs, "/#copies 1 store\n");

	if(!eqpt(bbox.min, ZP))
		gscmd(gs, "%d %d translate\n", -bbox.min.x, -bbox.min.y);

	switch(landscape){
	case 0:
		break;
	case 1:
		gscmd(gs, "%d 0 translate\n", Dy(bbox));
		gscmd(gs, "90 rotate\n");
		break;
	}

	waitgs(gs);
}

void
waitgs(GSInfo *gs)
{
	/* we figure out that gs is done by telling it to
	 * print something and waiting until it does.
	 */
	char *p;
	Biobuf *b = &gs->gsrd;
	uchar buf[1024];
	int n;

//	gscmd(gs, "(\\n**bstack\\n) print flush\n");
//	gscmd(gs, "stack flush\n");
//	gscmd(gs, "(**estack\\n) print flush\n");
	gscmd(gs, "(\\n//GO.SYSIN DD\\n) PAGE==\n");

	alarm(300*1000);
	for(;;) {
		p = Brdline(b, '\n');
		if(p == nil) {
			n = Bbuffered(b);
			if(n <= 0)
				break;
			if(n > sizeof buf)
				n = sizeof buf;
			Bread(b, buf, n);
			continue;
		}
		p[Blinelen(b)-1] = 0;
		if(chatty) fprint(2, "p: ");
		if(chatty) write(2, p, Blinelen(b)-1);
		if(chatty) fprint(2, "\n");
		if(strstr(p, "Error:")) {
			alarm(0);
			fprint(2, "ghostscript error: %s\n", p);
			wexits("gs error");
		}

		if(strstr(p, "//GO.SYSIN DD")) {
			break;
		}
	}
	alarm(0);
}
