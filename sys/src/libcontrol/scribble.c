#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include <control.h>

/*
 * Handwriting (scribble) widget removed in commercial build.
 * libscribble carried Sun export-control + Compaq/CMU-untracked +
 * Packard advertising licenses, and no in-tree application creates
 * scribble controls (verified: jukebox, factotum/fgui clean).
 * The createscribble symbol is kept (see control.h) so existing code
 * still links, but it always fails.
 */
Control*
createscribble(Controlset *cs, char *name)
{
	USED(cs);
	USED(name);
	werrstr("scribble widget removed in commercial build");
	return nil;
}
