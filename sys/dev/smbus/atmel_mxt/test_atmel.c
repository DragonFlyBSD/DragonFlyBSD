
/*
 * Atmel mxt probably mxt1188s-a or mxt1664s-a or mxt3432s or mxt143e
 * I think its the mxt143e
 *
 * URL for original source material:
 *	http://www.atmel.com/products/touchsolutions/touchscreens/unlimited_touch.aspx
 * GIT for original source material:
 *	git://github.com/atmel-maxtouch/obp-utils.git
 *
 * cc obp-utils.c test_atmel.c -o /tmp/test -I/usr/src/sys/
 *
 * kldload smb before running test program to get /dev/smb*
 */
#include <sys/types.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <dev/smbus/smb/smb.h>
#include <bus/smbus/smbconf.h>

#include "obp-utils.h"

static const char *
mxt_gettypestring(int type)
{
	static const struct mxt_strinfo strinfo[] = { MXT_INIT_STRINGS };
	int i;

	for (i = 0; i < sizeof(strinfo) / sizeof(strinfo[0]); ++i) {
		if (strinfo[i].type == type)
			return(strinfo[i].id);
	}
	return("unknown");
}

struct mxt_object *
mxt_findobject(struct mxt_rollup *rup, int type)
{
	int i;

	for (i = 0; i < rup->nobjs; ++i) {
		if (rup->objs[i].type == type)
			return(&rup->objs[i]);
	}
	return NULL;
}

static int
mxt_read_reg(int fd, uint16_t reg, void *rbuf, int bytes)
{
	struct smbcmd cmd;
	uint8_t wbuf[2];
	int r;

	bzero(&cmd, sizeof(cmd));
	wbuf[0] = reg & 255;
	wbuf[1] = reg >> 8;
	cmd.slave = 0;
	cmd.rbuf = rbuf;
	cmd.wbuf = wbuf;
	cmd.wcount = 2;
	cmd.rcount = bytes;
	cmd.op = SMB_TRANS_NOCNT | SMB_TRANS_NOCMD | SMB_TRANS_7BIT;

	r = ioctl(fd, SMB_TRANS, &cmd);
	if (r == 0) {
		return cmd.rcount;
	} else {
		printf("status r = %d slave 0x%02x\n", r, cmd.slave);
		return -1;
	}
}

static int
mxt_write_reg_buf(int fd, uint16_t reg, void *xbuf, int bytes)
{
	struct smbcmd cmd;
	uint8_t wbuf[256];
	int r;

	assert(bytes < sizeof(wbuf) - 2);
	wbuf[0] = reg & 0xff;
	wbuf[1] = reg >> 8;
	bcopy(xbuf, wbuf + 2, bytes);

	cmd.slave = 0;
	cmd.rbuf = NULL;
	cmd.wbuf = wbuf;
	cmd.wcount = bytes + 2;
	cmd.rcount = 0;
	cmd.op = SMB_TRANS_NOCNT | SMB_TRANS_NOCMD | SMB_TRANS_7BIT;

	r = ioctl(fd, SMB_TRANS, &cmd);
	if (r == 0) {
		return 0;
	} else {
		printf("status r = %d slave 0x%02x\n", r, cmd.slave);
		return -1;
	}
}

static int
mxt_write_reg(int fd, uint16_t reg, uint8_t val)
{
	return mxt_write_reg_buf(fd, reg, &val, 1);
}

static int
mxt_read_object(int fd, struct mxt_object *obj, void *rbuf, int rbytes)
{
	uint16_t reg = obj->start_pos_lsb + (obj->start_pos_msb << 8);
	int bytes = obj->size_minus_one + 1;

	if (bytes > rbytes)
		bytes = rbytes;
	return mxt_read_reg(fd, reg, rbuf, bytes);
}


static int
mxt_t6_command(int fd, uint16_t t6cmd, uint8_t t6val, int waitforme)
{
	uint8_t status;

	mxt_write_reg(fd, 6 + t6cmd, t6val);

	while (waitforme) {
		if (mxt_read_reg(fd, 6 + t6cmd, &status, 1) != 1)
			break;
		if (status)
			return status;
	}
	return 0;
}

