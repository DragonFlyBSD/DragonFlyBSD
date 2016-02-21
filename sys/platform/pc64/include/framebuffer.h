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
/*
 * XXX If syscons isn't enabled in kernel config, provide a dummy
 *     inline implementation of register_framebuffer().
 */

extern struct fb_info efi_fb_info;
extern int have_efi_framebuffer;

#endif  /* _KERNEL */

#endif /* !_MACHINE_FRAMEBUFFER_H_ */
