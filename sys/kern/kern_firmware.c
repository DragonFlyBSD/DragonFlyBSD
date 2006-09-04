/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Johannes Hofmann <Hoannes.Hofmann.gmx.de> and
 * Joerg Sonnenberger <joerg@bec.de>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/kern/kern_firmware.c,v 1.4 2006/09/04 07:00:58 dillon Exp $
 */

#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/firmware.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <machine/limits.h>

static TAILQ_HEAD(, fw_image) images;

static struct fw_image	*firmware_image_load_file(const char *);
static struct fw_image	*firmware_prepare_image(const char *, size_t);
static void		 firmware_destroy_image(struct fw_image *);

struct fw_image *
firmware_image_load(const char *image_name)
{
	struct fw_image *img;

	TAILQ_FOREACH(img, &images, fw_link) {
		if (strcmp(img->fw_name, image_name) == 0) {
			++img->fw_refcnt;
			return(img);
		}
	}

	if ((img = firmware_image_load_file(image_name)) != NULL)
		return(img);

	return(NULL);
}

void 
firmware_image_unload(struct fw_image *img)
{
	KKASSERT(img->fw_refcnt > 0);
	if (--img->fw_refcnt > 0)
		return;

	TAILQ_REMOVE(&images, img, fw_link);
	firmware_destroy_image(img);
}

static void
firmware_destroy_image(struct fw_image *img)
{
	bus_dmamap_unload(img->fw_dma_tag, img->fw_dma_map);
	bus_dmamem_free(img->fw_dma_tag, img->fw_image, img->fw_dma_map);
	bus_dma_tag_destroy(img->fw_dma_tag);
	free(__DECONST(char *, img->fw_name), M_DEVBUF);
	free(img, M_DEVBUF);	
}

static void
firmware_dma_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *p;

	KKASSERT(nseg == 1);

	p = arg;
	*p = segs->ds_addr;
}

static struct fw_image *
firmware_prepare_image(const char *imgname, size_t imglen)
{
	struct fw_image *img;
	int error;

 	img = malloc(sizeof(*img), M_DEVBUF, M_WAITOK | M_ZERO);
	img->fw_name = kstrdup(imgname, M_DEVBUF); /* XXX necessary? */
	img->fw_refcnt = 1;
	img->fw_imglen = imglen;

	error = bus_dma_tag_create(NULL, 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    img->fw_imglen, 1, BUS_SPACE_MAXSIZE_32BIT, BUS_DMA_ALLOCNOW,
	    &img->fw_dma_tag);
	if (error)
		goto fail_tag_create;

	error = bus_dmamem_alloc(img->fw_dma_tag, (void **)&img->fw_image,
				 BUS_DMA_WAITOK, &img->fw_dma_map);
	if (error)
		goto fail_dma_alloc;

	error = bus_dmamap_load(img->fw_dma_tag, img->fw_dma_map,
				img->fw_image, img->fw_imglen,
				firmware_dma_map, &img->fw_dma_addr, 0);
	if (error)
		goto fail_dma_load;

	return(img);

fail_dma_load:
	bus_dmamem_free(img->fw_dma_tag, img->fw_image, img->fw_dma_map);
fail_dma_alloc:
	bus_dma_tag_destroy(img->fw_dma_tag);
fail_tag_create:
	free(__DECONST(char *, img->fw_name), M_DEVBUF);
	free(img, M_DEVBUF);
	return(NULL);
}

static char firmware_root[MAXPATHLEN] = "/etc/firmware/";

static struct fw_image *
firmware_image_load_file(const char *image_name) 
{
	struct stat ub;
	struct file *fp;
	struct fw_image *img;
	size_t nread;
	char *fw_path;
	int error;

	fw_path = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	snprintf(fw_path, MAXPATHLEN, "%s/%s", firmware_root, image_name);

	/* XXX: access? */

	error = fp_open(fw_path, O_RDONLY|O_NOFOLLOW, 0600, &fp);
        if (error != 0)
		goto fail_open;

	if (fp_stat(fp, &ub) != 0) {
		if (bootverbose)
			printf("fp_stat on firmware image %s failed: %d\n",
			       fw_path, error);
		goto fail_stat;
	}
	if (ub.st_size > SIZE_T_MAX) {
		printf("firmware image %s is too large\n", fw_path);
		goto fail_stat;
	}

	/* XXX: file type */
	img = firmware_prepare_image(image_name, (size_t)ub.st_size);
	if (img == NULL)
		goto fail_stat;

	error = fp_read(fp, img->fw_image, img->fw_imglen, &nread, 1);
	if (error != 0 || nread != img->fw_imglen) {
		printf("firmware image could not be read: %d\n", error);
                goto fail_read;
	}
	fp_close(fp);
	TAILQ_INSERT_HEAD(&images, img, fw_link);

	return(img);

fail_read:
	firmware_destroy_image(img);
fail_stat:
	fp_close(fp);
fail_open:
	free(fw_path, M_TEMP);
	return(NULL);
}
