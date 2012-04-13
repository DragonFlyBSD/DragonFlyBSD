/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hammer2.h"

/*
 * Start-up the leaf daemon for a PFS on this machine.
 *
 * One leaf daemon is run for each mounted PFS.  The daemon may multi-thread
 * to improve performance if desired.  The daemon performs the following
 * functions:
 *
 *	(1) Makes and maintains connections to all cluster nodes found for
 *	    the PFS, retrieved from the REMOTE configuration stored in
 *	    the HAMMER2 mount.  A localhost connection is always implied
 *	    (using the backbone), but also having more direct connections
 *	    can result in higher performance.
 *
 *	    This also includes any required encryption or authentication.
 *
 *	(2) Runs the spanning tree protocol as a leaf, meaning that
 *	    the leaf daemon does not serve as a relay and the individual
 *	    connections made in (1) do not cross-connect.
 *
 *	(3) Obtains the PFS's registration and makes it available to the
 *	    cluster via the spanning tree protocol.
 *
 *	(4) Creates a communications pipe to the HAMMER2 VFS in the kernel
 *	    (installed via ioctl()) which the HAMMER2 VFS uses to accept and
 *	    communicate high-level requests.
 *
 *	(5) Performs all complex high-level messaging protocol operations,
 *	    such as quorum operations, maintains persistent cache state,
 *	    and so on and so forth.
 *
 * As you may have noted, the leaf daemon serves as an intermediary between
 * the kernel and the rest of the cluster.  The kernel will issue high level
 * protocol commands to the leaf which performs the protocol and sends a
 * response.  The kernel does NOT have to deal with the quorum or other
 * complex maintainance.
 *
 * Basically the kernel is simply another client from the point of view
 * of the high-level protocols, requesting cache state locks and such from
 * the leaf (in a degenerate situation one master lock is all that is needed).
 * If the kernel PFS has local media storage that storage can be used for
 * numerous purposes, such as caching, and in the degenerate non-clustered
 * case simply represents the one-and-only master copy of the filesystem.
 */
int
cmd_leaf(const char *sel_info)
{
	int ecode = 0;
	int fd;

	/*
	 * Obtain an ioctl descriptor and retrieve the registration info
	 * for the PFS.
	 */
	if ((fd = hammer2_ioctl_handle(sel_info)) < 0)
		return(1);

	/*
	 * Start a daemon to interconnect the HAMMER2 PFS in-kernel to the
	 * master-node daemon.  This daemon's thread will spend most of its
	 * time in the kernel.
	 */
/*	hammer2_demon(helper_pfs_interlink, (void *)(intptr_t)fd);*/
	if (NormalExit)
		close(fd);

	return ecode;
}

#if 0
/*
 * LEAF interconnect between PFS and the messaging core.  We create a
 * socket connection to the messaging core, register the PFS with the
 * core, and then pass the messaging descriptor to the kernel.
 *
 * The kernel takes over operation of the interconnect until the filesystem
 * is unmounted or the descriptor is lost or explicitly terminated via
 * a hammer2 command.
 *
 * This is essentially a localhost connection, so we don't have to worry
 * about encryption.  Any encryption will be handled by the messaging
 * core.
 */
static
void *
leaf_connect(void *data)
{
	int fd;

	fd = (int)(intptr_t)data;

	return (NULL);
}
#endif
