#ifndef _MACHINE_FRAMEBUFFER_H_
#define _MACHINE_FRAMEBUFFER_H_

#ifdef _KERNEL

#include <sys/bus.h>

struct fb_info;

struct fb_ops {
	int (*fb_set_par)(struct fb_info *);
	int (*fb_blank)(int, struct fb_info *);
	int (*fb_debug_enter)(struct fb_info *);
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
