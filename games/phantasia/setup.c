/*
 * setup.c - set up all files for Phantasia
 *
 * $FreeBSD: src/games/phantasia/setup.c,v 1.11 1999/11/16 02:57:34 billf Exp $
 */
#include "include.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void Error(const char *, const char *) __dead2;

static const char *const files[] = {		/* all files to create */
	_SPATH_MONST,
	_SPATH_PEOPLE,
	_SPATH_MESS,
	_SPATH_LASTDEAD,
	_SPATH_MOTD,
	_SPATH_GOLD,
	_SPATH_VOID,
	_SPATH_SCORE,
	NULL,
};

const char *monsterfile = "monsters.asc";

/*
 * FUNCTION: setup files for Phantasia 3.3.2
 *
 * GLOBAL INPUTS: Curmonster, _iob[], Databuf[], *Monstfp, Enrgyvoid
 *
 * GLOBAL OUTPUTS: Curmonster, Databuf[], *Monstfp, Enrgyvoid
 *
 * DESCRIPTION:
 *	This program tries to verify the parameters specified in
 *	the Makefile.
 *
 *	Create all necessary files.  Note that nothing needs to be
 *	put in these files.
 *	Also, the monster binary data base is created here.
 */

int
main(int argc, char *argv[])
{
	const char *const *filename;	/* for pointing to file names */
	int	fd;			/* file descriptor */
	FILE	*fp;			/* for opening files */
	struct stat	fbuf;		/* for getting files statistics */
	int ch;

	while ((ch = getopt(argc, argv, "m:")) != -1)
		switch(ch) {
		case 'm':
			monsterfile = optarg;
			break;
		case '?':
		default:
			break;
		}
	argc -= optind;
	argv += optind;

	srandomdev();

#if 0
	umask(0117);		/* only owner can read/write created files */
#endif

	/* try to create data files */
	filename = &files[0];
	/* create each file */
	while (*filename != NULL) {
		/* file exists; remove it */
		if (stat(*filename, &fbuf) == 0) {
			/* do not reset character file if it already exists */
			if (!strcmp(*filename, _SPATH_PEOPLE)) {
				++filename;
				continue;
			}

			if (unlink(*filename) < 0)
				Error("Cannot unlink %s.\n", *filename);
			/* NOTREACHED */
		}

		if ((fd = creat(*filename, 0666)) < 0)
			Error("Cannot create %s.\n", *filename);
		/* NOTREACHED */

		close(fd);			/* close newly created file */

		++filename;			/* process next file */
	}

	/* put holy grail info into energy void file */
	Enrgyvoid.ev_active = TRUE;
	Enrgyvoid.ev_x = ROLL(-1.0e6, 2.0e6);
	Enrgyvoid.ev_y = ROLL(-1.0e6, 2.0e6);
	if ((fp = fopen(_SPATH_VOID, "w")) == NULL)
		Error("Cannot update %s.\n", _SPATH_VOID);
	else {
		fwrite(&Enrgyvoid, SZ_VOIDSTRUCT, 1, fp);
		fclose(fp);
	}

	/* create binary monster data base */
	if ((Monstfp = fopen(_SPATH_MONST, "w")) == NULL)
		Error("Cannot update %s.\n", _SPATH_MONST);
	else if ((fp = fopen(monsterfile, "r")) == NULL) {
		fclose(Monstfp);
		Error("cannot open %s to create monster database.\n", "monsters.asc");
	} else {
		Curmonster.m_o_strength =
		    Curmonster.m_o_speed =
		    Curmonster.m_maxspeed =
		    Curmonster.m_o_energy =
		    Curmonster.m_melee =
		    Curmonster.m_skirmish = 0.0;

		/* read in text file, convert to binary */
		while (fgets(Databuf, SZ_DATABUF, fp) != NULL) {
			sscanf(&Databuf[24], "%lf%lf%lf%lf%lf%d%d%lf",
			    &Curmonster.m_strength, &Curmonster.m_brains,
			    &Curmonster.m_speed, &Curmonster.m_energy,
			    &Curmonster.m_experience, &Curmonster.m_treasuretype,
			    &Curmonster.m_type, &Curmonster.m_flock);
			Databuf[24] = '\0';
			strcpy(Curmonster.m_name, Databuf);
			fwrite((char *) &Curmonster, SZ_MONSTERSTRUCT, 1, Monstfp);
		}
		fclose(fp);
		fclose(Monstfp);
	}

	exit(0);
	/* NOTREACHED */
}

/*
 * FUNCTION: print an error message, and exit
 *
 * ARGUMENTS:
 *	char *str - format string for printf()
 *	char *file - file which caused error
 *
 * GLOBAL INPUTS: _iob[]
 *
 * DESCRIPTION:
 *	Print an error message, then exit.
 */

void
Error(const char *str, const char *file)
{
	fprintf(stderr, "Error: ");
	fprintf(stderr, str, file);
	perror(file);
	exit(1);
	/* NOTREACHED */
}
