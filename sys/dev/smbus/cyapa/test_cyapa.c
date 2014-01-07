
/*
 *
 */
#include <sys/types.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <bus/smbus/smb.h>
#include <bus/smbus/smbconf.h>

#define CYAPA_MAX_MT	5

struct cyapa_regs {
	uint8_t	stat;
	uint8_t fngr;

	struct {
		uint8_t	xy_high;	/* 7:4 high 4 bits of x */
		uint8_t x_low;		/* 3:0 high 4 bits of y */
		uint8_t y_low;
		uint8_t pressure;
		uint8_t id;		/* 1-15 incremented each touch */
	} touch[CYAPA_MAX_MT];
} __packed;

struct cyapa_cap {
	uint8_t	prod_ida[5];	/* 0x00 - 0x04 */
	uint8_t	prod_idb[6];	/* 0x05 - 0x0A */
	uint8_t	prod_idc[2];	/* 0x0B - 0x0C */
	uint8_t reserved[6];	/* 0x0D - 0x12 */
	uint8_t	buttons;	/* 0x13 */
	uint8_t	gen;		/* 0x14, low 4 bits */
	uint8_t max_abs_xy_high;/* 0x15 7:4 high x bits, 3:0 high y bits */
	uint8_t max_abs_x_low;	/* 0x16 */
	uint8_t max_abs_y_low;	/* 0x17 */
	uint8_t phy_siz_xy_high;/* 0x18 7:4 high x bits, 3:0 high y bits */
	uint8_t phy_siz_x_low;	/* 0x19 */
	uint8_t phy_siz_y_low;	/* 0x1A */
} __packed;

#define CYAPA_STAT_INTR		0x80
#define CYAPA_STAT_PWR_MASK	0x0C
#define  CYAPA_PWR_OFF		0x00
#define  CYAPA_PWR_IDLE		0x08
#define  CYAPA_PWR_ACTIVE	0x0C

#define CYAPA_STAT_DEV_MASK	0x03
#define  CYAPA_DEV_NORMAL	0x03
#define  CYAPA_DEV_BUSY		0x01

#define CYAPA_FNGR_DATA_VALID	0x08
#define CYAPA_FNGR_MIDDLE	0x04
#define CYAPA_FNGR_RIGHT	0x02
#define CYAPA_FNGR_LEFT		0x01
#define CYAPA_FNGR_NUMFINGERS(c) (((c) >> 4) & 0x0F)

#define CYAPA_TOUCH_X(regs, i)	((((regs)->touch[i].xy_high << 4) & 0x0F00) | \
				  (regs)->touch[i].x_low)
#define CYAPA_TOUCH_Y(regs, i)	((((regs)->touch[i].xy_high << 8) & 0x0F00) | \
				  (regs)->touch[i].y_low)
#define CYAPA_TOUCH_P(regs, i)	((regs)->touch[i].pressure)

#define CMD_DEV_STATUS		0x00
#define CMD_SOFT_RESET		0x28
#define CMD_POWER_MODE		0x29
#define CMD_QUERY_CAPABILITIES	0x2A

static char bl_exit[] = { 0x00, 0xff, 0xa5, 0x00, 0x01,
			  0x02, 0x03, 0x04, 0x05, 0x06,
			  0x07 };

static char bl_deactivate[] = { 0x00, 0xff, 0x3b, 0x00, 0x01,
				0x02, 0x03, 0x04, 0x05, 0x06,
				0x07 };

int
main(int ac, char **av)
{
	struct smbcmd cmd;
	char buf[256];
	int fd;
	int r;
	int i;
	int len;

	bzero(&cmd, sizeof(cmd));
	bzero(buf, sizeof(buf));

	fd = open("/dev/smb0-67", O_RDWR);
	printf("fd = %d\n", fd);

	cmd.slave = 0;
	cmd.rbuf = buf;

	cmd.cmd = CMD_DEV_STATUS;
	cmd.op = SMB_TRANS_NOCNT | SMB_TRANS_7BIT;
	cmd.rcount = 16;
	r = ioctl(fd, SMB_TRANS, &cmd);
	printf("status r = %d slave 0x%02x\n", r, cmd.slave);
	for (i = 0; i < cmd.rcount; ++i)
		printf(" %02x", (u_char)buf[i]);
	printf("\n");
	cmd.rcount = 0;

	/*
	 * bootloader idle
	 */
#if 1
	if ((buf[0] & 0x80) == 0) {
		if (buf[1] & 0x80) {
			printf("X2 BL BUSY\n");
		} else if (buf[2] & 0x20) {
			printf("X2 BL ACTIVE\n");
			cmd.cmd = 0x00;
			cmd.wbuf = bl_deactivate;
			cmd.wcount = sizeof(bl_deactivate);
			r = ioctl(fd, SMB_TRANS, &cmd);
			printf("r=%d\n", r);
		} else {
			printf("X2 BL IDLE\n");
			cmd.cmd = 0x00;
			cmd.wbuf = bl_exit;
			cmd.wcount = sizeof(bl_exit);
			r = ioctl(fd, SMB_TRANS, &cmd);
			printf("r=%d\n", r);
		}
		exit(0);
	}
#endif
#if 1
	{
		struct cyapa_cap *cap;

		cmd.cmd = CMD_QUERY_CAPABILITIES;
		cmd.wcount = 0;
		cmd.rcount = sizeof(struct cyapa_cap);
		r = ioctl(fd, SMB_TRANS, &cmd);
		cap = (void *)buf;

		printf("caps %5.5s-%6.6s-%2.2s left=%c midl=%c rght=%c\n",
			cap->prod_ida,
			cap->prod_idb,
			cap->prod_idc,
			((cap->buttons & CYAPA_FNGR_LEFT) ? 'y' : 'n'),
			((cap->buttons & CYAPA_FNGR_MIDDLE) ? 'y' : 'n'),
			((cap->buttons & CYAPA_FNGR_RIGHT) ? 'y' : 'n')
		);
		printf("caps pixx=%d pixy=%d\n",
			(cap->max_abs_xy_high << 4 & 0x0F00) |
			 cap->max_abs_x_low,
			(cap->max_abs_xy_high << 8 & 0x0F00) |
			 cap->max_abs_y_low);
	}
#endif
#if 1
	cmd.cmd = CMD_DEV_STATUS;

	while (1) {
		struct cyapa_regs *regs;
		int i;
		int nfingers;

		cmd.rcount = sizeof(struct cyapa_regs);
		r = ioctl(fd, SMB_TRANS, &cmd);
		regs = (void *)buf;

		nfingers = CYAPA_FNGR_NUMFINGERS(regs->fngr);

		printf("stat %02x buttons %c%c%c nfngrs=%d ",
			regs->stat,
			((regs->fngr & CYAPA_FNGR_LEFT) ? 'L' : '-'),
			((regs->fngr & CYAPA_FNGR_MIDDLE) ? 'L' : '-'),
			((regs->fngr & CYAPA_FNGR_RIGHT) ? 'L' : '-'),
			nfingers
		);
		fflush(stdout);
		for (i = 0; i < nfingers; ++i) {
			printf(" [x=%04d y=%04d p=%d]",
				CYAPA_TOUCH_X(regs, i),
				CYAPA_TOUCH_Y(regs, i),
				CYAPA_TOUCH_P(regs, i));
		}
		printf("\n");
		usleep(10000);
	}
#endif

	close(fd);

	return 0;
}
