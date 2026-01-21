/** @file
  Processor or Compiler specific defines and types AArch64.

  Copyright (c) 2006 - 2019, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __PROCESSOR_BIND_H__
#define __PROCESSOR_BIND_H__

///
/// Define the processor type so other code can make processor based choices
///
#define MDE_CPU_AARCH64

//
// Make sure we are using the correct packing rules per EFI specification
//
#if !defined(__GNUC__)
#pragma pack()
#endif

#if defined(_MSC_EXTENSIONS)
  typedef unsigned __int64    UINT64;
  typedef __int64             INT64;
  typedef unsigned __int32    UINT32;
  typedef __int32             INT32;
  typedef unsigned short      UINT16;
  typedef unsigned short      CHAR16;
  typedef short               INT16;
  typedef unsigned char       BOOLEAN;
  typedef unsigned char       UINT8;
  typedef char                CHAR8;
  typedef signed char         INT8;
#else
  typedef unsigned long long  UINT64;
  typedef long long           INT64;
  typedef unsigned int        UINT32;
  typedef int                 INT32;
  typedef unsigned short      UINT16;
  typedef unsigned short      CHAR16;
  typedef short               INT16;
  typedef unsigned char       BOOLEAN;
  typedef unsigned char       UINT8;
  typedef char                CHAR8;
  typedef signed char         INT8;
#endif

///
/// Unsigned value of native width.
///
typedef UINT64  UINTN;
///
/// Signed value of native width.
///
typedef INT64   INTN;

//
// Processor specific defines
//

///
/// A value of native width with the highest bit set.
///
#define MAX_BIT     0x8000000000000000ULL
///
/// A value of native width with the two highest bits set.
///
#define MAX_2_BITS  0xC000000000000000ULL

///
/// Maximum legal AArch64 address
///
#define MAX_ADDRESS   0xFFFFFFFFFFFFFFFFULL

///
/// Maximum usable address at boot time
///
#define MAX_ALLOC_ADDRESS   MAX_ADDRESS

///
/// Maximum legal AArch64 INTN and UINTN values.
///
#define MAX_INTN   ((INTN)0x7FFFFFFFFFFFFFFFULL)
#define MAX_UINTN  ((UINTN)0xFFFFFFFFFFFFFFFFULL)

///
/// Minimum legal AArch64 INTN value.
///
#define MIN_INTN   (((INTN)-9223372036854775807LL) - 1)

///
/// The stack alignment required for AArch64
///
#define CPU_STACK_ALIGNMENT   16

///
/// Page allocation granularity for AArch64
///
#define DEFAULT_PAGE_ALLOCATION_GRANULARITY   (0x1000)
#define RUNTIME_PAGE_ALLOCATION_GRANULARITY   (0x1000)

//
// Modifier to ensure that all protocol member functions and EFI intrinsics
// use the correct C calling convention.
//
#ifdef EFIAPI
#elif defined(_MSC_EXTENSIONS)
  #define EFIAPI __cdecl
#else
  #define EFIAPI
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define ASM_GLOBAL .globl
#endif

#ifndef __USER_LABEL_PREFIX__
#define __USER_LABEL_PREFIX__
#endif

#endif
