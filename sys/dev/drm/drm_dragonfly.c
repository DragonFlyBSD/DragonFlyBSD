/*
 * Copyright (c) 2015 Imre Vadász <imre@vdsz.com>
 *
 * DRM Dragonfly-specific helper functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <sys/libkern.h>
#include <sys/ctype.h>
#include <drm/drmP.h>

#if 0
commit 26a028bf8c7694e64d44f9e2bb8bd0fba47d7519
Author: Imre Vadász <imre@vdsz.com>
Date:   Tue Jun 2 23:14:52 2015 +0200

    drm: hack together an implementation of fb_get_options
    
    This can be used to set the video mode used for the syscons fb console,
    a la "video=..." in linux.
#endif

/*
 * An implementation of fb_get_options()
 * This can be used to set the video mode used for the syscons fb console,
 * a la "video=..." in linux.
 */
int
fb_get_options(const char *connector_name, char **option)
{
	char buf[128], str[1024];
	int i;

	/*
	 * This hack allows us to use drm.video.lvds1="<video-mode>"
	 * in loader.conf, where linux would use video=LVDS-1:<video-mode>.
	 * e.g. drm.video.lvds1=1024x768 sets the LVDS-1 connector to
	 * a 1024x768 video mode in the syscons framebuffer console.
	 * See https://wiki.archlinux.org/index.php/Kernel_mode_setting
	 * for an explanation of the video mode command line option.
	 * (This corresponds to the video= Linux kernel command-line
	 * option)
	 */
	memset(str, 0, sizeof(str));
	ksnprintf(buf, sizeof(buf), "drm.video.%s", connector_name);
	i = 0;
	while (i < strlen(buf)) {
		buf[i] = tolower(buf[i]);
		if (buf[i] == '-') {
			memmove(&buf[i], &buf[i+1], strlen(buf)-i);
		} else {
			i++;
		}
	}
	kprintf("looking up kenv for \"%s\"\n", buf);
	if (kgetenv_string(buf, str, sizeof(str)-1)) {
		kprintf("found kenv %s=%s\n", buf, str);
		*option = kstrdup(str, M_DRM);
		return (0);
	} else {
		kprintf("didn't find value for kenv %s\n", buf);
		return (1);
	}
}