static
const char *
msgflagsstr(uint8_t flags)
{
	static char buf[9];

	buf[0] = (flags & MXT_MSGF_DETECT) ? 'D' : '.';
	buf[1] = (flags & MXT_MSGF_PRESS) ? 'P' : '.';
	buf[2] = (flags & MXT_MSGF_RELEASE) ? 'R' : '.';
	buf[3] = (flags & MXT_MSGF_MOVE) ? 'M' : '.';
	buf[4] = (flags & MXT_MSGF_VECTOR) ? 'V' : '.';
	buf[5] = (flags & MXT_MSGF_AMP) ? 'A' : '.';
	buf[6] = (flags & MXT_MSGF_SUPPRESS) ? 'S' : '.';
	buf[7] = (flags & MXT_MSGF_UNGRIP) ? 'U' : '.';

	return buf;
}

int
main(int ac, char **av)
{
	uint8_t rbuf[1024];
	struct mxt_rollup rup;
	struct mxt_object *obj;
	int fd;
	int i;
	int n;
	int r;
	size_t blksize;
	size_t totsize;
	uint32_t crc;

	fd = open("/dev/smb1-4a", O_RDWR);
	printf("fd = %d\n", fd);

	r = mxt_t6_command(fd, 0/*RESET*/, 0x01, 1);
	printf("reset result %d\n", r);

	n = mxt_read_reg(fd, 0, &rup.info, sizeof(rup.info));
	for (i = 0; i < n; ++i)
		printf(" %02x", ((uint8_t *)&rup.info)[i]);
	printf("\n");
	rup.nobjs = rup.info.num_objects;

	blksize = sizeof(rup.info) +
		  rup.nobjs * sizeof(struct mxt_object);
	totsize = blksize + sizeof(struct mxt_raw_crc);
	assert(totsize < sizeof(rbuf));

	n = mxt_read_reg(fd, 0, rbuf, totsize);
	if (n != totsize) {
		printf("mxt_read_reg: config failed: %d/%d\n", n, totsize);
		exit(1);
	}
	crc = obp_convert_crc((struct mxt_raw_crc *)(rbuf + blksize));
	if (obp_crc24(rbuf, blksize) != crc) {
		printf("info: crc failed %08x/%08x\n",
			crc,
			obp_crc24(rbuf, blksize));
		exit(1);
	}
	rup.objs = (void *)(rbuf + sizeof(rup.info));
	for (i = 0; i < rup.nobjs; ++i) {
		obj = &rup.objs[i];
		printf("object %d (%s) {\n",
			obj->type,
			mxt_gettypestring(obj->type));
		printf("    position = %d\n",
			obj->start_pos_lsb + (obj->start_pos_msb << 8));
		printf("    size     = %d\n",
			obj->size_minus_one + 1);
		printf("    instances= %d\n",
			obj->instances_minus_one + 1);
		printf("    numids   = %d\n",
			obj->num_report_ids);
	}
	printf("\n");
	fflush(stdout);

	obj = mxt_findobject(&rup, MXT_GEN_MESSAGEPROCESSOR);
	assert(obj != NULL);

	for (;;) {
		mxt_message_t msg;

		n = mxt_read_object(fd, obj, &msg, sizeof(msg));
		if (msg.any.reportid == 255)
			continue;
		for (i = 0; i < n; ++i)
			printf(" %02x", ((uint8_t *)&msg)[i]);
		printf("  trk=%02x f=%s x=%-4d y=%-4d p=%d amp=%d\n",
			msg.any.reportid,
			msgflagsstr(msg.touch.flags),
			(msg.touch.pos[0] << 4) |
				((msg.touch.pos[2] >> 4) & 0x0F),
			(msg.touch.pos[1] << 4) |
				((msg.touch.pos[2]) & 0x0F),
			msg.touch.area,
			msg.touch.amplitude);
		usleep(100000);
	}
}
