/*
 * Copyright (c)  1999 - 2002 Intel Corporation. All rights reserved
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license. Except as permitted by such
 * license, no part of this software or documentation may be
 * reproduced, stored in a retrieval system, or transmitted in any
 * form or by any means without the express written consent of
 * Intel Corporation.
 */

#ifndef _EFI_INCLUDE_
#define _EFI_INCLUDE_

#ifdef	__x86_64__
#define	EFIAPI	__attribute__((ms_abi))
#endif

/*
 * The following macros are defined unconditionally in the EDK II headers,
 * so get our definitions out of the way.
 */
#undef NULL
#undef MIN
#undef MAX

#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Guid/DebugImageInfoTable.h>
#include <Guid/DxeServices.h>
#include <Guid/HobList.h>
#include <Guid/Mps.h>
#include <Guid/SmBios.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/NetworkInterfaceIdentifier.h>
#include <Protocol/PciIo.h>
#include <Protocol/SerialIo.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/UgaDraw.h>

/*
 * The following macros haven been preserved from the old EFI headers for now.
 */
#define	EFI_DP_TYPE_MASK	0x7f

#define	END_DEVICE_PATH_TYPE	0x7f

#define	DevicePathType(a)		(((a)->Type) & EFI_DP_TYPE_MASK)
#define	DevicePathSubType(a)		((a)->SubType)
#define	DevicePathNodeLength(a)		((size_t)(((a)->Length[0]) | ((a)->Length[1] << 8)))
#define	NextDevicePathNode(a)		((EFI_DEVICE_PATH *)(((UINT8 *)(a)) + DevicePathNodeLength(a)))
#define	IsDevicePathType(a, t)		(DevicePathType(a) == t)
#define	IsDevicePathEndType(a)		IsDevicePathType(a, END_DEVICE_PATH_TYPE)
#define	IsDevicePathEndSubType(a)	((a)->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE)
#define	IsDevicePathEnd(a)		(IsDevicePathEndType(a) && IsDevicePathEndSubType(a))

#define	SetDevicePathEndNode(a)	do {			\
	(a)->Type = END_DEVICE_PATH_TYPE;		\
	(a)->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;	\
	(a)->Length[0] = sizeof(EFI_DEVICE_PATH);	\
	(a)->Length[1] = 0;				\
} while (0)

#define	NextMemoryDescriptor(Ptr,Size)	((EFI_MEMORY_DESCRIPTOR *)(((UINT8 *)Ptr) + Size))

#define	FDT_TABLE_GUID \
	{ 0xb1b621d5, 0xf19c, 0x41a5, {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0} }

#define	MEMORY_TYPE_INFORMATION_TABLE_GUID \
	{ 0x4c19049f, 0x4137, 0x4dd3, {0x9c, 0x10, 0x8b, 0x97, 0xa8, 0x3f, 0xfd, 0xfa} }

#endif
