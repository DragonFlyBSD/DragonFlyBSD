/*
 * rconfig - Remote configurator
 *
 * 	rconfig [-W workingdir] [server_ip[:tag]]
 *	rconfig [-f configfile] -s
 *
 * $DragonFly: src/sbin/rconfig/rconfig.c,v 1.2 2004/06/18 04:26:53 dillon Exp $
 */

#include "defs.h"

const char *WorkDir = "/tmp";
const char *ConfigFiles = "/etc/defaults/rconfig.conf:/etc/rconfig.conf";
const char *TagDir = "/usr/local/etc/rconfig";
tag_t AddrBase;
tag_t VarBase;
int VerboseOpt;

static void usage(int code);
static void addTag(tag_t *basep, const char *tag, int flags);

int
main(int ac, char **av)
{
    int ch;
    int i;
    int serverMode = 0;
    
    while ((ch = getopt(ac, av, "aD:W:irt:f:sv")) != -1) {
	switch(ch) {
	case 'a':	/* auto tag / standard broadcast */
	    addTag(&AddrBase, NULL, 0);
	    break;
	case 'W':	/* specify working directory */
	    WorkDir = optarg;
	    break;
	case 'T':
	    TagDir = optarg;
	    break;
	case 'C':	/* specify server config file(s) (colon delimited) */
	    ConfigFiles = optarg;
	    break;
	case 's':	/* run as server using config file */
	    serverMode = 1;
	    break;
	case 'v':
	    VerboseOpt = 1;
	    break;
	default:
	    usage(1);
	    /* not reached */
	}
    }
    for (i = optind; i < ac; ++i) {
	if (strchr(av[i], '='))
	    addTag(&VarBase, av[i], 0);
	else
	    addTag(&AddrBase, av[i], 0);
    }
    if (AddrBase == NULL)
	usage(1);
    if (AddrBase && AddrBase->name == NULL && AddrBase->next) {
	fprintf(stderr,
		"You cannot specify both -a AND a list of hosts.  If you want\n"
		"to use auto-broadcast mode with a tag other then 'auto',\n"
		"just specify the tag without a host, e.g. ':<tag>'\n");
	exit(1);
    }
    if (serverMode)
	doServer();
    else
	doClient();
    return(0);
}

static
void
addTag(tag_t *basep, const char *name, int flags)
{
    tag_t tag = calloc(sizeof(struct tag), 1);

    while ((*basep) != NULL)
	basep = &(*basep)->next;

    tag->name = name;
    tag->flags = flags;
    *basep = tag;
}

static void
usage(int code)
{
    fprintf(stderr, "rconfig [-W workdir] [-f servconfig] "
		    "[-s] [var=data]* [server_ip[:tag]]* \n");
    exit(code);
}

