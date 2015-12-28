
/*
 * cc testvblank.c -o ~/bin/testvblank -I/usr/src/sys/dev/drm/include
 *
 * Should print one 'x' every 10 vblanks (6/sec @ 60Hz, 3/sec @ 30Hz).
 * Uses DRM ioctl.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <uapi_drm/drm.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(int ac, char **av)
{
    union drm_wait_vblank vblank;
    int fd;

    fd = open("/dev/dri/card0", O_RDWR);
    printf("should print one 'x' every 10 vblanks\n");
    for (;;) {
	bzero(&vblank, sizeof(vblank));
	vblank.request.type = _DRM_VBLANK_RELATIVE;
	vblank.request.sequence = 10;

	if (ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vblank) < 0)
		perror("ioctl");
	write(1, "x", 1);
    }

    return 0;
}
