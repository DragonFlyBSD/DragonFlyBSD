/*-
 * (MPSAFE ?)
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * Copyright (c) 1992-1998 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/fb/vga.c,v 1.9.2.1 2001/08/11 02:58:44 yokota Exp $
 */

#include "opt_vga.h"
#include "opt_fb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/fbio.h>
#include <sys/thread2.h>

#include <bus/isa/isareg.h>

#include <machine/clock.h>
#include <machine/md_var.h>
#ifdef __i386__
#include <machine/pc/bios.h>
#endif

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include "fbreg.h"
#include "vgareg.h"

#ifndef VGA_DEBUG
#define VGA_DEBUG		0
#endif

/* machine/pc/bios.h has got too much i386-specific stuff in it */
#ifndef BIOS_PADDRTOVADDR
#define BIOS_PADDRTOVADDR(x) (((x) - ISA_HOLE_START) + atdevbase)
#endif
int
vga_probe_unit(int unit, video_adapter_t *buf, int flags)
{
	video_adapter_t *adp;
	video_switch_t *sw;
	int error;

	sw = vid_get_switch(VGA_DRIVER_NAME);
	if (sw == NULL)
		return 0;
	error = (*sw->probe)(unit, &adp, NULL, flags);
	if (error)
		return error;
	bcopy(adp, buf, sizeof(*buf));
	return 0;
}

int
vga_attach_unit(int unit, vga_softc_t *sc, int flags)
{
	video_switch_t *sw;
	int error;

	sw = vid_get_switch(VGA_DRIVER_NAME);
	if (sw == NULL)
		return ENXIO;

	error = (*sw->probe)(unit, &sc->adp, NULL, flags);
	if (error)
		return error;
	return (*sw->init)(unit, sc->adp, flags);
}

/* cdev driver functions */

#ifdef FB_INSTALL_CDEV

struct ucred;

int
vga_open(cdev_t dev, vga_softc_t *sc, int flag, int mode, struct ucred *cred)
{
	if (sc == NULL)
		return ENXIO;
	if (mode & (O_CREAT | O_APPEND | O_TRUNC))
		return ENODEV;

	return genfbopen(&sc->gensc, sc->adp, flag, mode, cred);
}

int
vga_close(cdev_t dev, vga_softc_t *sc, int flag, int mode)
{
	return genfbclose(&sc->gensc, sc->adp, flag, mode);
}

int
vga_read(cdev_t dev, vga_softc_t *sc, struct uio *uio, int flag)
{
	return genfbread(&sc->gensc, sc->adp, uio, flag);
}

int
vga_write(cdev_t dev, vga_softc_t *sc, struct uio *uio, int flag)
{
	return genfbread(&sc->gensc, sc->adp, uio, flag);
}

int
vga_ioctl(cdev_t dev, vga_softc_t *sc, u_long cmd, caddr_t arg, int flag,
	  struct ucred *cred)
{
	return genfbioctl(&sc->gensc, sc->adp, cmd, arg, flag, cred);
}

int
vga_mmap(cdev_t dev, vga_softc_t *sc, vm_offset_t offset, int prot)
{
	return genfbmmap(&sc->gensc, sc->adp, offset, prot);
}

#endif /* FB_INSTALL_CDEV */

/* LOW-LEVEL */

#define probe_done(adp)		((adp)->va_flags & V_ADP_PROBED)
#define init_done(adp)		((adp)->va_flags & V_ADP_INITIALIZED)
#define config_done(adp)	((adp)->va_flags & V_ADP_REGISTERED)

/* various sizes */
#define V_MODE_MAP_SIZE		(M_VGA_CG320 + 1)
#define V_MODE_PARAM_SIZE	64

/* video adapter state buffer */
struct adp_state {
    int			sig;
#define V_STATE_SIG	0x736f6962
    u_char		regs[V_MODE_PARAM_SIZE];
};
typedef struct adp_state adp_state_t;

/* 
 * NOTE: `va_window' should have a virtual address, but is initialized
 * with a physical address in the following table, as verify_adapter()
 * will perform address conversion at run-time.
 */
static video_adapter_t biosadapter = {
    0, KD_VGA, VGA_DRIVER_NAME, 0, 0, V_ADP_COLOR, IO_VGA, 32,
    EGA_BUF_BASE, EGA_BUF_SIZE, CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 
    0, 0, 0, M_VGA_C80x25, M_C80x25, M_VGA_C80x25
};

/* video driver declarations */
static int			vga_configure(int flags);
       int			(*vga_sub_configure)(int flags);
#if 0
static int			vga_nop(void);
#endif
static int			vga_error(void);
static vi_probe_t		vga_probe;
static vi_init_t		vga_init;
static vi_get_info_t		vga_get_info;
static vi_query_mode_t		vga_query_mode;
static vi_set_mode_t		vga_set_mode;
static vi_save_font_t		vga_save_font;
static vi_load_font_t		vga_load_font;
static vi_show_font_t		vga_show_font;
static vi_save_palette_t	vga_save_palette;
static vi_load_palette_t	vga_load_palette;
static vi_set_border_t		vga_set_border;
static vi_save_state_t		vga_save_state;
static vi_load_state_t		vga_load_state;
static vi_set_win_org_t		vga_set_origin;
static vi_read_hw_cursor_t	vga_read_hw_cursor;
static vi_set_hw_cursor_t	vga_set_hw_cursor;
static vi_set_hw_cursor_shape_t	vga_set_hw_cursor_shape;
static vi_blank_display_t	vga_blank_display;
static vi_mmap_t		vga_mmap_buf;
static vi_ioctl_t		vga_dev_ioctl;
#ifndef VGA_NO_MODE_CHANGE
static vi_clear_t		vga_clear;
static vi_fill_rect_t		vga_fill_rect;
static vi_bitblt_t		vga_bitblt;
#else /* VGA_NO_MODE_CHANGE */
#define vga_clear		(vi_clear_t *)vga_error
#define vga_fill_rect		(vi_fill_rect_t *)vga_error
#define vga_bitblt		(vi_bitblt_t *)vga_error
#endif
static vi_diag_t		vga_diag;

static video_switch_t vgavidsw = {
	vga_probe,
	vga_init,
	vga_get_info,
	vga_query_mode,	
	vga_set_mode,
	vga_save_font,
	vga_load_font,
	vga_show_font,
	vga_save_palette,
	vga_load_palette,
	vga_set_border,
	vga_save_state,
	vga_load_state,
	vga_set_origin,
	vga_read_hw_cursor,
	vga_set_hw_cursor,
	vga_set_hw_cursor_shape,
	vga_blank_display,
	vga_mmap_buf,
	vga_dev_ioctl,
	vga_clear,
	vga_fill_rect,
	vga_bitblt,
	vga_error,
	vga_error,
	vga_diag,
};

VIDEO_DRIVER(vga, vgavidsw, vga_configure);

/* VGA BIOS standard video modes */
#define EOT		(-1)
#define NA		(-2)

