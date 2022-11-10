/*
 *  $Id: version.c,v 1.7 2022/04/03 22:38:16 tom Exp $
 *
 *  version.c -- dialog's version string
 *
 *  Copyright 2005-2010,2022	Thomas E. Dickey
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License, version 2.1
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to
 *	Free Software Foundation, Inc.
 *	51 Franklin St., Fifth Floor
 *	Boston, MA 02110, USA.
 */
#include <dlg_internals.h>

#define quoted(a)	#a
#define concat(a,b)	a "-" quoted(b)
#define DLG_VERSION	concat(DIALOG_VERSION,DIALOG_PATCHDATE)

const char *
dialog_version(void)
{
    return DLG_VERSION;
}
