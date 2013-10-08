/* $FreeBSD: head/sys/dev/drm2/radeon/radeon_gem.h 254885 2013-08-25 19:37:15Z dumbbell $ */

#ifndef __RADEON_GEM_H__
#define	__RADEON_GEM_H__

#include <drm/drmP.h>

int radeon_gem_object_init(struct drm_gem_object *obj);
void radeon_gem_object_free(struct drm_gem_object *obj);
int radeon_gem_object_open(struct drm_gem_object *obj,
				struct drm_file *file_priv);
void radeon_gem_object_close(struct drm_gem_object *obj,
				struct drm_file *file_priv);

#endif /* !defined(__RADEON_GEM_H__) */
