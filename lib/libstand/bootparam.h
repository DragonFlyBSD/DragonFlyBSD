/*	$NetBSD: bootparam.h,v 1.3 1998/01/05 19:19:41 perry Exp $	*/

#ifndef _STAND_BOOTPARAM_H_
#define	_STAND_BOOTPARAM_H_

int bp_whoami(int sock);
int bp_getfile(int sock, char *key, struct in_addr *addrp, char *path);

#endif /* !_STAND_BOOTPARAM_H_ */
