/*
 * LUKS - Linux Unified Key Setup 
 *
 * Copyright (C) 2004-2006, Clemens Fruhwirth <clemens@endorphin.org>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "luks.h"
//#include "../lib/libcryptsetup.h"
#include "../lib/internal.h"
//#include "../lib/blockdev.h"

#define div_round_up(a,b) ({          \
	typeof(a) __a = (a);          \
	typeof(b) __b = (b);          \
	(__a - 1) / __b + 1;          \
})

static inline int round_up_modulo(int x, int m) {
	return div_round_up(x, m) * m;
}

static const char *cleaner_name=NULL;
static uint64_t cleaner_size = 0;
static int devfd=-1;

static int setup_mapping(const char *cipher, const char *name,
			 const char *device, unsigned int payloadOffset,
			 const char *key, size_t keyLength,
			 unsigned int sector, size_t srcLength,
			 int mode, struct crypt_device *ctx)
{
	int device_sector_size = sector_size_for_device(device);
	uint64_t size;

	/*
	 * we need to round this to nearest multiple of the underlying
	 * device's sector size, otherwise the mapping will be refused.
	 */
	if(device_sector_size < 0) {
		log_err(ctx, _("Unable to obtain sector size for %s"), device);
		return -EINVAL;
	}
	size = round_up_modulo(srcLength,device_sector_size)/SECTOR_SIZE;
	cleaner_size = size;

	return dm_create_device(name, device, cipher, "TEMP", NULL, size, 0, sector,
				keyLength, key, (mode == O_RDONLY), 0);
}

static void sigint_handler(int sig)
{
	if(devfd >= 0)
		close(devfd);
	devfd = -1;
	if(cleaner_name) 
		dm_remove_device(cleaner_name, 1, cleaner_size);

	signal(SIGINT, SIG_DFL);
	kill(getpid(), SIGINT);
}

static char *_error_hint(char *cipherName, char *cipherMode, size_t keyLength)
{
	char *hint = "";
#ifdef __linux__
	char c, tmp[4] = {0};
	struct utsname uts;
	int i = 0, kernel_minor;

	/* Nothing to suggest here */
	if (uname(&uts) || strncmp(uts.release, "2.6.", 4))
		return hint;

	/* Get kernel minor without suffixes */
	while (i < 3 && (c = uts.release[i + 4]))
		tmp[i++] = isdigit(c) ? c : '\0';
	kernel_minor = atoi(tmp);

	if (!strncmp(cipherMode, "xts", 3) && (keyLength != 256 && keyLength != 512))
		hint = _("Key size in XTS mode must be 256 or 512 bits.\n");
	else if (!strncmp(cipherMode, "xts", 3) && kernel_minor < 24)
		hint = _("Block mode XTS is available since kernel 2.6.24.\n");
	if (!strncmp(cipherMode, "lrw", 3) && (keyLength != 256 && keyLength != 512))
		hint = _("Key size in LRW mode must be 256 or 512 bits.\n");
	else if (!strncmp(cipherMode, "lrw", 3) && kernel_minor < 20)
		hint = _("Block mode LRW is available since kernel 2.6.20.\n");
#endif
	return hint;
}

/* This function is not reentrant safe, as it installs a signal
   handler and global vars for cleaning */
static int LUKS_endec_template(char *src, size_t srcLength, 
			       struct luks_phdr *hdr, 
			       char *key, size_t keyLength, 
			       const char *device, 
			       unsigned int sector,
			       ssize_t (*func)(int, void *, size_t),
			       int mode,
			       struct crypt_device *ctx)
{
	char *name = NULL;
	char *fullpath = NULL;
	char *dmCipherSpec = NULL;
	const char *dmDir = dm_get_dir(); 
	int r = -1;

	if(dmDir == NULL) {
		log_err(ctx, _("Failed to obtain device mapper directory."));
		return -1;
	}
	if(asprintf(&name,"temporary-cryptsetup-%d",getpid())               == -1 ||
	   asprintf(&fullpath,"%s/%s",dmDir,name)                           == -1 ||
	   asprintf(&dmCipherSpec,"%s-%s",hdr->cipherName, hdr->cipherMode) == -1) {
	        r = -ENOMEM;
		goto out1;
        }

	signal(SIGINT, sigint_handler);
	cleaner_name = name;

	r = setup_mapping(dmCipherSpec, name, device, hdr->payloadOffset,
			  key, keyLength, sector, srcLength, mode, ctx);
	if(r < 0) {
		log_err(ctx, _("Failed to setup dm-crypt key mapping for device %s.\n"
			"Check that kernel supports %s cipher (check syslog for more info).\n%s"),
			device, dmCipherSpec,
			_error_hint(hdr->cipherName, hdr->cipherMode, keyLength * 8));
		r = -EIO;
		goto out1;
	}

	devfd = open(fullpath, mode | O_DIRECT | O_SYNC);  /* devfd is a global var */
	if(devfd == -1) {
		log_err(ctx, _("Failed to open temporary keystore device.\n"));
		r = -EIO;
		goto out2;
	}

	r = func(devfd,src,srcLength);
	if(r < 0) {
		log_err(ctx, _("Failed to access temporary keystore device.\n"));
		r = -EIO;
		goto out3;
	}

	r = 0;
 out3:
	close(devfd);
	devfd = -1;
 out2:
	dm_remove_device(cleaner_name, 1, cleaner_size);
 out1:
	signal(SIGINT, SIG_DFL);
	cleaner_name = NULL;
	cleaner_size = 0;
	free(dmCipherSpec);
	free(fullpath);
	free(name);
	return r;
}

int LUKS_encrypt_to_storage(char *src, size_t srcLength,
			    struct luks_phdr *hdr,
			    char *key, size_t keyLength,
			    const char *device,
			    unsigned int sector,
			    struct crypt_device *ctx)
{
	return LUKS_endec_template(src,srcLength,hdr,key,keyLength, device, sector,
				   (ssize_t (*)(int, void *, size_t)) write_blockwise,
				   O_RDWR, ctx);
}

int LUKS_decrypt_from_storage(char *dst, size_t dstLength,
			      struct luks_phdr *hdr,
			      char *key, size_t keyLength,
			      const char *device,
			      unsigned int sector,
			      struct crypt_device *ctx)
{
	return LUKS_endec_template(dst,dstLength,hdr,key,keyLength, device,
				   sector, read_blockwise, O_RDONLY, ctx);
}
