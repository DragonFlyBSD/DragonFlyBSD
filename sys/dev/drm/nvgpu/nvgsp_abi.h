/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP-RM ABI structures for r570 (TU102 GSP-RM 570.144).
 *
 * Mirrored verbatim from open-gpu-kernel-modules 570.144 via nouveau:
 *   linux/drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r570/nvrm/gsp.h
 *
 * Keep field names/types/order identical -- the compiler computes the
 * exact layout GSP-RM expects.
 */

#ifndef _NVGSP_ABI_H_
#define _NVGSP_ABI_H_

#include <sys/types.h>

typedef uint8_t  NvU8;
typedef uint16_t NvU16;
typedef uint32_t NvU32;
typedef uint64_t NvU64;
typedef int32_t  NV_STATUS;
typedef uint8_t  NvBool;	/* open-rm uses NvU8-sized bool */
typedef uint32_t NvHandle;

#define NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS 16U

typedef struct {
	NvU16 deviceID;
	NvU16 vendorID;
	NvU16 subdeviceID;
	NvU16 subvendorID;
	NvU8  revisionID;
} BUSINFO;

typedef struct {
	NV_STATUS status;
	NvU32     acpiIdListLen;
	NvU32     acpiIdList[NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS];
} DOD_METHOD_DATA;

typedef struct {
	NV_STATUS status;
	NvU32     jtCaps;
	NvU16     jtRevId;
	NvBool    bSBIOSCaps;
} JT_METHOD_DATA;

typedef struct {
	NvU32     acpiId;
	NvU32     mode;
	NV_STATUS status;
} MUX_METHOD_DATA_ELEMENT;

typedef struct {
	NvU32 tableLen;
	MUX_METHOD_DATA_ELEMENT acpiIdMuxModeTable [NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS];
	MUX_METHOD_DATA_ELEMENT acpiIdMuxPartTable [NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS];
	MUX_METHOD_DATA_ELEMENT acpiIdMuxStateTable[NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS];
} MUX_METHOD_DATA;

typedef struct {
	NV_STATUS status;
	NvU32     optimusCaps;
} CAPS_METHOD_DATA;

typedef struct {
	NvBool             bValid;
	DOD_METHOD_DATA    dodMethodData;
	JT_METHOD_DATA     jtMethodData;
	MUX_METHOD_DATA    muxMethodData;
	CAPS_METHOD_DATA   capsMethodData;
} ACPI_METHOD_DATA;

typedef struct {
	NvU32  totalVFs;
	NvU32  firstVFOffset;
	NvU64  FirstVFBar0Address;
	NvU64  FirstVFBar1Address;
	NvU64  FirstVFBar2Address;
	NvBool b64bitBar0;
	NvBool b64bitBar1;
	NvBool b64bitBar2;
} GSP_VF_INFO;

typedef struct {
	NvU32 linkCap;
} GSP_PCIE_CONFIG_REG;

typedef struct GspSystemInfo {
	NvU64 gpuPhysAddr;
	NvU64 gpuPhysFbAddr;
	NvU64 gpuPhysInstAddr;
	NvU64 gpuPhysIoAddr;
	NvU64 nvDomainBusDeviceFunc;
	NvU64 simAccessBufPhysAddr;
	NvU64 notifyOpSharedSurfacePhysAddr;
	NvU64 pcieAtomicsOpMask;
	NvU64 consoleMemSize;
	NvU64 maxUserVa;
	NvU32 pciConfigMirrorBase;
	NvU32 pciConfigMirrorSize;
	NvU32 PCIDeviceID;
	NvU32 PCISubDeviceID;
	NvU32 PCIRevisionID;
	NvU32 pcieAtomicsCplDeviceCapMask;
	NvU8  oorArch;
	NvU64 clPdbProperties;
	NvU32 Chipset;
	NvBool bGpuBehindBridge;
	NvBool bFlrSupported;
	NvBool b64bBar0Supported;
	NvBool bMnocAvailable;
	NvU32  chipsetL1ssEnable;
	NvBool bUpstreamL0sUnsupported;
	NvBool bUpstreamL1Unsupported;
	NvBool bUpstreamL1PorSupported;
	NvBool bUpstreamL1PorMobileOnly;
	NvBool bSystemHasMux;
	NvU8   upstreamAddressValid;
	BUSINFO FHBBusInfo;
	BUSINFO chipsetIDInfo;
	ACPI_METHOD_DATA acpiMethodData;
	NvU32 hypervisorType;
	NvBool bIsPassthru;
	NvU64 sysTimerOffsetNs;
	GSP_VF_INFO gspVFInfo;
	NvBool bIsPrimary;
	NvBool isGridBuild;
	GSP_PCIE_CONFIG_REG pcieConfigReg;
	NvU32 gridBuildCsp;
	NvBool bPreserveVideoMemoryAllocations;
	NvBool bTdrEventSupported;
	NvBool bFeatureStretchVblankCapable;
	NvBool bEnableDynamicGranularityPageArrays;
	NvBool bClockBoostSupported;
	NvBool bRouteDispIntrsToCPU;
	NvU64  hostPageSize;
} GspSystemInfo;

/* PACKED_REGISTRY_TABLE: open-rm publishes this as the SET_REGISTRY payload. */
typedef struct {
	NvU32 size;
	NvU32 numEntries;
	/* PACKED_REGISTRY_ENTRY entries[] -- empty for first-boot */
} PACKED_REGISTRY_TABLE;

/* RPC function codes (NV_VGPU_MSG_FUNCTION_*) used at boot/shutdown. */
#define NV_VGPU_MSG_FUNCTION_UNLOADING_GUEST_DRIVER 47
#define NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO  72
#define NV_VGPU_MSG_FUNCTION_SET_REGISTRY         73

/* Event codes (NV_VGPU_MSG_EVENT_*) we wait for. */
#define NV_VGPU_MSG_EVENT_GSP_INIT_DONE         4097

#endif /* _NVGSP_ABI_H_ */
