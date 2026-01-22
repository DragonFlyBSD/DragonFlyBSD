#ifndef _SYS_DIRENT_H_
#define _SYS_DIRENT_H_

#include <sys/types.h>

#define MAXNAMLEN 255

struct dirent {
	ino_t d_ino;
	u_int16_t d_reclen;
	u_int8_t d_type;
	u_int8_t d_namlen;
	char d_name[MAXNAMLEN + 1];
};

#define d_fileno d_ino

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#define DT_DBF 15

#endif
