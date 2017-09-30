#
# Makefile for the drm device(radeonkms) driver.
#
# Please keep SRC list structured in blocks and sorted.
#


KMOD	= radeonkms
SRCS	=								\
	rn50_reg_safe.h							\
	r100_reg_safe.h							\
	r200_reg_safe.h							\
	rv515_reg_safe.h						\
	r300_reg_safe.h							\
	r420_reg_safe.h							\
	rs600_reg_safe.h						\
	r600_reg_safe.h							\
	evergreen_reg_safe.h						\
	cayman_reg_safe.h
SRCS	+=								\
	radeon_acpi.c							\
	radeon_agp.c							\
	radeon_asic.c							\
	radeon_atombios.c						\
	radeon_benchmark.c						\
	radeon_bios.c							\
	radeon_clocks.c							\
	radeon_combios.c						\
	radeon_connectors.c						\
	radeon_cs.c							\
	radeon_cursor.c							\
	radeon_device.c							\
	radeon_display.c						\
	radeon_drv.c							\
	radeon_encoders.c						\
	radeon_fb.c							\
	radeon_fence.c							\
	radeon_gart.c							\
	radeon_gem.c							\
	radeon_i2c.c							\
	radeon_ib.c							\
	radeon_irq_kms.c						\
	radeon_kms.c							\
	radeon_legacy_crtc.c						\
	radeon_legacy_encoders.c					\
	radeon_legacy_tv.c						\
	radeon_object.c							\
	radeon_pm.c							\
	radeon_ring.c							\
	radeon_sa.c							\
	radeon_semaphore.c						\
	radeon_test.c							\
	radeon_ttm.c							\
	radeon_ucode.c							\
	radeon_vm.c							\
	atom.c								\
	atombios_crtc.c							\
	atombios_dp.c							\
	atombios_encoders.c						\
	atombios_i2c.c							\
	r100.c								\
	r200.c								\
	r300.c								\
	r420.c								\
	rs400.c								\
	rs600.c								\
	rs690.c								\
	rv515.c								\
	r520.c								\
	r600.c								\
	r600_blit_shaders.c						\
	r600_cs.c							\
	r600_hdmi.c							\
	rv770.c								\
	dce3_1_afmt.c							\
	evergreen.c							\
	evergreen_blit_shaders.c					\
	evergreen_cs.c							\
	evergreen_hdmi.c						\
	cayman_blit_shaders.c						\
	ni.c								\
	si.c								\
	si_blit_shaders.c						\
	cik.c								\
	cik_blit_shaders.c						\
	r600_dpm.c							\
	rs780_dpm.c							\
	rv6xx_dpm.c							\
	rv770_dpm.c							\
	rv730_dpm.c							\
	rv740_dpm.c							\
	rv770_smc.c							\
	cypress_dpm.c							\
	btc_dpm.c							\
	sumo_dpm.c							\
	sumo_smc.c							\
	trinity_dpm.c							\
	trinity_smc.c							\
	ni_dpm.c							\
	si_smc.c							\
	si_dpm.c							\
	kv_smc.c							\
	kv_dpm.c							\
	ci_smc.c							\
	ci_dpm.c							\
	dce6_afmt.c

# async DMA block
SRCS	+=								\
	r600_dma.c							\
	rv770_dma.c							\
	evergreen_dma.c							\
	ni_dma.c							\
	si_dma.c							\
	cik_sdma.c

# UVD block
SRCS	+=								\
	radeon_uvd.c							\
	uvd_v1_0.c							\
	uvd_v2_2.c							\
	uvd_v3_1.c							\
	uvd_v4_2.c

# add VCE block
SRCS	+=								\
	radeon_vce.c							\
	vce_v1_0.c							\
	vce_v2_0.c

#radeon_ioc32.c
#radeon_prime.c
#--radeon_trace_points.c

SRCS	+=								\
	opt_acpi.h							\
	opt_drm.h							\
	opt_ktr.h							\
	acpi_if.h							\
	bus_if.h							\
	device_if.h							\
	iicbb_if.h							\
	iicbus_if.h							\
	pci_if.h

KCFLAGS+= -I${SYSDIR}/dev/drm/include
KCFLAGS+= -I${SYSDIR}/contrib/dev/acpica/source/include

.include <bsd.kmod.mk>
