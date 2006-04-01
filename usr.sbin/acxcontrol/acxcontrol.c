/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com> and
 * Sascha Wildner <swildner@gmail.com>
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
 * $DragonFly: src/usr.sbin/acxcontrol/Attic/acxcontrol.c,v 1.1 2006/04/01 02:55:36 sephe Exp $
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <net/if.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#define SIOCSLOADFW     _IOW('i', 137, struct ifreq)    /* load firmware */
#define SIOCGRADIO      _IOW('i', 138, struct ifreq)    /* get radio type */
#define SIOCGSTATS      _IOW('i', 139, struct ifreq)    /* get acx stats */
#define SIOCSKILLFW     _IOW('i', 140, struct ifreq)    /* free firmware */
#define SIOCGFWVER      _IOW('i', 141, struct ifreq)    /* get firmware ver */
#define SIOCGHWID       _IOW('i', 142, struct ifreq)    /* get hardware id */

#define RADIO_FW_FMT	"radio%02x"

static int	do_req(const char *, unsigned long, void *);
static void	get_statistics(const char *);
static void	kill_firmware(const char *);
static void	load_firmware(const char *, const char *, int);
static void	mmap_file(const char *, uint8_t **, int *);
static void	usage(void);

struct firmware {
	uint8_t	*base_fw;
	int	base_fw_len;
	uint8_t	*radio_fw;
	int	radio_fw_len;
};

struct firmware_head {
	uint32_t	fwh_cksum;
	uint32_t	fwh_len;
};

struct statistic {
	int 		index;
	const char	*desc;
};

static const struct statistic tbl[] = {
	{  1, "Invalid param in TX description" },
	{  2, "No WEP key exists" },
	{  3, "MSDU timeouts" },
	{  4, "Excessive TX retries" },
	{  5, "Buffer overflows" },
	{  6, "DMA errors" },
	{  7, "Unknown errors" },
	{ -1, NULL }
};

static int
do_req(const char *iface, unsigned long req, void *data)
{
	int s;
	struct ifreq ifr;
	int error;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err(EX_OSERR, "Can't create socket");

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, iface, sizeof(ifr.ifr_name));
	ifr.ifr_data = data;
	error = ioctl(s, req, &ifr);

	close(s);

	return error;
}

static void
get_statistics(const char *iface)
{
	uint32_t i;
	uint64_t stats[16];
	const struct statistic *stt;

	if (do_req(iface, SIOCGHWID, &i) == -1)
		err(EX_OSERR, "Can't get hardware ID");
	printf("Hardware ID                    0x%x\n", i);

	if (do_req(iface, SIOCGFWVER, &i) == -1)
		err(EX_OSERR, "Can't get firmware version");
	printf("Firmware Version               0x%x\n", i);

	if (do_req(iface, SIOCGSTATS, &stats) == -1)
		err(EX_OSERR, "Can't get statistics");

	for (stt = tbl; stt->index != -1; stt++)
		printf("%-30s %qd\n", stt->desc, stats[stt->index]);
}

static void
kill_firmware(const char *iface)
{
	if (do_req(iface, SIOCSKILLFW, NULL) == -1)
		err(EX_OSERR, "Can't kill firmware");
}

static void
load_firmware(const char *iface, const char *filename, int uncombined)
{
	char radio_name[FILENAME_MAX];
	struct firmware fw;

	memset(&fw, 0, sizeof(fw));
	mmap_file(filename, &fw.base_fw, &fw.base_fw_len);

	if (uncombined) {
		uint8_t radio_type;

		if (do_req(iface, SIOCGRADIO, &radio_type) == -1)
			err(EX_OSERR, "Can't get radio type");
		snprintf(radio_name, FILENAME_MAX, "%s/" RADIO_FW_FMT ".bin",
			 dirname(filename), radio_type);
		mmap_file(radio_name, &fw.radio_fw, &fw.radio_fw_len);
	}

	do_req(iface, SIOCSLOADFW, &fw);
}

static void
mmap_file(const char *filename, uint8_t **addr, int *len)
{
	struct stat st;
	struct firmware_head *fwh;
	uint32_t cksum;
	uint8_t *p;
	int i, fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		err(EX_OSERR, "Can't open %s", filename);

	if (fstat(fd, &st) < 0)
		err(EX_OSERR, "Can't stat %s", filename);

	if (st.st_size <= sizeof(struct firmware_head))
		err(EX_SOFTWARE, "%s is too short", filename);

	fwh = mmap(NULL, st.st_size, PROT_READ, 0, fd, 0);
	if (fwh == NULL)
		err(EX_OSERR, "Can't map %s into memory", filename);

	if (fwh->fwh_len != st.st_size - sizeof(struct firmware_head))
		err(EX_SOFTWARE, "%s length mismatch", filename);

	cksum = 0;
	for (i = 0, p = (uint8_t *)&fwh->fwh_len;
	     i < st.st_size - sizeof(fwh->fwh_cksum);
	     ++i, ++p)
		cksum += *p;
	if (cksum != fwh->fwh_cksum)
		err(EX_SOFTWARE, "%s checksum mismatch", filename);

	*addr = (uint8_t *)(fwh + 1);
	*len = st.st_size - sizeof(struct firmware_head);

	close(fd);
}

static void
usage(void)
{
	fprintf(stderr, "usage: acxcontrol iface\n"
	    "       acxcontrol iface -f file [-r]\n"
	    "       acxcontrol iface -k\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	int c;
	int noflag = 1, kflag = 0, rflag = 0;
	const char *iface = NULL, *path = NULL;

	if (argc > 1 && argv[1][0] != '-') {
		iface = argv[1];
		optind++;
	}

	while ((c = getopt(argc, argv, "f:i:kr")) != -1) {
		if (c != 'i')
			noflag = 0;

		switch (c) {
		case 'f':
			path = optarg;
			break;
		case 'i':
			iface = optarg;
			break;
		case 'k':
			kflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		default:
			usage();
		}
	}

	if (iface == NULL)
		usage();

	if (kflag && ((path != NULL) || rflag))
		usage();

	if (kflag)
		kill_firmware(iface);

	if (path != NULL)
		load_firmware(iface, path, rflag);

	if (noflag)
		get_statistics(iface);

	return EX_OK;
}
