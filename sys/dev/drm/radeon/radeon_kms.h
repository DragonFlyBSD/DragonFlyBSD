#ifndef __RADEON_KMS_H__
#define	__RADEON_KMS_H__

#include <drm/drmP.h>

int radeon_driver_load_kms(struct drm_device *dev, unsigned long flags);
int radeon_driver_unload_kms(struct drm_device *dev);

void radeon_driver_lastclose_kms(struct drm_device *dev);
int radeon_driver_open_kms(struct drm_device *dev, struct drm_file *file_priv);
void radeon_driver_postclose_kms(struct drm_device *dev,
				 struct drm_file *file_priv);
void radeon_driver_preclose_kms(struct drm_device *dev,
				struct drm_file *file_priv);

#endif /* !defined(__RADEON_KMS_H__) */
