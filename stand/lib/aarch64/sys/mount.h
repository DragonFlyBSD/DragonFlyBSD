/*
 * Minimal sys/mount.h shim for aarch64 libstand build.
 * The real SOPEN_MAX is defined in stand.h; this file
 * exists only to satisfy the #include in netif.c.
 */

#ifndef _AARCH64_SYS_MOUNT_H_
#define _AARCH64_SYS_MOUNT_H_

/* Empty - SOPEN_MAX comes from stand.h */

#endif /* _AARCH64_SYS_MOUNT_H_ */
