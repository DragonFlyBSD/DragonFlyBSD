/* $DragonFly: src/lib/libcompat/regexp/regerror.c,v 1.2 2008/09/30 16:57:04 swildner Exp $ */

#include <regexp.h>
#include <stdio.h>

void
regerror(const char *s)
{
#ifdef ERRAVAIL
	error("regexp: %s", s);
#else
/*
	fprintf(stderr, "regexp(3): %s\n", s);
	exit(1);
*/
	return;	  /* let std. egrep handle errors */
#endif
	/* NOTREACHED */
}
