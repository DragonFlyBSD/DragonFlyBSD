/* $FreeBSD: head/sys/dev/drm2/radeon/r600_cs.h 254885 2013-08-25 19:37:15Z dumbbell $ */

#ifndef __R600_CS_H__
#define	__R600_CS_H__

int	r600_dma_cs_next_reloc(struct radeon_cs_parser *p,
	    struct radeon_cs_reloc **cs_reloc);

#endif /* !defined(__R600_CS_H__) */
