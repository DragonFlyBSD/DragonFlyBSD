#ifndef _MACHINE_FRAMEBUFFER_H_
#define _MACHINE_FRAMEBUFFER_H_

#ifdef _KERNEL

struct fb_info {
	vm_offset_t vaddr;
	vm_paddr_t paddr;
	uint16_t width;
	uint16_t height;
	uint16_t stride;
	uint16_t depth;
	int is_vga_boot_display;
	void *cookie;
	void (*restore)(void *);
	struct device *device;
};

int probe_efi_fb(int early);

int register_framebuffer(struct fb_info *fb_info);

extern struct fb_info efi_fb_info;

#endif  /* _KERNEL */

#endif /* !_MACHINE_FRAMEBUFFER_H_ */
