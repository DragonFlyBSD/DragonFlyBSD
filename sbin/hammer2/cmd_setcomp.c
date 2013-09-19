/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
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
 
int
cmd_setcomp(char* comp_string, char* file_string)
{
	int comp_method;
	if (strcmp(comp_string, "0") == 0) {
		printf("Will turn off compression on directory/file %s\n", file_string);
		comp_method = HAMMER2_COMP_NONE;
	} else if (strcmp(comp_string, "1") == 0) {
		printf("Will set zero-checking compression on directory/file %s.\n",
			file_string);
		comp_method = HAMMER2_COMP_AUTOZERO;
	} else if (strcmp(comp_string, "2") == 0) {
		printf("Will set LZ4 compression on directory/file %s.\n", file_string);
		comp_method = HAMMER2_COMP_LZ4;
	} else if (strcmp(comp_string, "3:6") == 0) {
		printf("Will set ZLIB level 6 compression on directory/file %s.\n", file_string);
		comp_method = 6 << 4;
		comp_method += HAMMER2_COMP_ZLIB;
	} else if (strcmp(comp_string, "3") == 0 || strcmp(comp_string, "3:7") == 0) {
		printf("Will set ZLIB level 7 (default) compression on directory/file %s.\n", file_string);
		comp_method = 7 << 4;
		comp_method += HAMMER2_COMP_ZLIB;
	} else if (strcmp(comp_string, "3:8") == 0) {
		printf("Will set ZLIB level 8 compression on directory/file %s.\n", file_string);
		comp_method = 8 << 4;
		comp_method += HAMMER2_COMP_ZLIB;
	} else if (strcmp(comp_string, "3:9") == 0) {
		printf("Will set ZLIB level 9 compression on directory/file %s.\n", file_string);
		printf("CAUTION: May be extremely slow on big amount of data.\n");
		comp_method = 9 << 4;
		comp_method += HAMMER2_COMP_ZLIB;
	} else if (strcmp(comp_string, "3:5") == 0 || strcmp(comp_string, "3:4") == 0 ||
				strcmp(comp_string, "3:3") == 0 || strcmp(comp_string, "3:2") == 0 ||
				strcmp(comp_string, "3:1") == 0) {
		printf("ZLIB compression levels below 6 are not supported,\n");
		printf("please use LZ4 (setcomp 2) for fast compression instead.\n");
		return 1;
	}
	else {
		printf("ERROR: Unknown compression method.\n");
		return 1;
	}
	int fd = hammer2_ioctl_handle(file_string);
	hammer2_ioc_inode_t inode;
	int res = ioctl(fd, HAMMER2IOC_INODE_GET, &inode);
	if (res < 0) {
		fprintf(stderr, "ERROR before setting the mode: %s\n",
			strerror(errno));
		return 3;
	}
	inode.ip_data.comp_algo = comp_method & 0x0FF;
	res = ioctl(fd, HAMMER2IOC_INODE_SET, &inode);
	if (res < 0) {
		if (errno != EINVAL) {
			fprintf(stderr, "ERROR after trying to set the mode: %s\n",
				strerror(errno));
			return 3;
		}
	}
	close(fd);
	return 0;
}

