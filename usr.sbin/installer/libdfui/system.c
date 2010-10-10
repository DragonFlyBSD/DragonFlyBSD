/*
 * system.c
 * System definitions and capabilities.
 * $Id: system.c,v 1.5 2004/11/14 02:45:51 cpressey Exp $
 */

#include <sys/param.h>
#include <sys/sysctl.h>

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>

#include "dfui.h"
#include "system.h"

char *
ostype(void)
{
	int mib[2];
	size_t len;
	char *p;

	mib[0] = CTL_KERN;
	mib[1] = KERN_OSTYPE;
	sysctl(mib, 2, NULL, &len, NULL, 0);
	p = malloc(len);
	sysctl(mib, 2, p, &len, NULL, 0);
	return p;
}

int
has_caps(void)
{
#ifdef HAS_CAPS
	return 1;
#endif
	return 0;
}

int
has_npipe(void)
{
#ifdef HAS_NPIPE
	return 1;
#else
	return 0;
#endif
}

int
has_tcp(void)
{
#ifdef HAS_TCP
	return 1;
#else
	return 0;
#endif
}

/*
 * Get transport from transport name.
 *
 * return(0) if transport is not supported.
 * retirn(-1) if transport unknown.
 */
int
get_transport(const char *transport_name)
{
	if (strcmp(transport_name, "caps") == 0) {
		if (has_caps())
			return DFUI_TRANSPORT_CAPS;
		return(0);
	} else if (strcmp(transport_name, "npipe") == 0) {
		if (has_npipe())
			return DFUI_TRANSPORT_NPIPE;
		return(0);
	} else if (strcmp(transport_name, "tcp") == 0) {
		if (has_tcp())
			return DFUI_TRANSPORT_TCP;
		return(0);
	}
	return(-1);
}

/*
 * Get transport upon user request
 *
 * Print appropriate error message to stderr
 * and exit if transport not supported or unknown.
 */
int
user_get_transport(const char *transport_name)
{
	int transport;

	transport = get_transport(transport_name);

	if (transport == 0) {
		errx(EX_UNAVAILABLE, "Transport is not supported: ``%s''.\n",
		    transport_name);
	} else if (transport < 0) {
		errx(EX_CONFIG, "Wrong transport name: ``%s''.\n",
		    transport_name);
	}

	return(transport);
}
