/* mdXhl.c * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/lib/libmd/mdXhl.c,v 1.19 2006/01/17 15:35:56 phk Exp $
 * $DragonFly: src/lib/libmd/mdXhl.c,v 1.3 2008/09/11 20:25:34 swildner Exp $
 */
/*
 * This code has been deprecated, do not put this in libmd or anywhere else please.
 * The few base system programs that use this code will .PATH it in.
 *
 * Note that libcrypto/lib[re]ssl provides the standard API that this file extends
 * for these functions.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sha.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "sha1hl.h"

#define LENGTH 20

char *
SHA1_End(SHA1_CTX *ctx, char *buf)
{
	int i;
	unsigned char digest[LENGTH];
	static const char hex[]="0123456789abcdef";

	if (!buf)
		buf = malloc(2*LENGTH + 1);
	if (!buf)
		return 0;
	SHA1_Final(digest, ctx);
	for (i = 0; i < LENGTH; i++) {
		buf[i+i] = hex[digest[i] >> 4];
		buf[i+i+1] = hex[digest[i] & 0x0f];
	}
	buf[i+i] = '\0';
	return buf;
}

char *
SHA1_File(const char *filename, char *buf)
{
	return (SHA1_FileChunk(filename, buf, 0, 0));
}

char *
SHA1_FileChunk(const char *filename, char *buf, off_t ofs, off_t len)
{
	unsigned char buffer[8192];
	SHA1_CTX ctx;
	struct stat stbuf;
	int f, i, e;
	off_t n;

	SHA1_Init(&ctx);
	f = open(filename, O_RDONLY);
	if (f < 0)
		return 0;
	if (fstat(f, &stbuf) < 0)
		return 0;
	if (ofs > stbuf.st_size)
		ofs = stbuf.st_size;
	if ((len == 0) || (len > stbuf.st_size - ofs))
		len = stbuf.st_size - ofs;
	if (lseek(f, ofs, SEEK_SET) < 0)
		return 0;
	n = len;
	i = 0;
	while (n > 0) {
		if ((size_t)n > sizeof(buffer))
			i = read(f, buffer, sizeof(buffer));
		else
			i = read(f, buffer, n);
		if (i < 0) 
			break;
		SHA1_Update(&ctx, buffer, i);
		n -= i;
	} 
	e = errno;
	close(f);
	errno = e;
	if (i < 0)
		return 0;
	return (SHA1_End(&ctx, buf));
}

char *
SHA1_Data (const void *data, unsigned int len, char *buf)
{
	SHA1_CTX ctx;

	SHA1_Init(&ctx);
	SHA1_Update(&ctx,data,len);
	return (SHA1_End(&ctx, buf));
}
