/* $DragonFly: src/usr.bin/newkey/externs.h,v 1.1 2005/01/11 00:29:12 joerg Exp $ */

void	genkeys(char *, char *, char *);
int	localupdate(char *, char *, u_int, char *, char *);
int	mapupdate(char *, char *, u_int op, u_int, char *, u_int, char *);

int	xencrypt(char *, char *);
int	xdecrypt(char *, char *);
