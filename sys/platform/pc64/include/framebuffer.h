/*
 * Copyright (c) 2016 Imre Vadász <imre@vdsz.com>
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_FRAMEBUFFER_H_
#define _MACHINE_FRAMEBUFFER_H_

#ifdef _KERNEL

#include <sys/bus.h>

struct fb_info;
struct fb_var_screeninfo;
struct fb_cmap;

struct fb_ops {
	int (*fb_check_var)(struct fb_var_screeninfo *var, struct fb_info *info);
	int (*fb_set_par)(struct fb_info *);
	int (*fb_setcmap)(struct fb_cmap *cmap, struct fb_info *info);
	int (*fb_blank)(int, struct fb_info *);
	int (*fb_pan_display)(struct fb_var_screeninfo *var, struct fb_info *info);
	int (*fb_debug_enter)(struct fb_info *);
	int (*fb_debug_leave)(struct fb_info *info);
	int (*fb_ioctl)(struct fb_info *info, unsigned int cmd,
			unsigned long arg);
};

struct fb_info {
	vm_offset_t vaddr;
	vm_paddr_t paddr;
	uint16_t width;
	uint16_t height;
	uint16_t stride;
	uint16_t depth;
	int is_vga_boot_display;
	void *par;
	struct fb_ops fbops;
	device_t device;
};

int probe_efi_fb(int early);

int register_framebuffer(struct fb_info *fb_info);
void unregister_framebuffer(struct fb_info *fb_info);

extern struct fb_info efi_fb_info;

#endif  /* _KERNEL */

#endif /* !_MACHINE_FRAMEBUFFER_H_ */
