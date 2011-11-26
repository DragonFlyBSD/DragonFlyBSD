/*-
 * David Leonard <d@openbsd.org>, 1999.  Public domain.
 *
 * $OpenBSD: list.h,v 1.3 2003/06/17 00:36:36 pjanzen Exp $
 * $DragonFly: src/games/hunt/hunt/list.h,v 1.1 2008/09/02 21:50:20 dillon Exp $
 */

struct driver {
	struct sockaddr addr;
	u_int16_t	response;
	int		once;
};

extern struct driver *drivers;
extern u_int16_t Server_port;

struct  driver *next_driver(void);
struct  driver *next_driver_fd(int);
const char *	driver_name(struct driver *);
void	probe_drivers(u_int16_t, char *);
void	probe_cleanup(void);
