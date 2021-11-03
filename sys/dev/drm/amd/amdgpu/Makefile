#
# Makefile for the drm/amdgpu driver.
#

KMOD=	amdgpu

.PATH:	${.CURDIR}/../../scheduler \
	${.CURDIR}/../powerplay \
	${.CURDIR}/../powerplay/hwmgr \
	${.CURDIR}/../powerplay/smumgr \
	${.CURDIR}/../lib

SRCS=	amdgpu_drv.c

SRCS+=	chash.c

# add KMS driver
SRCS+= amdgpu_device.c amdgpu_kms.c \
	amdgpu_atombios.c atombios_crtc.c amdgpu_connectors.c \
	atom.c amdgpu_fence.c amdgpu_ttm.c amdgpu_object.c amdgpu_gart.c \
	amdgpu_encoders.c amdgpu_display.c amdgpu_i2c.c \
	amdgpu_fb.c amdgpu_gem.c amdgpu_ring.c \
	amdgpu_cs.c amdgpu_bios.c amdgpu_benchmark.c amdgpu_test.c \
	amdgpu_pm.c atombios_dp.c amdgpu_afmt.c amdgpu_trace_points.c \
	atombios_encoders.c amdgpu_sa.c atombios_i2c.c \
	amdgpu_prime.c amdgpu_vm.c amdgpu_ib.c amdgpu_pll.c \
	amdgpu_ucode.c amdgpu_bo_list.c amdgpu_ctx.c amdgpu_sync.c \
	amdgpu_gtt_mgr.c amdgpu_vram_mgr.c amdgpu_virt.c amdgpu_atomfirmware.c \
	amdgpu_queue_mgr.c amdgpu_vf_error.c amdgpu_sched.c amdgpu_debugfs.c \
	amdgpu_ids.c

SRCS+= \
	vi.c mxgpu_vi.c nbio_v6_1.c soc15.c emu_soc.c mxgpu_ai.c nbio_v7_0.c vega10_reg_init.c \
	vega20_reg_init.c

# add DF block
SRCS+= \
	df_v1_7.c \
	df_v3_6.c

# add GMC block
SRCS+= \
	gmc_v7_0.c \
	gmc_v8_0.c \
	gfxhub_v1_0.c mmhub_v1_0.c gmc_v9_0.c

# add IH block
SRCS+= \
	amdgpu_irq.c \
	amdgpu_ih.c \
	iceland_ih.c \
	tonga_ih.c \
	cz_ih.c \
	vega10_ih.c

# add PSP block
SRCS+= \
	amdgpu_psp.c \
	psp_v3_1.c \
	psp_v10_0.c

# add SMC block
SRCS+= \
	amdgpu_dpm.c

# add DCE block
SRCS+= \
	dce_v10_0.c \
	dce_v11_0.c \
	dce_virtual.c

# add GFX block
SRCS+= \
	amdgpu_gfx.c \
	gfx_v8_0.c \
	gfx_v9_0.c

# add async DMA block
SRCS+= \
	sdma_v2_4.c \
	sdma_v3_0.c \
	sdma_v4_0.c

# add UVD block
SRCS+= \
	amdgpu_uvd.c \
	uvd_v5_0.c \
	uvd_v6_0.c \
	uvd_v7_0.c

# add VCE block
SRCS+= \
	amdgpu_vce.c \
	vce_v3_0.c \
	vce_v4_0.c

# add VCN block
SRCS+= \
	amdgpu_vcn.c \
	vcn_v1_0.c

# add amdkfd interfaces
SRCS+= amdgpu_amdkfd.c

# add cgs
SRCS+= amdgpu_cgs.c

# GPU scheduler
SRCS+= amdgpu_job.c

SRCS+= amdgpu_acpi.c

# powerplay/hwmgr
SRCS+= hwmgr.c processpptables.c \
		hardwaremanager.c smu8_hwmgr.c \
		pppcielanes.c\
		process_pptables_v1_0.c ppatomctrl.c ppatomfwctrl.c \
		smu7_hwmgr.c smu7_powertune.c smu7_thermal.c \
		smu7_clockpowergating.c \
		vega10_processpptables.c vega10_hwmgr.c vega10_powertune.c \
		vega10_thermal.c smu10_hwmgr.c pp_psm.c\
		vega12_processpptables.c vega12_hwmgr.c \
		vega12_thermal.c \
		pp_overdriver.c smu_helper.c

# powerplay/smumgr
SRCS+= smumgr.c smu8_smumgr.c tonga_smumgr.c fiji_smumgr.c \
	  polaris10_smumgr.c iceland_smumgr.c \
	  smu7_smumgr.c vega10_smumgr.c smu10_smumgr.c ci_smumgr.c \
	  vega12_smumgr.c vegam_smumgr.c smu9_smumgr.c 

SRCS+= \
	opt_ddb.h 	\
	opt_acpi.h	\
	opt_drm.h 	\
	acpi_if.h	\
	bus_if.h	\
	device_if.h	\
	pci_if.h

# From linux
#TODO: remove when Timing Sync feature is complete
KCFLAGS+= -DBUILD_FEATURE_TIMING_SYNC=0

# fix display/include/link_service_types.h
KCFLAGS+= -DLITTLEENDIAN_CPU

KCFLAGS+= -I${SYSDIR}/dev/drm/include
KCFLAGS+= -I${SYSDIR}/dev/drm/include/drm
KCFLAGS+= -I${SYSDIR}/dev/drm/include/uapi
KCFLAGS+= -I${SYSDIR}/dev/drm/include/uapi/drm
KCFLAGS+= -I${SYSDIR}/contrib/dev/acpica/source/include
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/amdgpu
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/dc
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/amdgpu_dm
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/display/include
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/include
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/include/asic_reg
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/powerplay/inc
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/powerplay/hwmgr
KCFLAGS+= -I${SYSDIR}/dev/drm/amd/powerplay/smumgr
KCFLAGS+= -I${SYSDIR}/dev/drm/scheduler
KCFLAGS+= -include ${SYSDIR}/dev/drm/kconfig.h

.include <bsd.kmod.mk>
