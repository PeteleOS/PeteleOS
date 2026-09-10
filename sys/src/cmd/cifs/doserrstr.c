#include <u.h>
#include <libc.h>

/*
 * dos error strings for the cifs client.
 * rewritten for PeteleOS; error numbers are protocol facts
 * from the SMB specification (errdos/errsrv/errhrd classes).
 */

static struct {
	int	err;
	char	*msg;
} DOSerrs[] = {
	/* errdos class */
	{ (0<<16)|1,	"no error" },
	{ (1<<16)|1,	"bad function" },
	{ (2<<16)|1,	"file does not exist" },
	{ (3<<16)|1,	"directory does not exist" },
	{ (4<<16)|1,	"too many open files" },
	{ (5<<16)|1,	"permission denied" },
	{ (6<<16)|1,	"bad fid" },
	{ (7<<16)|1,	"memory arena trashed" },
	{ (8<<16)|1,	"out of memory" },
	{ (9<<16)|1,	"bad memory address" },
	{ (10<<16)|1,	"bad environment" },
	{ (12<<16)|1,	"bad open mode" },
	{ (13<<16)|1,	"bad data" },
	{ (14<<16)|1,	"reserved" },
	{ (15<<16)|1,	"bad drive" },
	{ (16<<16)|1,	"remove current directory" },
	{ (17<<16)|1,	"rename across filesystems" },
	{ (18<<16)|1,	"no more files" },
	{ (31<<16)|1,	"failure" },
	{ (32<<16)|1,	"sharing violation" },
	{ (33<<16)|1,	"lock violation" },
	{ (50<<16)|1,	"not supported" },
	{ (64<<16)|1,	"network name missing" },
	{ (66<<16)|1,	"bad ipc" },
	{ (67<<16)|1,	"bad share name" },
	{ (80<<16)|1,	"file exists" },
	{ (87<<16)|1,	"bad parameter" },
	{ (110<<16)|1,	"cannot open" },
	{ (122<<16)|1,  "buffer too small" },
	{ (123<<16)|1,	"bad name" },
	{ (124<<16)|1,	"unknown level" },
	{ (158<<16)|1,	"region locked" },

	{ (183<<16)|1,	"rename failed" },

	{ (230<<16)|1,	"bad pipe" },
	{ (231<<16)|1,	"pipe busy" },
	{ (232<<16)|1,	"close in progress" },
	{ (233<<16)|1,	"pipe has no reader" },
	{ (234<<16)|1,	"more data" },
	{ (259<<16)|1,	"no more items" },
	{ (267<<16)|1,	"bad directory in path" },
	{ (282<<16)|1,	"extended attributes" },
	{ (1326<<16)|1,	"authentication failed" },
	{ (2123<<16)|1,	"buffer too small" },
	{ (2142<<16)|1,	"unknown ipc" },
	{ (2151<<16)|1,	"no such print job" },
	{ (2455<<16)|1,	"bad group" },

	/* errsrv class */
	{ (1<<16)|2,	"error" },
	{ (2<<16)|2,	"bad password" },
	{ (3<<16)|2,	"reserved" },
	{ (4<<16)|2,	"permission denied" },
	{ (5<<16)|2,	"bad tid" },
	{ (6<<16)|2,	"bad server name" },
	{ (7<<16)|2,	"bad device" },
	{ (22<<16)|2,	"unknown smb" },
	{ (49<<16)|2,	"print queue full" },
	{ (50<<16)|2,	"spool file too big" },
	{ (52<<16)|2,	"bad print fid" },
	{ (64<<16)|2,	"bad command" },
	{ (65<<16)|2,	"server error" },
	{ (67<<16)|2,	"bad fid or path" },
	{ (68<<16)|2,	"reserved 68" },
	{ (69<<16)|2,	"bad access" },
	{ (70<<16)|2,	"reserved 70" },
	{ (71<<16)|2,	"bad attributes" },
	{ (81<<16)|2,	"message server paused" },
	{ (82<<16)|2,	"not receiving" },
	{ (83<<16)|2,	"no room" },
	{ (87<<16)|2,	"too many names" },
	{ (88<<16)|2,	"timed out" },
	{ (89<<16)|2,	"no resources" },
	{ (90<<16)|2,	"too many uids" },
	{ (91<<16)|2,	"bad uid" },
	{ (250<<16)|2,	"use mpx mode" },
	{ (251<<16)|2,	"use standard mode" },
	{ (252<<16)|2,	"resume mpx" },
	{ (0xffff<<16)|2, "not supported" },

	/* errhrd class */
	{ (19<<16)|3,	"read only media" },
	{ (20<<16)|3,	"unknown device" },
	{ (21<<16)|3,	"drive not ready" },
	{ (22<<16)|3,	"unknown command" },
	{ (23<<16)|3,	"crc error" },
	{ (24<<16)|3,	"bad request size" },
	{ (25<<16)|3,	"seek failed" },
	{ (26<<16)|3,	"bad media" },
	{ (27<<16)|3,	"bad sector" },
	{ (28<<16)|3,	"no paper" },
	{ (29<<16)|3,	"write fault" },
	{ (30<<16)|3,	"read fault" },
	{ (31<<16)|3,	"hardware failure" },
	{ (34<<16)|3,	"wrong disk" },
	{ (35<<16)|3,	"no fcb" },
	{ (36<<16)|3,	"share buffer full" },
	{ (39<<16)|3,	"disk full" },

};

char *
doserrstr(uint err)
{
	int i, match;
	char *class;
	static char buf[0xff];

	switch(err & 0xff){
 	case 1:
 		class = "dos";
 		break;
 	case 2:
 		class = "network";
 		break;
 	case 3:
 		class = "hardware";
 		break;
 	case 4:
 		class = "Xos";
 		break;
 	case 0xe1:
 		class = "mx1";
 		break;
 	case 0xe2:
 		class = "mx2";
 		break;
 	case 0xe3:
 		class = "mx3";
 		break;
 	case 0xff:
 		class = "packet";
 		break;
 	default:
 		class = "unknown";
 		break;
 	}

	match = -1;
	for(i = 0; i < nelem(DOSerrs); i++)
		if(DOSerrs[i].err == err)
			match = i;

	if(match != -1)
		snprint(buf, sizeof(buf), "%s, %s", class, DOSerrs[match].msg);
	else
		snprint(buf, sizeof(buf), "%s, %ud/0x%ux - unknown error",
			class, err >> 16, err >> 16);
	return buf;
}
