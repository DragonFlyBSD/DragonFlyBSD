/* $DragonFly: src/usr.sbin/pcvt/vttest/Attic/header.h,v 1.3 2005/02/20 17:19:11 asmodai Exp $ */
#define VERSION "1.7b 1985-04-19"

#include <stdio.h>

#include <ctype.h>
#include <sgtty.h>
#include <signal.h>
#include <setjmp.h>
jmp_buf intrenv;
struct sgttyb sgttyOrg, sgttyNew;
char stdioBuf[BUFSIZ];
int brkrd, reading;
void	onterm(int);
void	onbrk(int);
int ttymode;
