/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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

void 
print_inode(char* inode_string)
{
	printf("Printing the inode's contents of directory/file %s\n", inode_string);
	int fd = hammer2_ioctl_handle(inode_string);
	if (fd != -1) {
		hammer2_ioc_inode_t inode;
		int res = ioctl(fd, HAMMER2IOC_INODE_GET, &inode);
		hammer2_inode_data_t inode_data;
		inode_data = inode.ip_data;
		printf("Got res = %d\n", res);
		printf("Printing inode data.\n");
		/*printf("version = %d\n", inode_data.meta.version);
		printf("uflags = %d\n", inode_data.meta.uflags);
		printf("rmajor = %d\n", inode_data.meta.rmajor);
		printf("rminor = %d\n", inode_data.meta.rminor);
		printf("ctime = %u !\n", (unsigned int)inode_data.meta.ctime);
		printf("mtime = %u !\n", (unsigned int)inode_data.meta.mtime);*/
		printf("type = %d\n", inode_data.meta.type);
		printf("op_flags = %d\n", inode_data.meta.op_flags);
		/*printf("cap_flags = %d\n", inode_data.meta.cap_flags);
		printf("mode = %d\n", inode_data.meta.mode);
		printf("inum = %u !\n", (unsigned int)inode_data.meta.inum);
		printf("size = %u !\n", (unsigned int)inode_data.meta.size),*/
		printf("name_key = %u !\n", (unsigned int)inode_data.meta.name_key);
		/*printf("name_len = %d\n", inode_data.meta.name_len);
		printf("ncopies = %d\n", inode_data.meta.ncopies);*/
		printf("comp_algo = %d\n", inode_data.meta.comp_algo);
		if (inode_data.meta.op_flags != HAMMER2_OPFLAG_DIRECTDATA) {
			int i;
			for (i = 0; i < HAMMER2_SET_COUNT; ++i) {
				if (inode_data.u.blockset.blockref[i].type != HAMMER2_BREF_TYPE_EMPTY) {
					printf("blockrefs %d type = %d\n", i, inode_data.u.blockset.blockref[i].type);
					printf("blockrefs %d methods = %d\n", i, inode_data.u.blockset.blockref[i].methods);
					printf("blockrefs %d copyid = %d\n", i, inode_data.u.blockset.blockref[i].copyid);
					printf("blockrefs %d flags = %d\n", i, inode_data.u.blockset.blockref[i].flags);
					printf("blockrefs %d key = %u !\n", i, (unsigned int)inode_data.u.blockset.blockref[i].key);
				}
				else
					printf("blockrefs %d is empty.\n", i);
				}
			}
		else {
			printf("This inode has data instead of blockrefs.\n");
		}
	}
}
