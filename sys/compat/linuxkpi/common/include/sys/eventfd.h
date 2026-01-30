/*-
 * Copyright (c) 2026 The DragonFly Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYS_EVENTFD_H_
#define _SYS_EVENTFD_H_

/* DragonFly eventfd compatibility for LinuxKPI (Linux eventfd emulation) */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/filedesc.h>

/* eventfd flags */
#define EFD_SEMAPHORE 0x00000001
#define EFD_CLOEXEC 0x00000002
#define EFD_NONBLOCK 0x00000004

/* eventfd structure - minimal implementation */
struct eventfd_ctx {
    uint64_t count;
    int flags;
    struct selinfo sel;
};

struct file;
struct eventfd_ctx;

/* eventfd operations - stubs */
static __inline struct eventfd_ctx *
eventfd_ctx_fdget(int fd)
{
    return NULL; /* Stub - would need proper implementation */
}

static __inline struct eventfd_ctx *
eventfd_ctx_fileget(struct file *file)
{
    return NULL; /* Stub */
}

static __inline void
eventfd_ctx_put(struct eventfd_ctx *ctx)
{
    /* Stub */
}

static __inline int
eventfd_signal(struct eventfd_ctx *ctx, int n)
{
    return 0; /* Stub */
}

static __inline int
eventfd_ctx_remove_wait_queue(struct eventfd_ctx *ctx, void *wait, uint64_t *cnt)
{
    return 0; /* Stub */
}

static __inline int
eventfd_ctx_read(struct eventfd_ctx *ctx, int no_wait, uint64_t *cnt)
{
    return 0; /* Stub */
}

static __inline int
eventfd_ctx_write(struct eventfd_ctx *ctx, int no_wait, uint64_t cnt)
{
    return 0; /* Stub */
}

static __inline struct file *
eventfd_fget(int fd)
{
    return NULL; /* Stub */
}

static __inline int
eventfd_create(struct eventfd_ctx **ctxp, unsigned int count, int flags)
{
    return ENODEV; /* Stub - not supported */
}

#endif /* _SYS_EVENTFD_H_ */
