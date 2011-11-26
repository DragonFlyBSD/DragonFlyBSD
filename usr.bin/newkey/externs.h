/* $DragonFly: src/usr.bin/newkey/externs.h,v 1.2 2005/01/11 00:51:11 joerg Exp $ */

void	genkeys(char *, char *, char *);
int	localupdate(char *, char *, u_int, char *, char *);
int	mapupdate(char *, char *, u_int op, u_int, char *, u_int, char *);

int	xencrypt(char *, char *);
int	xdecrypt(char *, char *);

int	yp_update(char *, char *, unsigned int, char *, int, char *, int);
