#include	<u.h>
#include	<libc.h>
#include	<bio.h>
#include	<mach.h>
		/* table for selecting machine-dependent parameters */

typedef	struct machtab Machtab;

struct machtab
{
	char		*name;			/* machine name */
	short		type;			/* executable type */
	short		boottype;		/* bootable type */
	int		asstype;		/* disassembler code */
	Mach		*mach;			/* machine description */
	Machdata	*machdata;		/* machine functions */
};

extern	Mach		mmips64, mamd64, mriscv64,
			marm64, mpower64, msparc64, m68020, malpha;
extern	Machdata	mipsmach, i386mach, riscv64mach,
			arm64mach, mipsmach2le, powermach, sparc64mach,
			m68020mach, alphamach;

/*
 *	machine selection table.  32-bit entries (68020, mips, sparc,
 *	386, arm, power, alpha, riscv) were removed with the 32-bit
 *	toolchains; only 64-bit entries remain.  machines with native
 *	disassemblers should follow the plan 9 variant in the table;
 *	native modes are selectable only by name.
 */
Machtab	machines[] =
{
	{	"68020",			/*68020*/
		F68020,
		F68020B,
		A68020,
		&m68020,
		&m68020mach,	},
	{	"68020",			/*Next 68040 bootable*/
		F68020,
		FNEXTB,
		A68020,
		&m68020,
		&m68020mach,	},
	{	"mips64LE",			/*plan 9 mips64 little endian*/
		FMIPS2LE,
		0,
		AMIPS,
		&mmips64,
		&mipsmach2le, 	},
	{	"mips64",			/*plan 9 mips64*/
		FMIPS2BE,
		FMIPSB,
		AMIPS,
		&mmips64,
		&mipsmach, 	},
	{	"amd64",			/*amd64*/
		FAMD64,
		FAMD64B,
		AAMD64,
		&mamd64,
		&i386mach,	},
	{	"power64",			/*PowerPC*/
		FPOWER64,
		FPOWER64B,
		APOWER64,
		&mpower64,
		&powermach,	},
	{	"alpha",			/*Alpha*/
		FALPHA,
		FALPHAB,
		AALPHA,
		&malpha,
		&alphamach,	},
	{	"sparc64",			/*plan 9 sparc64 */
		FSPARC64,
		FSPARCB,			/* XXX? */
		ASPARC64,
		&msparc64,
		&sparc64mach,	},
	{	"riscv64",
		FRISCV64,
		FRISCV64B,
		ARISCV64,
		&mriscv64,
		&riscv64mach,	},
	{	"arm64",
		FARM64,
		FARM64B,
		AARM64,
		&marm64,
		&arm64mach,	},
	{	0		},		/*the terminator*/
};

/*
 *	select a machine by executable file type
 */
void
machbytype(int type)
{
	Machtab *mp;

	for (mp = machines; mp->name; mp++){
		if (mp->type == type || mp->boottype == type) {
			asstype = mp->asstype;
			machdata = mp->machdata;
			break;
		}
	}
}
/*
 *	select a machine by name
 */
int
machbyname(char *name)
{
	Machtab *mp;

	if (!name) {
		asstype = AAMD64;
		machdata = &i386mach;
		mach = &mamd64;
		return 1;
	}
	for (mp = machines; mp->name; mp++){
		if (strcmp(mp->name, name) == 0) {
			asstype = mp->asstype;
			machdata = mp->machdata;
			mach = mp->mach;
			return 1;
		}
	}
	return 0;
}
