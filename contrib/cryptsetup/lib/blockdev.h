#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include <unistd.h>
#include <asm/types.h>
#include <sys/ioctl.h>
#include <sys/mount.h>

#if defined(__linux__) && defined(_IOR) && !defined(BLKGETSIZE64)
#	define BLKGETSIZE64	_IOR(0x12, 114, size_t)
#endif

#endif /* BLOCKDEV_H */
