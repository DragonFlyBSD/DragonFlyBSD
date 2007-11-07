/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sys/vfs/hammer/hammer_object.c,v 1.1 2007/11/07 00:43:24 dillon Exp $
 */

#include "hammer.h"

/*
 * Allocate a new filesystem object
 */
int
hammer_alloc_inode(struct hammer_transaction *trans, struct vattr *vap,
		   struct ucred *cred, struct hammer_inode **ipp)
{
	return(EINVAL);
}

int
hammer_add_directory(struct hammer_transaction *trans,
		     struct hammer_inode *dip, struct namecache *ncp,
		     struct hammer_inode *ip)
{
	return(EINVAL);
}

#if 0

/*
 * 
 */
int
hammer_add_record(struct hammer_transaction *trans,
		  struct hammer_inode *ip, hammer_record_ondisk_t rec,
		  void *data, int bytes)
{
}

/*
 * Delete records belonging to the specified range.  Deal with edge and
 * overlap cases.  This function sets the delete tid and breaks adds
 * up to two records to deal with edge cases, leaving the range as a gap.
 * The caller will then add records as appropriate.
 */
int
hammer_delete_records(struct hammer_transaction *trans,
		       struct hammer_inode *ip,
		       hammer_base_elm_t ran_beg, hammer_base_elm_t ran_end)
{
}

#endif