static video_info_t bios_vmode[] = {
    /* CGA */
    { M_B40x25,     V_INFO_COLOR, 40, 25, 8,  8, 2, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_C40x25,     V_INFO_COLOR, 40, 25, 8,  8, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_B80x25,     V_INFO_COLOR, 80, 25, 8,  8, 2, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_C80x25,     V_INFO_COLOR, 80, 25, 8,  8, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    /* EGA */
    { M_ENH_B40x25, V_INFO_COLOR, 40, 25, 8, 14, 2, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_ENH_C40x25, V_INFO_COLOR, 40, 25, 8, 14, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_ENH_B80x25, V_INFO_COLOR, 80, 25, 8, 14, 2, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_ENH_C80x25, V_INFO_COLOR, 80, 25, 8, 14, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    /* VGA */
    { M_VGA_C40x25, V_INFO_COLOR, 40, 25, 8, 16, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_M80x25, 0,            80, 25, 8, 16, 2, 1,
      MDA_BUF_BASE, MDA_BUF_SIZE, MDA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_C80x25, V_INFO_COLOR, 80, 25, 8, 16, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    /* MDA */
    { M_EGAMONO80x25, 0,          80, 25, 8, 14, 2, 1,
      MDA_BUF_BASE, MDA_BUF_SIZE, MDA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    /* EGA */
    { M_ENH_B80x43, 0,            80, 43, 8,  8, 2, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_ENH_C80x43, V_INFO_COLOR, 80, 43, 8,  8, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    /* VGA */
    { M_VGA_M80x30, 0,            80, 30, 8, 16, 2, 1,
      MDA_BUF_BASE, MDA_BUF_SIZE, MDA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_C80x30, V_INFO_COLOR, 80, 30, 8, 16, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_M80x50, 0,            80, 50, 8,  8, 2, 1,
      MDA_BUF_BASE, MDA_BUF_SIZE, MDA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_C80x50, V_INFO_COLOR, 80, 50, 8,  8, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_M80x60, 0,            80, 60, 8,  8, 2, 1,
      MDA_BUF_BASE, MDA_BUF_SIZE, MDA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_C80x60, V_INFO_COLOR, 80, 60, 8,  8, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },

#ifndef VGA_NO_MODE_CHANGE

#ifdef VGA_WIDTH90
    { M_VGA_M90x25, 0,            90, 25, 8, 16, 2, 1,
      MDA_BUF_BASE, MDA_BUF_SIZE, MDA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_C90x25, V_INFO_COLOR, 90, 25, 8, 16, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_M90x30, 0,            90, 30, 8, 16, 2, 1,
      MDA_BUF_BASE, MDA_BUF_SIZE, MDA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_C90x30, V_INFO_COLOR, 90, 30, 8, 16, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_M90x43, 0,            90, 43, 8,  8, 2, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_C90x43, V_INFO_COLOR, 90, 43, 8,  8, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_M90x50, 0,            90, 50, 8,  8, 2, 1,
      MDA_BUF_BASE, MDA_BUF_SIZE, MDA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_C90x50, V_INFO_COLOR, 90, 50, 8,  8, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_M90x60, 0,            90, 60, 8,  8, 2, 1,
      MDA_BUF_BASE, MDA_BUF_SIZE, MDA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
    { M_VGA_C90x60, V_INFO_COLOR, 90, 60, 8,  8, 4, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_TEXT },
#endif /* VGA_WIDTH90 */

    /* CGA */
    { M_BG320,      V_INFO_COLOR | V_INFO_GRAPHICS, 320, 200, 8,  8, 2, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_CGA },
    { M_CG320,      V_INFO_COLOR | V_INFO_GRAPHICS, 320, 200, 8,  8, 2, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_CGA },
    { M_BG640,      V_INFO_COLOR | V_INFO_GRAPHICS, 640, 200, 8,  8, 1, 1,
      CGA_BUF_BASE, CGA_BUF_SIZE, CGA_BUF_SIZE, 0, 0, V_INFO_MM_CGA },
    /* EGA */
    { M_CG320_D,    V_INFO_COLOR | V_INFO_GRAPHICS, 320, 200, 8,  8, 4, 4,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, GRAPHICS_BUF_SIZE, 0, 0,
      V_INFO_MM_PLANAR },
    { M_CG640_E,    V_INFO_COLOR | V_INFO_GRAPHICS, 640, 200, 8,  8, 4, 4,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, GRAPHICS_BUF_SIZE, 0, 0 ,
      V_INFO_MM_PLANAR },
    { M_EGAMONOAPA, V_INFO_GRAPHICS,                640, 350, 8, 14, 4, 4,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, 64*1024, 0, 0 ,
      V_INFO_MM_PLANAR },
    { M_ENHMONOAPA2,V_INFO_GRAPHICS,                640, 350, 8, 14, 4, 4,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, GRAPHICS_BUF_SIZE, 0, 0 ,
      V_INFO_MM_PLANAR },
    { M_CG640x350,  V_INFO_COLOR | V_INFO_GRAPHICS, 640, 350, 8, 14, 2, 2,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, GRAPHICS_BUF_SIZE, 0, 0 ,
      V_INFO_MM_PLANAR },
    { M_ENH_CG640,  V_INFO_COLOR | V_INFO_GRAPHICS, 640, 350, 8, 14, 4, 4,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, GRAPHICS_BUF_SIZE, 0, 0 ,
      V_INFO_MM_PLANAR },
    /* VGA */
    { M_BG640x480,  V_INFO_COLOR | V_INFO_GRAPHICS, 640, 480, 8, 16, 4, 4,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, GRAPHICS_BUF_SIZE, 0, 0 ,
      V_INFO_MM_PLANAR },
    { M_CG640x480,  V_INFO_COLOR | V_INFO_GRAPHICS, 640, 480, 8, 16, 4, 4,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, GRAPHICS_BUF_SIZE, 0, 0 ,
      V_INFO_MM_PLANAR },
    { M_VGA_CG320,  V_INFO_COLOR | V_INFO_GRAPHICS, 320, 200, 8,  8, 8, 1,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, GRAPHICS_BUF_SIZE, 0, 0,
      V_INFO_MM_PACKED, 1 },
    { M_VGA_MODEX,  V_INFO_COLOR | V_INFO_GRAPHICS, 320, 240, 8,  8, 8, 4,
      GRAPHICS_BUF_BASE, GRAPHICS_BUF_SIZE, GRAPHICS_BUF_SIZE, 0, 0,
      V_INFO_MM_VGAX, 1 },
#endif /* VGA_NO_MODE_CHANGE */

    { EOT },
};

static int		vga_init_done = FALSE;
#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
static u_char		*video_mode_ptr = NULL;
#endif
static u_char		*mode_map[V_MODE_MAP_SIZE];
static adp_state_t	adpstate;
static adp_state_t	adpstate2;
static int		rows_offset = 1;

/* local macros and functions */
#define BIOS_SADDRTOLADDR(p) ((((p) & 0xffff0000) >> 12) + ((p) & 0x0000ffff))

#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
static void map_mode_table(u_char **, u_char *);
static int map_mode_num(int);
#endif
static int map_bios_mode_num(int);
static u_char *get_mode_param(int);
static int verify_adapter(video_adapter_t *);
static void update_adapter_info(video_adapter_t *, video_info_t *);
static int probe_adapters(void);
static int set_line_length(video_adapter_t *, int);
static int set_display_start(video_adapter_t *, int, int);

#ifndef VGA_NO_MODE_CHANGE
#ifdef VGA_WIDTH90
static void set_width90(adp_state_t *);
#endif
#endif /* !VGA_NO_MODE_CHANGE */

#ifndef VGA_NO_FONT_LOADING
#define PARAM_BUFSIZE	6
static void set_font_mode(video_adapter_t *, u_char *);
static void set_normal_mode(video_adapter_t *, u_char *);
#endif

#ifndef VGA_NO_MODE_CHANGE
static void filll_io(int, vm_offset_t, size_t);
static void planar_fill(video_adapter_t *, int);
static void packed_fill(video_adapter_t *, int);
static void direct_fill(video_adapter_t *, int);
#ifdef notyet
static void planar_fill_rect(video_adapter_t *, int, int, int, int, int);
static void packed_fill_rect(video_adapter_t *, int, int, int, int, int);
static void direct_fill_rect16(video_adapter_t *, int, int, int, int, int);
static void direct_fill_rect24(video_adapter_t *, int, int, int, int, int);
static void direct_fill_rect32(video_adapter_t *, int, int, int, int, int);
#endif /* notyet */
#endif /* !VGA_NO_MODE_CHANGE */

#define	ISMAPPED(pa, width)				\
	(((pa) <= (u_long)0x1000 - (width)) 		\
	 || ((pa) >= ISA_HOLE_START && (pa) <= 0x100000 - (width)))

#define	prologue(adp, flag, err)			\
	if (!vga_init_done || !((adp)->va_flags & (flag)))	\
	    return (err)

/* a backdoor for the console driver */
static int
vga_configure(int flags)
{
    probe_adapters();
    if (probe_done(&biosadapter)) {
	biosadapter.va_flags |= V_ADP_INITIALIZED;
	if (!config_done(&biosadapter) && !(vid_register(&biosadapter) < 0))
	    biosadapter.va_flags |= V_ADP_REGISTERED;
    }
    if (vga_sub_configure != NULL)
	(*vga_sub_configure)(flags);

    return 1;
}

/* local subroutines */

#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
/* construct the mode parameter map (accept 40x25, 80x25 and 80x30 modes) */
static void
map_mode_table(u_char *map[], u_char *table)
{
    int i, valid;

    for(i = 0; i < V_MODE_MAP_SIZE; ++i) {
	map[i] = table + i*V_MODE_PARAM_SIZE;
	valid = 0;
	if ((map[i][0] == 40 && map[i][1] == 24) ||
	    (map[i][0] == 80 && (map[i][1] == 24 || map[i][1] == 29)))
	    valid++;
	if (!valid)
	    map[i] = NULL;
    }
}
#endif /* !VGA_NO_BIOS && !VGA_NO_MODE_CHANGE */

#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
/* map the non-standard video mode to a known mode number */
static int
map_mode_num(int mode)
{
    static struct {
        int from;
        int to;
    } mode_map[] = {
        { M_ENH_B80x43, M_ENH_B80x25 },
        { M_ENH_C80x43, M_ENH_C80x25 },
        { M_VGA_M80x30, M_VGA_M80x25 },
        { M_VGA_C80x30, M_VGA_C80x25 },
        { M_VGA_M80x50, M_VGA_M80x25 },
        { M_VGA_C80x50, M_VGA_C80x25 },
        { M_VGA_M80x60, M_VGA_M80x25 },
        { M_VGA_C80x60, M_VGA_C80x25 },
#ifdef VGA_WIDTH90
        { M_VGA_M90x25, M_VGA_M80x25 },
        { M_VGA_C90x25, M_VGA_C80x25 },
        { M_VGA_M90x30, M_VGA_M80x25 },
        { M_VGA_C90x30, M_VGA_C80x25 },
        { M_VGA_M90x43, M_ENH_B80x25 },
        { M_VGA_C90x43, M_ENH_C80x25 },
        { M_VGA_M90x50, M_VGA_M80x25 },
        { M_VGA_C90x50, M_VGA_C80x25 },
        { M_VGA_M90x60, M_VGA_M80x25 },
        { M_VGA_C90x60, M_VGA_C80x25 },
#endif
        { M_VGA_MODEX,  M_VGA_CG320 },
    };
    int i;

    for (i = 0; i < NELEM(mode_map); ++i) {
        if (mode_map[i].from == mode)
            return mode_map[i].to;
    }
    return mode;
}
#endif /* !VGA_NO_BIOS && !VGA_NO_MODE_CHANGE */

/* turn the BIOS video number into our video mode number */
static int
map_bios_mode_num(int bios_mode)
{
    static int vga_modes[20] = {
	M_VGA_C40x25, M_VGA_C40x25,	/* 0, 1 */
	M_VGA_C80x25, M_VGA_C80x25,	/* 2, 3 */
	M_BG320, M_CG320,
	M_BG640,
	M_VGA_M80x25,			/* 7 */
	8, 9, 10, 11, 12,
	M_CG320_D,
	M_CG640_E,
	M_ENHMONOAPA2,
	M_ENH_CG640,
	M_BG640x480, M_CG640x480, 
	M_VGA_CG320,
    };

    if (bios_mode < NELEM(vga_modes))
	return vga_modes[bios_mode];

    return M_VGA_C80x25;
}

/* look up a parameter table entry */
static u_char *
get_mode_param(int mode)
{
#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
    if (mode >= V_MODE_MAP_SIZE)
	mode = map_mode_num(mode);
#endif
    if ((mode >= 0) && (mode < V_MODE_MAP_SIZE))
	return mode_map[mode];
    else
	return NULL;
}

static int
verify_adapter(video_adapter_t *adp)
{
    vm_offset_t buf;
    u_int16_t v;
#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
    u_int32_t p;
#endif

    buf = BIOS_PADDRTOVADDR(adp->va_window);
    v = readw(buf);
    writew(buf, 0xA55A);
    if (readw(buf) != 0xA55A)
	return ENXIO;
    writew(buf, v);

    outb(CRTC, 7);
    if (inb(CRTC) != 7)
	return ENXIO;

    adp->va_flags |= V_ADP_STATELOAD | V_ADP_STATESAVE | V_ADP_PALETTE |
	V_ADP_BORDER;

#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
    /* get the BIOS video mode pointer */
    p = *(u_int32_t *)BIOS_PADDRTOVADDR(0x4a8);
    p = BIOS_SADDRTOLADDR(p);
    if (ISMAPPED(p, sizeof(u_int32_t))) {
	p = *(u_int32_t *)BIOS_PADDRTOVADDR(p);
	p = BIOS_SADDRTOLADDR(p);
	if (ISMAPPED(p, V_MODE_PARAM_SIZE))
	    video_mode_ptr = (u_char *)BIOS_PADDRTOVADDR(p);
    }
#endif

    return 0;
}

static void
update_adapter_info(video_adapter_t *adp, video_info_t *info)
{
    adp->va_flags |= V_ADP_COLOR;
    adp->va_window = BIOS_PADDRTOVADDR(info->vi_window);
    adp->va_window_size = info->vi_window_size;
    adp->va_window_gran = info->vi_window_gran;
    adp->va_window_orig = 0;
    /* XXX */
    adp->va_buffer = info->vi_buffer;
    adp->va_buffer_size = info->vi_buffer_size;
    if (info->vi_mem_model == V_INFO_MM_VGAX) {
	adp->va_line_width = info->vi_width/2;
    } else if (info->vi_flags & V_INFO_GRAPHICS) {
	switch (info->vi_depth/info->vi_planes) {
	case 1:
	    adp->va_line_width = info->vi_width/8;
	    break;
	case 2:
	    adp->va_line_width = info->vi_width/4;
	    break;
	case 4:
	    adp->va_line_width = info->vi_width/2;
	    break;
	case 8:
	default: /* shouldn't happen */
	    adp->va_line_width = info->vi_width;
	    break;
	}
    } else {
	adp->va_line_width = info->vi_width;
    }
    adp->va_disp_start.x = 0;
    adp->va_disp_start.y = 0;
    bcopy(info, &adp->va_info, sizeof(adp->va_info));
}

/* probe video adapters and return the number of detected adapters */
static int
probe_adapters(void)
{
    video_adapter_t *adp;
    video_info_t info;
#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
    u_char *mp;
#endif
    int i;

    /* do this test only once */
    if (vga_init_done)
	return 1;
    vga_init_done = TRUE;

    /*
     * Check BIOS data area.
     * The VGA BIOS has more sophisticated mechanism and has this 
     * information in BIOSDATA_DCCINDEX (40:8a), but it also maintains 
     * compatibility with the EGA BIOS by updating BIOSDATA_VIDEOSWITCH.
     */

    /* 
     * XXX: we don't use BIOSDATA_EQUIPMENT, since it is not a dead
     * copy of RTC_EQUIPMENT.  Bits 4 and 5 of ETC_EQUIPMENT are
     * zeros for VGA.  However, VGA BIOS sets these bits in
     * BIOSDATA_EQUIPMENT according to the monitor type detected.
     */
#ifndef VGA_NO_BIOS
    if ((readb(BIOS_PADDRTOVADDR(0x488)) & 0x0f) != 0x09)
	    return 0;
#endif /* VGA_NO_BIOS */

    if (verify_adapter(&biosadapter) != 0)
	return 0;

    biosadapter.va_flags |= V_ADP_PROBED;
#ifndef VGA_NO_BIOS
    biosadapter.va_initial_bios_mode = readb(BIOS_PADDRTOVADDR(0x449));
    biosadapter.va_mode = biosadapter.va_initial_mode =
	map_bios_mode_num(biosadapter.va_initial_bios_mode);
#endif

    /*
     * Ensure a zero start address.  This is mainly to recover after
     * switching from pcvt using userconfig().  The registers are w/o
     * for old hardware so it's too hard to relocate the active screen
     * memory.
     * This must be done before vga_save_state() for VGA.
     */
    outb(CRTC, 12);
    outb(CRTC + 1, 0);
    outb(CRTC, 13);
    outb(CRTC + 1, 0);

    /* the video mode parameter table in VGA BIOS */
    /* NOTE: there can be only one VGA recognized by the video BIOS.
     */
    adp = &biosadapter;
    bzero(mode_map, sizeof(mode_map));
    vga_save_state(adp, &adpstate, sizeof(adpstate));
    for(i = 0; i < 16; i++)
	adp->va_palette_regs[i] = adpstate.regs[35 + i];
#if defined(VGA_NO_BIOS) || defined(VGA_NO_MODE_CHANGE)
    mode_map[adp->va_initial_mode] = adpstate.regs;
    rows_offset = 1;
#else /* VGA_NO_BIOS || VGA_NO_MODE_CHANGE */
    if (video_mode_ptr == NULL) {
	mode_map[adp->va_initial_mode] = adpstate.regs;
	rows_offset = 1;
    } else {
	/* discard modes that we are not familiar with */
	map_mode_table(mode_map, video_mode_ptr);
	mp = get_mode_param(adp->va_initial_mode);
#if !defined(VGA_KEEP_POWERON_MODE)
	if (mp != NULL) {
	    bcopy(mp, adpstate2.regs, sizeof(adpstate2.regs));
	    rows_offset = adpstate.regs[1] + 1 - mp[1];
	} else
#endif
	{
	    mode_map[adp->va_initial_mode] = adpstate.regs;
	    rows_offset = 1;
	}
    }
#endif /* VGA_NO_BIOS || VGA_NO_MODE_CHANGE */

#ifndef VGA_NO_MODE_CHANGE
    adp->va_flags |= V_ADP_MODECHANGE;
#endif
#ifndef VGA_NO_FONT_LOADING
    adp->va_flags |= V_ADP_FONT;
#endif

    /* XXX remove conflicting modes */
    for (i = 0; i < M_VGA_CG320; i++) {
	if (vga_get_info(&biosadapter, i, &info))
	    continue;
	if ((info.vi_flags & V_INFO_COLOR) != V_ADP_COLOR)
	    mode_map[i] = NULL;
    }

    /* buffer address */
    vga_get_info(&biosadapter, biosadapter.va_initial_mode, &info);
    info.vi_flags &= ~V_INFO_LINEAR; /* XXX */
    update_adapter_info(&biosadapter, &info);

    /*
     * XXX: we should verify the following values for the primary adapter...
     * crtc I/O port address: *(u_int16_t *)BIOS_PADDRTOVADDR(0x463);
     * color/mono display: (*(u_int8_t *)BIOS_PADDRTOVADDR(0x487) & 0x02) 
     *                     ? 0 : V_ADP_COLOR;
     * columns: *(u_int8_t *)BIOS_PADDRTOVADDR(0x44a);
     * rows: *(u_int8_t *)BIOS_PADDRTOVADDR(0x484);
     * font size: *(u_int8_t *)BIOS_PADDRTOVADDR(0x485);
     * buffer size: *(u_int16_t *)BIOS_PADDRTOVADDR(0x44c);
     */

    return 1;
}

/* set the scan line length in pixel */
static int
set_line_length(video_adapter_t *adp, int pixel)
{
    u_char *mp;
    int ppw;	/* pixels per word */
    int bpl;	/* bytes per line */
    int count;

    mp = get_mode_param(adp->va_mode);
    if (mp == NULL)
	return EINVAL;

    switch (adp->va_info.vi_mem_model) {
    case V_INFO_MM_PLANAR:
	ppw = 16/(adp->va_info.vi_depth/adp->va_info.vi_planes);
	count = (pixel + ppw - 1)/ppw/2;
	bpl = ((pixel + ppw - 1)/ppw/2)*4;
	break;
    case V_INFO_MM_PACKED:
	count = (pixel + 7)/8;
	bpl = ((pixel + 7)/8)*8;
	break;
    case V_INFO_MM_TEXT:
	count = (pixel + 7)/8;			/* columns */
	bpl = (pixel + 7)/8;			/* columns */
	break;
    default:
	return ENODEV;
    }

    if (mp[10 + 0x17] & 0x40)			/* CRTC mode control reg */
	count *= 2;				/* byte mode */
    outb(CRTC, 0x13);
    outb(CRTC, count);
    adp->va_line_width = bpl;

    return 0;
}

static int
set_display_start(video_adapter_t *adp, int x, int y)
{
    int off;	/* byte offset (graphics mode)/word offset (text mode) */
    int poff;	/* pixel offset */
    int roff;	/* row offset */
    int ppb;	/* pixels per byte */

    if (adp->va_info.vi_flags & V_INFO_GRAPHICS) {
	ppb = 8/(adp->va_info.vi_depth/adp->va_info.vi_planes);
	off = y*adp->va_line_width + x/ppb;
	roff = 0;
	poff = x%ppb;
    } else {
	outb(TSIDX, 1);
	if (inb(TSREG) & 1)
	    ppb = 9;
	else
	    ppb = 8;
	off = y/adp->va_info.vi_cheight*adp->va_line_width + x/ppb;
	roff = y%adp->va_info.vi_cheight;
	/* FIXME: is this correct? XXX */
	if (ppb == 8)
	    poff = x%ppb;
	else
	    poff = (x + 8)%ppb;
    }

    /* start address */
    outb(CRTC, 0xc);		/* high */
    outb(CRTC + 1, off >> 8);
    outb(CRTC, 0xd);		/* low */
    outb(CRTC + 1, off & 0xff);

    /* horizontal pel pan */
    inb(CRTC + 6);
    outb(ATC, 0x13 | 0x20);
    outb(ATC, poff);
    inb(CRTC + 6);
    outb(ATC, 0x20);

    /* preset raw scan */
    outb(CRTC, 8);
    outb(CRTC + 1, roff);

    adp->va_disp_start.x = x;
    adp->va_disp_start.y = y;
    return 0;
}

#ifndef VGA_NO_MODE_CHANGE
#if defined(__i386__) || defined(__x86_64__)	/* XXX */
static void
fill(int val, void *d, size_t size)
{
    u_char *p = d;

    while (size-- > 0)
	*p++ = val;
}
#endif /* __i386__ || __x86_64__ */

static void
filll_io(int val, vm_offset_t d, size_t size)
{
    while (size-- > 0) {
	writel(d, val);
	d += sizeof(u_int32_t);
    }
}
#endif /* !VGA_NO_MODE_CHANGE */

/* entry points */

#if 0
static int
vga_nop(void)
{
    return 0;
}
#endif

static int
vga_error(void)
{
    return ENODEV;
}

static int
vga_probe(int unit, video_adapter_t **adpp, void *arg, int flags)
{
    probe_adapters();
    if (unit != 0)
	return ENXIO;

    *adpp = &biosadapter;

    return 0;
}

static int
vga_init(int unit, video_adapter_t *adp, int flags)
{
    if ((unit != 0) || (adp == NULL) || !probe_done(adp))
	return ENXIO;

    if (!init_done(adp)) {
	/* nothing to do really... */
	adp->va_flags |= V_ADP_INITIALIZED;
    }

    if (!config_done(adp)) {
	if (vid_register(adp) < 0)
		return ENXIO;
	adp->va_flags |= V_ADP_REGISTERED;
    }
    if (vga_sub_configure != NULL)
	(*vga_sub_configure)(0);

    return 0;
}

/*
 * get_info():
 * Return the video_info structure of the requested video mode.
 */
static int
vga_get_info(video_adapter_t *adp, int mode, video_info_t *info)
{
    int i;

    if (!vga_init_done)
	return ENXIO;

#ifndef VGA_NO_MODE_CHANGE
    if (adp->va_flags & V_ADP_MODECHANGE) {
	/*
	 * If the parameter table entry for this mode is not found, 
	 * the mode is not supported...
	 */
	if (get_mode_param(mode) == NULL)
	    return EINVAL;
    } else
#endif /* VGA_NO_MODE_CHANGE */
    {
	/* 
	 * Even if we don't support video mode switching on this adapter,
	 * the information on the initial (thus current) video mode 
	 * should be made available.
	 */
	if (mode != adp->va_initial_mode)
	    return EINVAL;
    }

    for (i = 0; bios_vmode[i].vi_mode != EOT; ++i) {
	if (bios_vmode[i].vi_mode == NA)
	    continue;
	if (mode == bios_vmode[i].vi_mode) {
	    *info = bios_vmode[i];
	    /* XXX */
	    info->vi_buffer_size = info->vi_window_size*info->vi_planes;
	    return 0;
	}
    }
    return EINVAL;
}

/*
 * query_mode():
 * Find a video mode matching the requested parameters.
 * Fields filled with 0 are considered "don't care" fields and
 * match any modes.
 */
static int
vga_query_mode(video_adapter_t *adp, video_info_t *info)
{
    int i;

    if (!vga_init_done)
	return ENXIO;

    for (i = 0; bios_vmode[i].vi_mode != EOT; ++i) {
	if (bios_vmode[i].vi_mode == NA)
	    continue;

	if ((info->vi_width != 0)
	    && (info->vi_width != bios_vmode[i].vi_width))
		continue;
	if ((info->vi_height != 0)
	    && (info->vi_height != bios_vmode[i].vi_height))
		continue;
	if ((info->vi_cwidth != 0)
	    && (info->vi_cwidth != bios_vmode[i].vi_cwidth))
		continue;
	if ((info->vi_cheight != 0)
	    && (info->vi_cheight != bios_vmode[i].vi_cheight))
		continue;
	if ((info->vi_depth != 0)
	    && (info->vi_depth != bios_vmode[i].vi_depth))
		continue;
	if ((info->vi_planes != 0)
	    && (info->vi_planes != bios_vmode[i].vi_planes))
		continue;
	/* XXX: should check pixel format, memory model */
	if ((info->vi_flags != 0)
	    && (info->vi_flags != bios_vmode[i].vi_flags))
		continue;

	/* verify if this mode is supported on this adapter */
	if (vga_get_info(adp, bios_vmode[i].vi_mode, info))
		continue;
	return 0;
    }
    return ENODEV;
}

/*
 * set_mode():
 * Change the video mode.
 */

#ifndef VGA_NO_MODE_CHANGE
#ifdef VGA_WIDTH90
static void
set_width90(adp_state_t *params)
{
    /* 
     * Based on code submitted by Kelly Yancey (kbyanc@freedomnet.com)
     * and alexv@sui.gda.itesm.mx.
     */
    params->regs[5] |= 1;		/* toggle 8 pixel wide fonts */
    params->regs[10+0x0] = 0x6b;
    params->regs[10+0x1] = 0x59;
    params->regs[10+0x2] = 0x5a;
    params->regs[10+0x3] = 0x8e;
    params->regs[10+0x4] = 0x5e;
    params->regs[10+0x5] = 0x8a;
    params->regs[10+0x13] = 45;
    params->regs[35+0x13] = 0;
}
#endif /* VGA_WIDTH90 */
#endif /* !VGA_NO_MODE_CHANGE */

static int
vga_set_mode(video_adapter_t *adp, int mode)
{
#ifndef VGA_NO_MODE_CHANGE
    video_info_t info;
    adp_state_t params;

    prologue(adp, V_ADP_MODECHANGE, ENODEV);

    if (vga_get_info(adp, mode, &info))
	return EINVAL;

    lwkt_gettoken(&tty_token);

#if VGA_DEBUG > 1
    kprintf("vga_set_mode(): setting mode %d\n", mode);
#endif

    params.sig = V_STATE_SIG;
    bcopy(get_mode_param(mode), params.regs, sizeof(params.regs));

    switch (mode) {
#ifdef VGA_WIDTH90
    case M_VGA_C90x60: case M_VGA_M90x60:
	set_width90(&params);
	/* FALLTHROUGH */
#endif
    case M_VGA_C80x60: case M_VGA_M80x60:
	params.regs[2]  = 0x08;
	params.regs[19] = 0x47;
	goto special_480l;

#ifdef VGA_WIDTH90
    case M_VGA_C90x30: case M_VGA_M90x30:
	set_width90(&params);
	/* FALLTHROUGH */
#endif
    case M_VGA_C80x30: case M_VGA_M80x30:
	params.regs[19] = 0x4f;
special_480l:
	params.regs[9] |= 0xc0;
	params.regs[16] = 0x08;
	params.regs[17] = 0x3e;
	params.regs[26] = 0xea;
	params.regs[28] = 0xdf;
	params.regs[31] = 0xe7;
	params.regs[32] = 0x04;
	goto setup_mode;

#ifdef VGA_WIDTH90
    case M_VGA_C90x43: case M_VGA_M90x43:
	set_width90(&params);
	/* FALLTHROUGH */
#endif
    case M_ENH_C80x43: case M_ENH_B80x43:
	params.regs[28] = 87;
	goto special_80x50;

#ifdef VGA_WIDTH90
    case M_VGA_C90x50: case M_VGA_M90x50:
	set_width90(&params);
	/* FALLTHROUGH */
#endif
    case M_VGA_C80x50: case M_VGA_M80x50:
special_80x50:
	params.regs[2] = 8;
	params.regs[19] = 7;
	goto setup_mode;

#ifdef VGA_WIDTH90
    case M_VGA_C90x25: case M_VGA_M90x25:
	set_width90(&params);
	/* FALLTHROUGH */
#endif
    case M_VGA_C40x25: case M_VGA_C80x25:
    case M_VGA_M80x25:
    case M_B40x25:     case M_C40x25:
    case M_B80x25:     case M_C80x25:
    case M_ENH_B40x25: case M_ENH_C40x25:
    case M_ENH_B80x25: case M_ENH_C80x25:
    case M_EGAMONO80x25:

setup_mode:
	vga_load_state(adp, &params);
	break;

    case M_VGA_MODEX:
	/* "unchain" the VGA mode */
	params.regs[5-1+0x04] &= 0xf7;
	params.regs[5-1+0x04] |= 0x04;
	/* turn off doubleword mode */
	params.regs[10+0x14] &= 0xbf;
	/* turn off word addressing */
	params.regs[10+0x17] |= 0x40;
	/* set logical screen width */
	params.regs[10+0x13] = 80;
	/* set 240 lines */
	params.regs[10+0x11] = 0x2c;
	params.regs[10+0x06] = 0x0d;
	params.regs[10+0x07] = 0x3e;
	params.regs[10+0x10] = 0xea;
	params.regs[10+0x11] = 0xac;
	params.regs[10+0x12] = 0xdf;
	params.regs[10+0x15] = 0xe7;
	params.regs[10+0x16] = 0x06;
	/* set vertical sync polarity to reflect aspect ratio */
	params.regs[9] = 0xe3;
	goto setup_grmode;

    case M_BG320:     case M_CG320:     case M_BG640:
    case M_CG320_D:   case M_CG640_E:
    case M_CG640x350: case M_ENH_CG640:
    case M_BG640x480: case M_CG640x480: case M_VGA_CG320:

setup_grmode:
	vga_load_state(adp, &params);
	break;

    default:
        lwkt_reltoken(&tty_token);
	return EINVAL;
    }

    adp->va_mode = mode;
    info.vi_flags &= ~V_INFO_LINEAR; /* XXX */
    update_adapter_info(adp, &info);

    /* move hardware cursor out of the way */
    (*vidsw[adp->va_index]->set_hw_cursor)(adp, -1, -1);

    lwkt_reltoken(&tty_token);
    return 0;
#else /* VGA_NO_MODE_CHANGE */
    lwkt_reltoken(&tty_token);
    return ENODEV;
#endif /* VGA_NO_MODE_CHANGE */
}

#ifndef VGA_NO_FONT_LOADING

static void
set_font_mode(video_adapter_t *adp, u_char *buf)
{
    crit_enter();

    /* save register values */
    outb(TSIDX, 0x02); buf[0] = inb(TSREG);
    outb(TSIDX, 0x04); buf[1] = inb(TSREG);
    outb(GDCIDX, 0x04); buf[2] = inb(GDCREG);
    outb(GDCIDX, 0x05); buf[3] = inb(GDCREG);
    outb(GDCIDX, 0x06); buf[4] = inb(GDCREG);
    inb(CRTC + 6);
    outb(ATC, 0x10); buf[5] = inb(ATC + 1);

    /* setup vga for loading fonts */
    inb(CRTC + 6);				/* reset flip-flop */
    outb(ATC, 0x10); outb(ATC, buf[5] & ~0x01);
    inb(CRTC + 6);				/* reset flip-flop */
    outb(ATC, 0x20);				/* enable palette */

#ifdef VGA_ALT_SEQACCESS
    outw(TSIDX, 0x0100);
#endif
    outw(TSIDX, 0x0402);
    outw(TSIDX, 0x0704);
#ifdef VGA_ALT_SEQACCESS
    outw(TSIDX, 0x0300);
#endif
    outw(GDCIDX, 0x0204);
    outw(GDCIDX, 0x0005);
    outw(GDCIDX, 0x0406);               /* addr = a0000, 64kb */

    crit_exit();
}

static void
set_normal_mode(video_adapter_t *adp, u_char *buf)
{
    crit_enter();

    /* setup vga for normal operation mode again */
    inb(CRTC + 6);				/* reset flip-flop */
    outb(ATC, 0x10); outb(ATC, buf[5]);
    inb(CRTC + 6);				/* reset flip-flop */
    outb(ATC, 0x20);				/* enable palette */

#ifdef VGA_ALT_SEQACCESS
    outw(TSIDX, 0x0100);
#endif
    outw(TSIDX, 0x0002 | (buf[0] << 8));
    outw(TSIDX, 0x0004 | (buf[1] << 8));
#ifdef VGA_ALT_SEQACCESS
    outw(TSIDX, 0x0300);
#endif
    outw(GDCIDX, 0x0004 | (buf[2] << 8));
    outw(GDCIDX, 0x0005 | (buf[3] << 8));
    outw(GDCIDX, 0x0006 | (((buf[4] & 0x03) | 0x0c)<<8));

    crit_exit();
}

#endif /* VGA_NO_FONT_LOADING */

/*
 * save_font():
 * Read the font data in the requested font page from the video adapter.
 */
static int
vga_save_font(video_adapter_t *adp, int page, int fontsize, u_char *data,
	      int ch, int count)
{
#ifndef VGA_NO_FONT_LOADING
    u_char buf[PARAM_BUFSIZE];
    vm_offset_t segment;
    int c;
#ifdef VGA_ALT_SEQACCESS
    u_char val = 0;
#endif

    prologue(adp, V_ADP_FONT, ENODEV);

    if (fontsize < 14) {
	/* FONT_8 */
	fontsize = 8;
    } else if (fontsize >= 32) {
	fontsize = 32;
    } else if (fontsize >= 16) {
	/* FONT_16 */
	fontsize = 16;
    } else {
	/* FONT_14 */
	fontsize = 14;
    }

    if (page < 0 || page >= 8)
	return EINVAL;
    segment = FONT_BUF + 0x4000*page;
    if (page > 3)
	segment -= 0xe000;

#ifdef VGA_ALT_SEQACCESS
    crit_enter();
    outb(TSIDX, 0x00); outb(TSREG, 0x01);
    outb(TSIDX, 0x01); val = inb(TSREG);	/* disable screen */
    outb(TSIDX, 0x01); outb(TSREG, val | 0x20);
    outb(TSIDX, 0x00); outb(TSREG, 0x03);
    crit_exit();
#endif

    set_font_mode(adp, buf);
    if (fontsize == 32) {
	bcopy_fromio((uintptr_t)segment + ch*32, data, fontsize*count);
    } else {
	for (c = ch; count > 0; ++c, --count) {
	    bcopy_fromio((uintptr_t)segment + c*32, data, fontsize);
	    data += fontsize;
	}
    }
    set_normal_mode(adp, buf);

#ifdef VGA_ALT_SEQACCESS
    crit_enter();
    outb(TSIDX, 0x00); outb(TSREG, 0x01);
    outb(TSIDX, 0x01); outb(TSREG, val & 0xdf);	/* enable screen */
    outb(TSIDX, 0x00); outb(TSREG, 0x03);
    crit_exit();
#endif

    return 0;
#else /* VGA_NO_FONT_LOADING */
    return ENODEV;
#endif /* VGA_NO_FONT_LOADING */
}

/*
 * load_font():
 * Set the font data in the requested font page.
 * NOTE: it appears that some recent video adapters do not support
 * the font page other than 0... XXX
 */
static int
vga_load_font(video_adapter_t *adp, int page, int fontsize, u_char *data,
	      int ch, int count)
{
#ifndef VGA_NO_FONT_LOADING
    u_char buf[PARAM_BUFSIZE];
    vm_offset_t segment;
    int c;
#ifdef VGA_ALT_SEQACCESS
    u_char val = 0;
#endif

    prologue(adp, V_ADP_FONT, ENODEV);

    if (fontsize < 14) {
	/* FONT_8 */
	fontsize = 8;
    } else if (fontsize >= 32) {
	fontsize = 32;
    } else if (fontsize >= 16) {
	/* FONT_16 */
	fontsize = 16;
    } else {
	/* FONT_14 */
	fontsize = 14;
    }

    if (page < 0 || page >= 8)
	return EINVAL;
    segment = FONT_BUF + 0x4000*page;
    if (page > 3)
	segment -= 0xe000;

#ifdef VGA_ALT_SEQACCESS
    crit_enter();
    outb(TSIDX, 0x00); outb(TSREG, 0x01);
    outb(TSIDX, 0x01); val = inb(TSREG);	/* disable screen */
    outb(TSIDX, 0x01); outb(TSREG, val | 0x20);
    outb(TSIDX, 0x00); outb(TSREG, 0x03);
    crit_exit();
#endif

    set_font_mode(adp, buf);
    if (fontsize == 32) {
	bcopy_toio(data, (uintptr_t)segment + ch*32, fontsize*count);
    } else {
	for (c = ch; count > 0; ++c, --count) {
	    bcopy_toio(data, (uintptr_t)segment + c*32, fontsize);
	    data += fontsize;
	}
    }
    set_normal_mode(adp, buf);

#ifdef VGA_ALT_SEQACCESS
    crit_enter();
    outb(TSIDX, 0x00); outb(TSREG, 0x01);
    outb(TSIDX, 0x01); outb(TSREG, val & 0xdf);	/* enable screen */
    outb(TSIDX, 0x00); outb(TSREG, 0x03);
    crit_exit();
#endif

    return 0;
#else /* VGA_NO_FONT_LOADING */
    return ENODEV;
#endif /* VGA_NO_FONT_LOADING */
}

/*
 * show_font():
 * Activate the requested font page.
 * NOTE: it appears that some recent video adapters do not support
 * the font page other than 0... XXX
 */
static int
vga_show_font(video_adapter_t *adp, int page)
{
#ifndef VGA_NO_FONT_LOADING
    static u_char cg[] = { 0x00, 0x05, 0x0a, 0x0f, 0x30, 0x35, 0x3a, 0x3f };

    prologue(adp, V_ADP_FONT, ENODEV);
    if (page < 0 || page >= 8)
	return EINVAL;

    crit_enter();
    outb(TSIDX, 0x03); outb(TSREG, cg[page]);
    crit_exit();

    return 0;
#else /* VGA_NO_FONT_LOADING */
    return ENODEV;
#endif /* VGA_NO_FONT_LOADING */
}

/*
 * save_palette():
 * Read DAC values. The values have expressed in 8 bits.
 *
 * VGA
 */
static int
vga_save_palette(video_adapter_t *adp, u_char *palette)
{
    int i;

    prologue(adp, V_ADP_PALETTE, ENODEV);

    /* 
     * We store 8 bit values in the palette buffer, while the standard
     * VGA has 6 bit DAC .
     */
    outb(PALRADR, 0x00);
    for (i = 0; i < 256*3; ++i)
	palette[i] = inb(PALDATA) << 2; 
    inb(CRTC + 6);		/* reset flip/flop */
    return 0;
}

static int
vga_save_palette2(video_adapter_t *adp, int base, int count,
		  u_char *r, u_char *g, u_char *b)
{
    int i;

    prologue(adp, V_ADP_PALETTE, ENODEV);

    outb(PALRADR, base);
    for (i = 0; i < count; ++i) {
	r[i] = inb(PALDATA) << 2; 
	g[i] = inb(PALDATA) << 2; 
	b[i] = inb(PALDATA) << 2; 
    }
    inb(CRTC + 6);		/* reset flip/flop */
    return 0;
}

/*
 * load_palette():
 * Set DAC values.
 *
 * VGA
 */
static int
vga_load_palette(video_adapter_t *adp, const u_char *palette)
{
    int i;

    prologue(adp, V_ADP_PALETTE, ENODEV);

    outb(PIXMASK, 0xff);		/* no pixelmask */
    outb(PALWADR, 0x00);
    for (i = 0; i < 256*3; ++i)
	outb(PALDATA, palette[i] >> 2);
    inb(CRTC + 6);			/* reset flip/flop */
    outb(ATC, 0x20);			/* enable palette */
    return 0;
}

static int
vga_load_palette2(video_adapter_t *adp, int base, int count,
		  u_char *r, u_char *g, u_char *b)
{
    int i;

    prologue(adp, V_ADP_PALETTE, ENODEV);

    outb(PIXMASK, 0xff);		/* no pixelmask */
    outb(PALWADR, base);
    for (i = 0; i < count; ++i) {
	outb(PALDATA, r[i] >> 2);
	outb(PALDATA, g[i] >> 2);
	outb(PALDATA, b[i] >> 2);
    }
    inb(CRTC + 6);			/* reset flip/flop */
    outb(ATC, 0x20);			/* enable palette */
    return 0;
}

/*
 * set_border():
 * Change the border color.
 */
static int
vga_set_border(video_adapter_t *adp, int color)
{
    prologue(adp, V_ADP_BORDER, ENODEV);

    inb(CRTC + 6);	/* reset flip-flop */
    outb(ATC, 0x31); outb(ATC, color & 0xff); 

    return 0;
}

/*
 * save_state():
 * Read video register values.
 * NOTE: this function only reads the standard VGA registers.
 * any extra/extended registers of SVGA adapters are not saved.
 */
static int
vga_save_state(video_adapter_t *adp, void *p, size_t size)
{
    video_info_t info;
    u_char *buf;
    int crtc_addr;
    int i, j;

    if (size == 0) {
	/* return the required buffer size */
	prologue(adp, V_ADP_STATESAVE, 0);
	return sizeof(adp_state_t);
    } else {
	prologue(adp, V_ADP_STATESAVE, ENODEV);
	if (size < sizeof(adp_state_t))
	    return EINVAL;
    }
    ((adp_state_t *)p)->sig = V_STATE_SIG;
    buf = ((adp_state_t *)p)->regs;
    bzero(buf, V_MODE_PARAM_SIZE);
    crtc_addr = CRTC;

    crit_enter();

    outb(TSIDX, 0x00); outb(TSREG, 0x01);	/* stop sequencer */
    for (i = 0, j = 5; i < 4; i++) {           
	outb(TSIDX, i + 1);
	buf[j++]  =  inb(TSREG);
    }
    buf[9]  =  inb(MISC + 10);			/* dot-clock */
    outb(TSIDX, 0x00); outb(TSREG, 0x03);	/* start sequencer */

    for (i = 0, j = 10; i < 25; i++) {		/* crtc */
	outb(crtc_addr, i);
	buf[j++]  =  inb(crtc_addr + 1);
    }
    for (i = 0, j = 35; i < 20; i++) {		/* attribute ctrl */
        inb(crtc_addr + 6);			/* reset flip-flop */
	outb(ATC, i);
	buf[j++]  =  inb(ATC + 1);
    }
    for (i = 0, j = 55; i < 9; i++) {		/* graph data ctrl */
	outb(GDCIDX, i);
	buf[j++]  =  inb(GDCREG);
    }
    inb(crtc_addr + 6);				/* reset flip-flop */
    outb(ATC, 0x20);				/* enable palette */

    crit_exit();

#if 1
    if (vga_get_info(adp, adp->va_mode, &info) == 0) {
	if (info.vi_flags & V_INFO_GRAPHICS) {
	    buf[0] = info.vi_width/info.vi_cwidth; /* COLS */
	    buf[1] = info.vi_height/info.vi_cheight - 1; /* ROWS */
	} else {
	    buf[0] = info.vi_width;		/* COLS */
	    buf[1] = info.vi_height - 1;	/* ROWS */
	}
	buf[2] = info.vi_cheight;		/* POINTS */
    } else {
	/* XXX: shouldn't be happening... */
	kprintf("vga%d: %s: failed to obtain mode info. (vga_save_state())\n",
	       adp->va_unit, adp->va_name);
    }
#else
    buf[0] = readb(BIOS_PADDRTOVADDR(0x44a));	/* COLS */
    buf[1] = readb(BIOS_PADDRTOVADDR(0x484));	/* ROWS */
    buf[2] = readb(BIOS_PADDRTOVADDR(0x485));	/* POINTS */
    buf[3] = readb(BIOS_PADDRTOVADDR(0x44c));
    buf[4] = readb(BIOS_PADDRTOVADDR(0x44d));
#endif

    return 0;
}

/*
 * load_state():
 * Set video registers at once.
 * NOTE: this function only updates the standard VGA registers.
 * any extra/extended registers of SVGA adapters are not changed.
 */
static int
vga_load_state(video_adapter_t *adp, void *p)
{
    u_char *buf;
    int crtc_addr;
    int i;

    prologue(adp, V_ADP_STATELOAD, ENODEV);
    if (((adp_state_t *)p)->sig != V_STATE_SIG)
	return EINVAL;

    buf = ((adp_state_t *)p)->regs;
    crtc_addr = CRTC;

#if VGA_DEBUG > 1
    hexdump(buf, V_MODE_PARAM_SIZE, NULL, HD_OMIT_CHARS | HD_OMIT_COUNT);
#endif

    crit_enter();

    outb(TSIDX, 0x00); outb(TSREG, 0x01);	/* stop sequencer */
    for (i = 0; i < 4; ++i) {			/* program sequencer */
	outb(TSIDX, i + 1);
	outb(TSREG, buf[i + 5]);
    }
    outb(MISC, buf[9]);				/* set dot-clock */
    outb(TSIDX, 0x00); outb(TSREG, 0x03);	/* start sequencer */
    outb(crtc_addr, 0x11);
    outb(crtc_addr + 1, inb(crtc_addr + 1) & 0x7F);
    for (i = 0; i < 25; ++i) {			/* program crtc */
	outb(crtc_addr, i);
	outb(crtc_addr + 1, buf[i + 10]);
    }
    inb(crtc_addr+6);				/* reset flip-flop */
    for (i = 0; i < 20; ++i) {			/* program attribute ctrl */
	outb(ATC, i);
	outb(ATC, buf[i + 35]);
    }
    for (i = 0; i < 9; ++i) {			/* program graph data ctrl */
	outb(GDCIDX, i);
	outb(GDCREG, buf[i + 55]);
    }
    inb(crtc_addr + 6);				/* reset flip-flop */
    outb(ATC, 0x20);				/* enable palette */

#if 0 /* XXX a temporary workaround for kernel panic */
#ifndef VGA_NO_BIOS
    if (adp->va_unit == V_ADP_PRIMARY) {
	writeb(BIOS_PADDRTOVADDR(0x44a), buf[0]);	/* COLS */
	writeb(BIOS_PADDRTOVADDR(0x484), buf[1] + rows_offset - 1); /* ROWS */
	writeb(BIOS_PADDRTOVADDR(0x485), buf[2]);	/* POINTS */
#if 0
	writeb(BIOS_PADDRTOVADDR(0x44c), buf[3]);
	writeb(BIOS_PADDRTOVADDR(0x44d), buf[4]);
#endif
    }
#endif /* VGA_NO_BIOS */
#endif /* XXX */

    crit_exit();
    return 0;
}

/*
 * set_origin():
 * Change the origin (window mapping) of the banked frame buffer.
 */
static int
vga_set_origin(video_adapter_t *adp, off_t offset)
{
    /* 
     * The standard video modes do not require window mapping; 
     * always return error.
     */
    return ENODEV;
}

/*
 * read_hw_cursor():
 * Read the position of the hardware text cursor.
 */
static int
vga_read_hw_cursor(video_adapter_t *adp, int *col, int *row)
{
    u_int16_t off;

    if (!vga_init_done)
	return ENXIO;

    if (adp->va_info.vi_flags & V_INFO_GRAPHICS)
	return ENODEV;

    crit_enter();
    outb(CRTC, 14);
    off = inb(CRTC + 1);
    outb(CRTC, 15);
    off = (off << 8) | inb(CRTC + 1);
    crit_exit();

    *row = off / adp->va_info.vi_width;
    *col = off % adp->va_info.vi_width;

    return 0;
}

/*
 * set_hw_cursor():
 * Move the hardware text cursor.  If col and row are both -1, 
 * the cursor won't be shown.
 */
static int
vga_set_hw_cursor(video_adapter_t *adp, int col, int row)
{
    u_int16_t off;

    if (!vga_init_done)
	return ENXIO;

    if ((col == -1) && (row == -1)) {
	off = -1;
    } else {
	if (adp->va_info.vi_flags & V_INFO_GRAPHICS)
	    return ENODEV;
	off = row*adp->va_info.vi_width + col;
    }

    crit_enter();
    outb(CRTC, 14);
    outb(CRTC + 1, off >> 8);
    outb(CRTC, 15);
    outb(CRTC + 1, off & 0x00ff);
    crit_exit();

    return 0;
}

/*
 * set_hw_cursor_shape():
 * Change the shape of the hardware text cursor. If the height is
 * zero or negative, the cursor won't be shown.
 */
static int
vga_set_hw_cursor_shape(video_adapter_t *adp, int base, int height,
			int celsize, int blink)
{
    if (!vga_init_done)
	return ENXIO;

    crit_enter();
    if (height <= 0) {
	/* make the cursor invisible */
	outb(CRTC, 10);
	outb(CRTC + 1, 32);
	outb(CRTC, 11);
	outb(CRTC + 1, 0);
    } else {
	outb(CRTC, 10);
	outb(CRTC + 1, celsize - base - height);
	outb(CRTC, 11);
	outb(CRTC + 1, celsize - base - 1);
    }
    crit_exit();

    return 0;
}

/*
 * blank_display()
 * Put the display in power save/power off mode.
 */
static int
vga_blank_display(video_adapter_t *adp, int mode)
{
    u_char val;

    crit_enter();
    switch (mode) {
    case V_DISPLAY_SUSPEND:
    case V_DISPLAY_STAND_BY:
	outb(TSIDX, 0x01);
	val = inb(TSREG);
	outb(TSIDX, 0x01);
	outb(TSREG, val | 0x20);
	outb(CRTC, 0x17);
	val = inb(CRTC + 1);
	outb(CRTC + 1, val & ~0x80);
	break;
    case V_DISPLAY_OFF:
	outb(TSIDX, 0x01);
	val = inb(TSREG);
	outb(TSIDX, 0x01);
	outb(TSREG, val | 0x20);
	break;
    case V_DISPLAY_ON:
	outb(TSIDX, 0x01);
	val = inb(TSREG);
	outb(TSIDX, 0x01);
	outb(TSREG, val & 0xDF);
	outb(CRTC, 0x17);
	val = inb(CRTC + 1);
	outb(CRTC + 1, val | 0x80);
	break;
    }
    crit_exit();

    return 0;
}

/*
 * mmap():
 * Mmap frame buffer.
 */
static int
vga_mmap_buf(video_adapter_t *adp, vm_offset_t offset, int prot)
{
    if (adp->va_info.vi_flags & V_INFO_LINEAR)
	return -1;

#if VGA_DEBUG > 0
    kprintf("vga_mmap_buf(): window:0x%x, offset:%p\n",
	   adp->va_info.vi_window, (void *)offset);
#endif

    /* XXX: is this correct? */
    if (offset > adp->va_window_size - PAGE_SIZE)
	return -1;

#if defined(__i386__)
    return i386_btop(adp->va_info.vi_window + offset);
#elif defined(__x86_64__)
    return x86_64_btop(adp->va_info.vi_window + offset);
#else
#error "vga_mmap_buf needs to return something"
#endif
}

#ifndef VGA_NO_MODE_CHANGE

static void
planar_fill(video_adapter_t *adp, int val)
{
    int length;
    int at;			/* position in the frame buffer */
    int l;

    lwkt_gettoken(&tty_token);
    outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
    outw(GDCIDX, 0x0003);		/* data rotate/function select */
    outw(GDCIDX, 0x0f01);		/* set/reset enable */
    outw(GDCIDX, 0xff08);		/* bit mask */
    outw(GDCIDX, (val << 8) | 0x00);	/* set/reset */
    at = 0;
    length = adp->va_line_width*adp->va_info.vi_height;
    while (length > 0) {
	l = imin(length, adp->va_window_size);
	(*vidsw[adp->va_index]->set_win_org)(adp, at);
	bzero_io(adp->va_window, l);
	length -= l;
	at += l;
    }
    outw(GDCIDX, 0x0000);		/* set/reset */
    outw(GDCIDX, 0x0001);		/* set/reset enable */
    lwkt_reltoken(&tty_token);
}

static void
packed_fill(video_adapter_t *adp, int val)
{
    int length;
    int at;			/* position in the frame buffer */
    int l;

    lwkt_gettoken(&tty_token);
    at = 0;
    length = adp->va_line_width*adp->va_info.vi_height;
    while (length > 0) {
	l = imin(length, adp->va_window_size);
	(*vidsw[adp->va_index]->set_win_org)(adp, at);
	fill_io(val, adp->va_window, l);
	length -= l;
	at += l;
    }
    lwkt_reltoken(&tty_token);
}

static void
direct_fill(video_adapter_t *adp, int val)
{
    int length;
    int at;			/* position in the frame buffer */
    int l;

    lwkt_gettoken(&tty_token);
    at = 0;
    length = adp->va_line_width*adp->va_info.vi_height;
    while (length > 0) {
	l = imin(length, adp->va_window_size);
	(*vidsw[adp->va_index]->set_win_org)(adp, at);
	switch (adp->va_info.vi_pixel_size) {
	case sizeof(u_int16_t):
	    fillw_io(val, adp->va_window, l/sizeof(u_int16_t));
	    break;
	case 3:
	    /* FIXME */
	    break;
	case sizeof(u_int32_t):
	    filll_io(val, adp->va_window, l/sizeof(u_int32_t));
	    break;
	}
	length -= l;
	at += l;
    }
    lwkt_reltoken(&tty_token);
}

static int
vga_clear(video_adapter_t *adp)
{
    switch (adp->va_info.vi_mem_model) {
    case V_INFO_MM_TEXT:
	/* do nothing? XXX */
	break;
    case V_INFO_MM_PLANAR:
	planar_fill(adp, 0);
	break;
    case V_INFO_MM_PACKED:
	packed_fill(adp, 0);
	break;
    case V_INFO_MM_DIRECT:
	direct_fill(adp, 0);
	break;
    }
    return 0;
}

#ifdef notyet
static void
planar_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
    int banksize;
    int bank;
    int pos;
    int offset;			/* offset within window */
    int bx;
    int l;

    lwkt_gettoken(&tty_token);
    outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
    outw(GDCIDX, 0x0003);		/* data rotate/function select */
    outw(GDCIDX, 0x0f01);		/* set/reset enable */
    outw(GDCIDX, 0xff08);		/* bit mask */
    outw(GDCIDX, (val << 8) | 0x00); /* set/reset */

    banksize = adp->va_window_size;
    bank = -1;
    while (cy > 0) {
	pos = adp->va_line_width*y + x/8;
	if (bank != pos/banksize) {
	    (*vidsw[adp->va_index]->set_win_org)(adp, pos);
	    bank = pos/banksize;
	}
	offset = pos%banksize;
	bx = (x + cx)/8 - x/8;
	if (x % 8) {
	    outw(GDCIDX, ((0xff00 >> (x % 8)) & 0xff00) | 0x08);
	    writeb(adp->va_window + offset, 0);
	    ++offset;
	    --bx;
	    if (offset >= banksize) {
		offset = 0;
		++bank;		/* next bank */
		(*vidsw[adp->va_index]->set_win_org)(adp, bank*banksize);
	    }
	    outw(GDCIDX, 0xff08);	/* bit mask */
	}
	while (bx > 0) {
	    l = imin(bx, banksize);
	    bzero_io(adp->va_window + offset, l);
	    offset += l;
	    bx -= l;
	    if (offset >= banksize) {
		offset = 0;
		++bank;		/* next bank */
		(*vidsw[adp->va_index]->set_win_org)(adp, bank*banksize);
	    }
	}
	if ((x + cx) % 8) {
	    outw(GDCIDX, (~(0xff00 >> ((x + cx) % 8)) & 0xff00) | 0x08);
	    writeb(adp->va_window + offset, 0);
	    ++offset;
	    if (offset >= banksize) {
		offset = 0;
		++bank;		/* next bank */
		(*vidsw[adp->va_index]->set_win_org)(adp, bank*banksize);
	    }
	    outw(GDCIDX, 0xff08);	/* bit mask */
	}
	++y;
	--cy;
    }

    outw(GDCIDX, 0xff08);		/* bit mask */
    outw(GDCIDX, 0x0000);		/* set/reset */
    outw(GDCIDX, 0x0001);		/* set/reset enable */
    lwkt_reltoken(&tty_token);
}

static void
packed_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
    int banksize;
    int bank;
    int pos;
    int offset;			/* offset within window */
    int end;

    lwkt_gettoken(&tty_token);
    banksize = adp->va_window_size;
    bank = -1;
    cx *= adp->va_info.vi_pixel_size;
    while (cy > 0) {
	pos = adp->va_line_width*y + x*adp->va_info.vi_pixel_size;
	if (bank != pos/banksize) {
	    (*vidsw[adp->va_index]->set_win_org)(adp, pos);
	    bank = pos/banksize;
	}
	offset = pos%banksize;
	end = imin(offset + cx, banksize);
	fill_io(val, adp->va_window + offset,
		(end - offset)/adp->va_info.vi_pixel_size);
	/* the line may cross the window boundary */
	if (offset + cx > banksize) {
	    ++bank;		/* next bank */
	    (*vidsw[adp->va_index]->set_win_org)(adp, bank*banksize);
	    end = offset + cx - banksize;
	    fill_io(val, adp->va_window, end/adp->va_info.vi_pixel_size);
	}
	++y;
	--cy;
    }
    lwkt_reltoken(&tty_token);
}

static void
direct_fill_rect16(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
    int banksize;
    int bank;
    int pos;
    int offset;			/* offset within window */
    int end;

    lwkt_gettoken(&tty_token);
    /*
     * XXX: the function assumes that banksize is a muliple of
     * sizeof(u_int16_t).
     */
    banksize = adp->va_window_size;
    bank = -1;
    cx *= sizeof(u_int16_t);
    while (cy > 0) {
	pos = adp->va_line_width*y + x*sizeof(u_int16_t);
	if (bank != pos/banksize) {
	    (*vidsw[adp->va_index]->set_win_org)(adp, pos);
	    bank = pos/banksize;
	}
	offset = pos%banksize;
	end = imin(offset + cx, banksize);
	fillw_io(val, adp->va_window + offset,
		 (end - offset)/sizeof(u_int16_t));
	/* the line may cross the window boundary */
	if (offset + cx > banksize) {
	    ++bank;		/* next bank */
	    (*vidsw[adp->va_index]->set_win_org)(adp, bank*banksize);
	    end = offset + cx - banksize;
	    fillw_io(val, adp->va_window, end/sizeof(u_int16_t));
	}
	++y;
	--cy;
    }
    lwkt_reltoken(&tty_token);
}

static void
direct_fill_rect24(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
    int banksize;
    int bank;
    int pos;
    int offset;			/* offset within window */
    int end;
    int i;
    int j;
    u_int8_t b[3];

    lwkt_gettoken(&tty_token);
    b[0] = val & 0x0000ff;
    b[1] = (val >> 8) & 0x0000ff;
    b[2] = (val >> 16) & 0x0000ff;
    banksize = adp->va_window_size;
    bank = -1;
    cx *= 3;
    while (cy > 0) {
	pos = adp->va_line_width*y + x*3;
	if (bank != pos/banksize) {
	    (*vidsw[adp->va_index]->set_win_org)(adp, pos);
	    bank = pos/banksize;
	}
	offset = pos%banksize;
	end = imin(offset + cx, banksize);
	for (i = 0, j = offset; j < end; i = (++i)%3, ++j) {
	    writeb(adp->va_window + j, b[i]);
	}
	/* the line may cross the window boundary */
	if (offset + cx >= banksize) {
	    ++bank;		/* next bank */
	    (*vidsw[adp->va_index]->set_win_org)(adp, bank*banksize);
	    j = 0;
	    end = offset + cx - banksize;
	    for (; j < end; i = (++i)%3, ++j) {
		writeb(adp->va_window + j, b[i]);
	    }
	}
	++y;
	--cy;
    }
    lwkt_reltoken(&tty_token);
}

static void
direct_fill_rect32(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
    int banksize;
    int bank;
    int pos;
    int offset;			/* offset within window */
    int end;

    lwkt_gettoken(&tty_token);
    /*
     * XXX: the function assumes that banksize is a muliple of
     * sizeof(u_int32_t).
     */
    banksize = adp->va_window_size;
    bank = -1;
    cx *= sizeof(u_int32_t);
    while (cy > 0) {
	pos = adp->va_line_width*y + x*sizeof(u_int32_t);
	if (bank != pos/banksize) {
	    (*vidsw[adp->va_index]->set_win_org)(adp, pos);
	    bank = pos/banksize;
	}
	offset = pos%banksize;
	end = imin(offset + cx, banksize);
	filll_io(val, adp->va_window + offset,
		 (end - offset)/sizeof(u_int32_t));
	/* the line may cross the window boundary */
	if (offset + cx > banksize) {
	    ++bank;		/* next bank */
	    (*vidsw[adp->va_index]->set_win_org)(adp, bank*banksize);
	    end = offset + cx - banksize;
	    filll_io(val, adp->va_window, end/sizeof(u_int32_t));
	}
	++y;
	--cy;
    }
    lwkt_reltoken(&tty_token);
}

static int
vga_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
    switch (adp->va_info.vi_mem_model) {
    case V_INFO_MM_TEXT:
	/* do nothing? XXX */
	break;
    case V_INFO_MM_PLANAR:
	planar_fill_rect(adp, val, x, y, cx, cy);
	break;
    case V_INFO_MM_PACKED:
	packed_fill_rect(adp, val, x, y, cx, cy);
	break;
    case V_INFO_MM_DIRECT:
	switch (adp->va_info.vi_pixel_size) {
	case sizeof(u_int16_t):
	    direct_fill_rect16(adp, val, x, y, cx, cy);
	    break;
	case 3:
	    direct_fill_rect24(adp, val, x, y, cx, cy);
	    break;
	case sizeof(u_int32_t):
	    direct_fill_rect32(adp, val, x, y, cx, cy);
	    break;
	}
	break;
    }
    return 0;
}
#else /* !notyet */
static int
vga_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
    return ENODEV;
}
#endif /* notyet */

static int
vga_bitblt(video_adapter_t *adp,...)
{
    /* FIXME */
    return ENODEV;
}

#endif /* !VGA_NO_MODE_CHANGE */

static int
get_palette(video_adapter_t *adp, int base, int count,
	    u_char *red, u_char *green, u_char *blue, u_char *trans)
{
    u_char *r;
    u_char *g;
    u_char *b;

    if (count < 0 || base < 0 || count > 256 || base > 256 ||
	base + count > 256)
	return EINVAL;

    r = kmalloc(count*3, M_DEVBUF, M_WAITOK);
    g = r + count;
    b = g + count;
    if (vga_save_palette2(adp, base, count, r, g, b)) {
	kfree(r, M_DEVBUF);
	return ENODEV;
    }
    copyout(r, red, count);
    copyout(g, green, count);
    copyout(b, blue, count);
    if (trans != NULL) {
	bzero(r, count);
	copyout(r, trans, count);
    }
    kfree(r, M_DEVBUF);

    return 0;
}

static int
set_palette(video_adapter_t *adp, int base, int count,
	    u_char *red, u_char *green, u_char *blue, u_char *trans)
{
    u_char *r;
    u_char *g;
    u_char *b;
    int err;

    if (count < 0 || base < 0 || count > 256 || base > 256 ||
	base + count > 256)
	return EINVAL;

    r = kmalloc(count*3, M_DEVBUF, M_WAITOK);
    g = r + count;
    b = g + count;
    err = copyin(red, r, count);
    if (!err)
	err = copyin(green, g, count);
    if (!err)
	err = copyin(blue, b, count);
    if (!err)
	err = vga_load_palette2(adp, base, count, r, g, b);
    kfree(r, M_DEVBUF);

    return (err ? ENODEV : 0);
}

static int
vga_dev_ioctl(video_adapter_t *adp, u_long cmd, caddr_t arg)
{
    switch (cmd) {
    case FBIO_GETWINORG:	/* get frame buffer window origin */
	*(u_int *)arg = 0;
	return 0;

    case FBIO_SETWINORG:	/* set frame buffer window origin */
	return ENODEV;

    case FBIO_SETDISPSTART:	/* set display start address */
	return (set_display_start(adp, 
				  ((video_display_start_t *)arg)->x,
			  	  ((video_display_start_t *)arg)->y)
		? ENODEV : 0);

    case FBIO_SETLINEWIDTH:	/* set scan line length in pixel */
	return (set_line_length(adp, *(u_int *)arg) ? ENODEV : 0);

    case FBIO_GETPALETTE:	/* get color palette */
	return get_palette(adp, ((video_color_palette_t *)arg)->index,
			   ((video_color_palette_t *)arg)->count,
			   ((video_color_palette_t *)arg)->red,
			   ((video_color_palette_t *)arg)->green,
			   ((video_color_palette_t *)arg)->blue,
			   ((video_color_palette_t *)arg)->transparent);

    case FBIO_SETPALETTE:	/* set color palette */
	return set_palette(adp, ((video_color_palette_t *)arg)->index,
			   ((video_color_palette_t *)arg)->count,
			   ((video_color_palette_t *)arg)->red,
			   ((video_color_palette_t *)arg)->green,
			   ((video_color_palette_t *)arg)->blue,
			   ((video_color_palette_t *)arg)->transparent);

    case FBIOGTYPE:		/* get frame buffer type info. */
	((struct fbtype *)arg)->fb_type = fb_type(adp->va_type);
	((struct fbtype *)arg)->fb_height = adp->va_info.vi_height;
	((struct fbtype *)arg)->fb_width = adp->va_info.vi_width;
	((struct fbtype *)arg)->fb_depth = adp->va_info.vi_depth;
	if ((adp->va_info.vi_depth <= 1) || (adp->va_info.vi_depth > 8))
	    ((struct fbtype *)arg)->fb_cmsize = 0;
	else
	    ((struct fbtype *)arg)->fb_cmsize = 1 << adp->va_info.vi_depth;
	((struct fbtype *)arg)->fb_size = adp->va_buffer_size;
	return 0;

    case FBIOGETCMAP:		/* get color palette */
	return get_palette(adp, ((struct fbcmap *)arg)->index,
			   ((struct fbcmap *)arg)->count,
			   ((struct fbcmap *)arg)->red,
			   ((struct fbcmap *)arg)->green,
			   ((struct fbcmap *)arg)->blue, NULL);

    case FBIOPUTCMAP:		/* set color palette */
	return set_palette(adp, ((struct fbcmap *)arg)->index,
			   ((struct fbcmap *)arg)->count,
			   ((struct fbcmap *)arg)->red,
			   ((struct fbcmap *)arg)->green,
			   ((struct fbcmap *)arg)->blue, NULL);

    default:
	return fb_commonioctl(adp, cmd, arg);
    }
}

/*
 * diag():
 * Print some information about the video adapter and video modes,
 * with requested level of details.
 */
static int
vga_diag(video_adapter_t *adp, int level)
{
    u_char *mp;
#if FB_DEBUG > 1
    video_info_t info;
    int i;
#endif

    if (!vga_init_done)
	return ENXIO;

#if FB_DEBUG > 1
#ifndef VGA_NO_BIOS
    kprintf("vga: DCC code:0x%02x\n",
	readb(BIOS_PADDRTOVADDR(0x488)));
    kprintf("vga: CRTC:0x%x, video option:0x%02x, ",
	readw(BIOS_PADDRTOVADDR(0x463)),
	readb(BIOS_PADDRTOVADDR(0x487)));
    kprintf("rows:%d, cols:%d, font height:%d\n",
	readb(BIOS_PADDRTOVADDR(0x44a)),
	readb(BIOS_PADDRTOVADDR(0x484)) + 1,
	readb(BIOS_PADDRTOVADDR(0x485)));
#endif /* VGA_NO_BIOS */
#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
    kprintf("vga: param table:%p\n", video_mode_ptr);
    kprintf("vga: rows_offset:%d\n", rows_offset);
#endif
#endif /* FB_DEBUG > 1 */

    fb_dump_adp_info(VGA_DRIVER_NAME, adp, level);

#if FB_DEBUG > 1
    if (adp->va_flags & V_ADP_MODECHANGE) {
	for (i = 0; bios_vmode[i].vi_mode != EOT; ++i) {
	    if (bios_vmode[i].vi_mode == NA)
		continue;
	    if (get_mode_param(bios_vmode[i].vi_mode) == NULL)
		continue;
	    fb_dump_mode_info(VGA_DRIVER_NAME, adp, &bios_vmode[i], level);
	}
    } else {
	vga_get_info(adp, adp->va_initial_mode, &info);	/* shouldn't fail */
	fb_dump_mode_info(VGA_DRIVER_NAME, adp, &info, level);
    }
#endif /* FB_DEBUG > 1 */

#if !defined(VGA_NO_BIOS) && !defined(VGA_NO_MODE_CHANGE)
    if (video_mode_ptr == NULL)
	kprintf("vga%d: %s: WARNING: video mode switching is not "
	       "fully supported on this adapter\n",
	       adp->va_unit, adp->va_name);
#endif
    if (level <= 0)
	return 0;

    kprintf("VGA parameters upon power-up\n");
    hexdump(adpstate.regs, sizeof(adpstate.regs), NULL,
	HD_OMIT_CHARS | HD_OMIT_COUNT);

    mp = get_mode_param(adp->va_initial_mode);
    if (mp == NULL)	/* this shouldn't be happening */
	return 0;
    kprintf("VGA parameters in BIOS for mode %d\n", adp->va_initial_mode);
    hexdump(adpstate2.regs, sizeof(adpstate2.regs), NULL,
	HD_OMIT_CHARS | HD_OMIT_COUNT);
    kprintf("VGA parameters to be used for mode %d\n", adp->va_initial_mode);
    hexdump(mp, V_MODE_PARAM_SIZE, NULL, HD_OMIT_CHARS | HD_OMIT_COUNT);

    return 0;
}
