/*
    FILE:   os.h
    DATE:   2/7/00

    This file contains the os interface. Note that the os interface is
    itself an OS-independent API. The OS specific module is implemented
    by ndis.c for Win9X/NT and linuxnet.c for linux.
*/
#ifndef _OS_H_
#define _OS_H_

#include "phy.h"

#define HDO_VERSION_STRING "HDR O: $Revision: #21 $";

// This is the maximum packet size that we will be sending
#define MAX_PACKET_SIZE     2048
//#define RX_BUFFER_SIZE      2048

typedef struct  _MEMORY_BLOCK
{
    PVOID   pLogical;
    PVOID   pPhysical;
    UINT    uiLength;
}   MEMORY_BLOCK, *PMEMORY_BLOCK;

#define		ALLOC_MEMORY_NONCACHED	0x0001
#define		ALLOC_MEMORY_ALIGNED	0x0002

typedef struct  _MEMORY_BLOCKEX
{
    PVOID   pLogical;
    PVOID   pPhysical;
    UINT    uiLength;
	/* Parameter to OS layer to indicate what type of memory is needed */
	USHORT	AllocFlags;
	USHORT	AlignmentSize; //always power of 2
	/* Following three fields used for aligned memory allocation */
    PVOID   pLogicalOrig;
    ULONG	pPhysicalOrigLow;
    ULONG	pPhysicalOrigHigh;
    UINT    uiLengthOrig;
}   MEMORY_BLOCKEX, *PMEMORY_BLOCKEX;


// The typedefs for the OS functions
typedef int (* PFN_MEMORY_ALLOC) (PVOID pOSCX, PMEMORY_BLOCK pMem);
typedef int (* PFN_MEMORY_FREE)  (PVOID pOSCX, PMEMORY_BLOCK pMem);
typedef int (* PFN_MEMORY_ALLOCEX) (PVOID pOSCX, PMEMORY_BLOCKEX pMem);
typedef int (* PFN_MEMORY_FREEEX)  (PVOID pOSCX, PMEMORY_BLOCKEX pMem);
typedef int (* PFN_CLEAR_MEMORY)  (PVOID pOSCX, PVOID pMem, int iLength);
typedef int (* PFN_STALL_EXECUTION) (PVOID pOSCX, ULONG ulTimeInMicroseconds);
typedef int (* PFN_ALLOC_RECEIVE_BUFFER) (PVOID pOSCX, PMEMORY_BLOCK pMem, PVOID *ppvID);
typedef int (* PFN_FREE_RECEIVE_BUFFER) (PVOID pOSCX, PMEMORY_BLOCK pMem, PVOID pvID);
typedef int (* PFN_PACKET_WAS_SENT) (PVOID pOSCX, PVOID pvID, ULONG ulSuccess);
typedef int (* PFN_PACKET_WAS_RECEIVED) (PVOID pOSCX, PVOID pvADReadData, ULONG ulSuccess, UCHAR *pNewBuffer, UCHAR uc8021pPriority);
typedef int (* PFN_LINK_STATE_HAS_CHANGED) (PVOID pOSCX, int nEnabled);
typedef int (* PFN_ALLOC_TIMER) (PVOID pvContext, PVOID *ppvTimer);
typedef int (* PFN_FREE_TIMER) (PVOID pvContext, PVOID pvTimer);
typedef int (* PFN_INITIALIZE_TIMER) (PVOID pvContext, PVOID pvTimer, PTIMER_FUNC pvFunc, PVOID pvFuncParameter);
typedef int (* PFN_SET_TIMER) (PVOID pvContext, PVOID pvTimer, ULONG dwMillisecondsDelay);
typedef int (* PFN_CANCEL_TIMER) (PVOID pvContext, PVOID pvTimer);

typedef int (* PFN_PREPROCESS_PACKET) (PVOID pvContext, PVOID pvADReadData, PVOID *ppvID,
				UCHAR *pNewBuffer, UCHAR uc8021pPriority);
typedef PVOID (* PFN_PREPROCESS_PACKET_NOPQ) (PVOID pvContext, PVOID pvADReadData);
typedef int (* PFN_INDICATE_PACKETS) (PVOID pvContext, PVOID *ppvID, ULONG ulNumPacket);
typedef int (* PFN_LOCK_ALLOC) (PVOID pOSCX, int iLockType, PVOID *ppvLock);
typedef int (* PFN_LOCK_ACQUIRE) (PVOID pOSCX, int iLockType, PVOID pvLock);
typedef int (* PFN_LOCK_RELEASE) (PVOID pOSCX, int iLockType, PVOID pvLock);
typedef PVOID (* PFN_RETURN_BUFFER_VIRTUAL) (PVOID pvContext, PVOID pvADReadData);

// Here are the OS functions that those objects below the OS interface
// can call up to.
typedef struct  _OS_API
{
    // OS Context -- this is a parameter to every OS API call
    PVOID                       pOSCX;

    // Basic OS functions
    PFN_MEMORY_ALLOC            pfnAllocMemory;
    PFN_MEMORY_FREE             pfnFreeMemory;
    PFN_MEMORY_ALLOCEX          pfnAllocMemoryEx;
    PFN_MEMORY_FREEEX           pfnFreeMemoryEx;
    PFN_CLEAR_MEMORY            pfnClearMemory;
    PFN_STALL_EXECUTION         pfnStallExecution;
    PFN_ALLOC_RECEIVE_BUFFER    pfnAllocReceiveBuffer;
    PFN_FREE_RECEIVE_BUFFER     pfnFreeReceiveBuffer;
    PFN_PACKET_WAS_SENT         pfnPacketWasSent;
    PFN_PACKET_WAS_RECEIVED     pfnPacketWasReceived;
    PFN_LINK_STATE_HAS_CHANGED  pfnLinkStateHasChanged;
	PFN_ALLOC_TIMER				pfnAllocTimer;
	PFN_FREE_TIMER				pfnFreeTimer;
	PFN_INITIALIZE_TIMER		pfnInitializeTimer;
	PFN_SET_TIMER				pfnSetTimer;
	PFN_CANCEL_TIMER			pfnCancelTimer;
    PFN_PREPROCESS_PACKET       pfnPreprocessPacket;
    PFN_PREPROCESS_PACKET_NOPQ  pfnPreprocessPacketNopq;
    PFN_INDICATE_PACKETS        pfnIndicatePackets;
	PFN_LOCK_ALLOC				pfnLockAlloc;
	PFN_LOCK_ACQUIRE			pfnLockAcquire;
	PFN_LOCK_RELEASE			pfnLockRelease;
	PFN_RETURN_BUFFER_VIRTUAL	pfnReturnBufferVirtual;
}   OS_API, *POS_API;

#endif // _OS_H_
