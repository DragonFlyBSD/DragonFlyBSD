/* $FreeBSD: head/sys/dev/drm2/radeon/radeon_irq_kms.h 254885 2013-08-25 19:37:15Z dumbbell $ */

#ifndef __RADEON_IRQ_KMS_H__
#define	__RADEON_IRQ_KMS_H__

irqreturn_t radeon_driver_irq_handler_kms(DRM_IRQ_ARGS);
void radeon_driver_irq_preinstall_kms(struct drm_device *dev);
int radeon_driver_irq_postinstall_kms(struct drm_device *dev);
void radeon_driver_irq_uninstall_kms(struct drm_device *dev);

int radeon_msi_ok(struct drm_device *dev, unsigned long flags);

#endif /* !defined(__RADEON_IRQ_KMS_H__) */
