#ifndef __RADEON_GEM_H__
#define	__RADEON_GEM_H__

#include <drm/drmP.h>

void radeon_gem_object_free(struct drm_gem_object *obj);
int radeon_gem_object_open(struct drm_gem_object *obj,
				struct drm_file *file_priv);
void radeon_gem_object_close(struct drm_gem_object *obj,
				struct drm_file *file_priv);

#endif /* !defined(__RADEON_GEM_H__) */
