/*
 * FreeBSD install - a package for the installation and maintainance
 * of non-core utilities.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Jordan K. Hubbard
 * 18 July 1993
 *
 * Include and define various things wanted by the delete command.
 *
 * $FreeBSD: src/usr.sbin/pkg_install/delete/delete.h,v 1.8 2004/06/29 19:06:41 eik Exp $
 * $DragonFly: src/usr.sbin/pkg_install/delete/Attic/delete.h,v 1.3 2004/07/30 04:46:13 dillon Exp $
 */

#ifndef _INST_DELETE_H_INCLUDE
#define _INST_DELETE_H_INCLUDE

extern char	*Prefix;
extern Boolean	CleanDirs;
extern Boolean	Interactive;
extern Boolean	NoDeInstall;
extern Boolean	Recursive;
extern char	*Directory;
extern char	*PkgName;
extern match_t	MatchType;

#endif	/* _INST_DELETE_H_INCLUDE */