int
cmd_setcomp_recursive(char* option_string, char* comp_string, char* file_string)
{
	int ecode = 0;
	int set_files;
	if (strcmp(option_string, "-r") == 0) {
		set_files = 0;
	}
	else if (strcmp(option_string, "-rf") == 0) {
		set_files = 1;
	}
	else {
		printf("setcomp: Unrecognized option.\n");
		exit(1);
	}
	int comp_method;
	if (strcmp(comp_string, "0") == 0) {
		printf("Will turn off compression on directory/file %s\n", file_string);
		comp_method = HAMMER2_COMP_NONE;
	} else if (strcmp(comp_string, "1") == 0) {
		printf("Will set zero-checking compression on directory/file %s.\n", file_string);
		comp_method = HAMMER2_COMP_AUTOZERO;
	} else if (strcmp(comp_string, "2") == 0) {
		printf("Will set LZ4 compression on directory/file %s.\n", file_string);
		comp_method = HAMMER2_COMP_LZ4;
	} else if (strcmp(comp_string, "3") == 0) {
		printf("Will set ZLIB (slowest) compression on directory/file %s.\n", file_string);
		comp_method = HAMMER2_COMP_ZLIB;
	}
	else {
		printf("Unknown compression method.\n");
		return 1;
	}
	int fd = hammer2_ioctl_handle(file_string);
	hammer2_ioc_inode_t inode;
	int res = ioctl(fd, HAMMER2IOC_INODE_GET, &inode);
	if (res < 0) {
		fprintf(stderr, "ERROR before setting the mode: %s\n", strerror(errno));
		return 3;
	}
	if (inode.ip_data.type != HAMMER2_OBJTYPE_DIRECTORY) {
		printf("setcomp: the specified object is not a directory, nothing changed.\n");
		return 1;
	}
	printf("Attention: recursive compression mode setting demanded, this may take a while...\n");
	ecode = setcomp_recursive_call(file_string, comp_method, set_files);
	inode.ip_data.comp_algo = comp_method;
	res = ioctl(fd, HAMMER2IOC_INODE_SET, &inode);
	if (res < 0) {
		if (errno != EINVAL) {
			fprintf(stderr, "ERROR after trying to set the mode: %s\n", strerror(errno));
			return 3;
		}
	}
	close(fd);
	return ecode;
}

int
setcomp_recursive_call(char *directory, int comp_method, int set_files)
{
	int ecode = 0;
	DIR *dir;
	if ((dir = opendir (directory)) == NULL) {
        fprintf(stderr, "ERROR while trying to set the mode recursively: %s\n",
			strerror(errno));
		return 3;
    }
    struct dirent *dent;
    int lenght;
    lenght = strlen(directory);
    char name[HAMMER2_INODE_MAXNAME];
    strcpy(name, directory);
    name[lenght] = '/';
    ++lenght;
    errno = 0;
    dent = readdir(dir);
    while (dent != NULL && ecode == 0) {
		if ((strcmp(dent->d_name, ".") != 0) &&
		 (strcmp(dent->d_name, "..") != 0)) {
			strncpy(name + lenght, dent->d_name, HAMMER2_INODE_MAXNAME -
				lenght);
			int fd = hammer2_ioctl_handle(name);
			hammer2_ioc_inode_t inode;
			int res = ioctl(fd, HAMMER2IOC_INODE_GET, &inode);
			if (res < 0) {
				fprintf(stderr, "ERROR during recursion: %s\n",
					strerror(errno));
				return 3;
			}
			if (inode.ip_data.type == HAMMER2_OBJTYPE_DIRECTORY) {
				ecode = setcomp_recursive_call(name, comp_method, set_files);
				inode.ip_data.comp_algo = comp_method;
				res = ioctl(fd, HAMMER2IOC_INODE_SET, &inode);
			}
			else {
				if (set_files == 1 && inode.ip_data.type ==
						HAMMER2_OBJTYPE_REGFILE) {
					inode.ip_data.comp_algo = comp_method;
					res = ioctl(fd, HAMMER2IOC_INODE_SET, &inode);
				}
			}
			if (res < 0) {
				if (errno != EINVAL) {
					fprintf(stderr, "ERROR during recursion after trying"
						"to set the mode: %s\n",
						strerror(errno));
					return 3;
				}
			}
			close(fd);
		}
		errno = 0; //we must set errno to 0 before readdir()
		dent = readdir(dir);
	}
	closedir(dir);
	if (errno != 0) {
		fprintf(stderr, "ERROR during iteration: %s\n", strerror(errno));
		return 3;
    }
    return ecode;
}
