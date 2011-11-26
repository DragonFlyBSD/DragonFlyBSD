/* $FreeBSD: src/usr.bin/getopt/getopt.c,v 1.4.2.2 2001/07/30 10:16:38 dd Exp $ */
/* $DragonFly: src/usr.bin/getopt/getopt.c,v 1.4 2004/10/23 13:33:36 eirikn Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int c;
	int status = 0;

	optind = 2;	/* Past the program name and the option letters. */
	while ((c = getopt(argc, argv, argv[1])) != -1)
		switch (c) {
		case '?':
			status = 1;	/* getopt routine gave message */
			break;
		default:
			if (optarg != NULL)
				printf(" -%c %s", c, optarg);
			else
				printf(" -%c", c);
			break;
		}
	printf(" --");
	for (; optind < argc; optind++)
		printf(" %s", argv[optind]);
	printf("\n");
	return(status);
}
