/*
 * DEFS.H
 *
 * $DragonFly: src/sbin/rconfig/defs.h,v 1.2 2004/08/19 23:45:21 joerg Exp $
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

typedef struct tag {
    struct tag *next;
    const char *name;
    int flags;
} *tag_t;

#define PAS_ALPHA	0x0001
#define PAS_NUMERIC	0x0002
#define PAS_ANY		0x0004

extern const char *TagDir;
extern const char *WorkDir;
extern const char *ConfigFiles;
extern tag_t AddrBase;
extern tag_t VarBase;
extern int VerboseOpt;

extern void doServer(void);
extern void doClient(void);

const char *parse_str(char **scanp, int flags);
int udp_transact(struct sockaddr_in *sain, struct sockaddr_in *rsin, int *pfd,
		char **bufp, int *lenp, const char *ctl, ...);
int tcp_transact(struct sockaddr_in *sain, FILE **pfi, FILE **pfo, char **bufp,
		int *lenp, const char *ctl, ...);


