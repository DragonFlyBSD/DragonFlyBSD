/*
    FILE:   adapter.h
    DATE:   2/7/00

    This file contains the hardware interface to the ethernet adapter.
*/

#ifndef _ADAPTER_H_
#define _ADAPTER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define HDA_VERSION_STRING "HDR A: $Revision: #47 $"

//////////////////////////////////////////////////////////////////
// For the set and get configuration calls.
typedef struct  _ADAPTER_CONFIG
{
    ULONG   ulFlags;
}   ADAPTER_CONFIG, *PADAPTER_CONFIG;
//////////////////////////////////////////////////////////////////

#if defined(_WIN32)
//////////////////////////////////////////////////////////////////
// For the ADAPTER_Write1 call.
/* This scatter gather list should be same as defined in ndis.h by MS.
   For ULONG_PTR MS header file says that it will be of same size as
   pointer. It has been defined to take care of casting between differenet
   sizes.
*/
typedef struct _NVSCATTER_GATHER_ELEMENT {
    ULONG PhysLow;
    ULONG PhysHigh;
    ULONG Length;
    void *Reserved;
} NVSCATTER_GATHER_ELEMENT, *PNVSCATTER_GATHER_ELEMENT;

#pragma warning(disable:4200)
typedef struct _NVSCATTER_GATHER_LIST {
    ULONG NumberOfElements;
    void *Reserved;
    NVSCATTER_GATHER_ELEMENT Elements[];
} NVSCATTER_GATHER_LIST, *PNVSCATTER_GATHER_LIST;
#pragma warning(default:4200)

typedef struct  _ADAPTER_WRITE_DATA1
{
    ULONG                   ulTotalLength;
    PVOID                   pvID;
	UCHAR					uc8021pPriority;
	PNVSCATTER_GATHER_LIST 	pNVSGL;
}   ADAPTER_WRITE_DATA1, *PADAPTER_WRITE_DATA1;

#endif

// For the ADAPTER_Write call.
typedef struct  _ADAPTER_WRITE_ELEMENT
{
    PVOID   pPhysical;
    ULONG   ulLength;
}   ADAPTER_WRITE_ELEMENT, *PADAPTER_WRITE_ELEMENT;

// pvID is a value that will be passed back into OSAPI.pfnPacketWasSent
// when the transmission completes. if pvID is NULL, the ADAPTER code
// assumes the caller does not want the pfnPacketWasSent callback.
typedef struct  _ADAPTER_WRITE_DATA
{
    ULONG                   ulNumberOfElements;
    ULONG                   ulTotalLength;
    PVOID                   pvID;
	UCHAR					uc8021pPriority;
    ADAPTER_WRITE_ELEMENT   sElement[100];
}   ADAPTER_WRITE_DATA, *PADAPTER_WRITE_DATA;
//////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////
// For the ADAPTER_Read call.
typedef struct  _ADAPTER_READ_ELEMENT
{
    PVOID   pPhysical;
    ULONG   ulLength;
}   ADAPTER_READ_ELEMENT, *PADAPTER_READ_ELEMENT;

typedef struct _ADAPTER_READ_DATA
{
    ULONG                   ulNumberOfElements;
    ULONG                   ulTotalLength;
    PVOID                   pvID;
    ULONG                   ulFilterMatch;
    ADAPTER_READ_ELEMENT    sElement[10];
}   ADAPTER_READ_DATA, *PADAPTER_READ_DATA;

// The ulFilterMatch flag can be a logical OR of the following
#define ADREADFL_UNICAST_MATCH          0x00000001
#define ADREADFL_MULTICAST_MATCH        0x00000002
#define ADREADFL_BROADCAST_MATCH        0x00000004
//////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////
// For the ADAPTER_GetStatistics call.
#define MAX_TRANSMIT_COLISION_STATS 16
typedef struct  _ADAPTER_STATS
{
    ULONG   ulSuccessfulTransmissions;
    ULONG   ulFailedTransmissions;
    ULONG   ulRetryErrors;
    ULONG   ulUnderflowErrors;
    ULONG   ulLossOfCarrierErrors;
    ULONG   ulLateCollisionErrors;
    ULONG   ulDeferredTransmissions;
	ULONG	ulExcessDeferredTransmissions;
    ULONG   aulSuccessfulTransmitsAfterCollisions[MAX_TRANSMIT_COLISION_STATS];

    ULONG   ulMissedFrames;
    ULONG   ulSuccessfulReceptions;
    ULONG   ulFailedReceptions;
    ULONG   ulCRCErrors;
    ULONG   ulFramingErrors;
    ULONG   ulOverFlowErrors;
	ULONG	ulFrameErrorsPrivate; //Not for public.
	ULONG	ulNullBufferReceivePrivate; //Not for public, These are the packets which we didn't indicate to OS

	//interrupt related statistics
    ULONG   ulRxInterrupt;
    ULONG   ulRxInterruptUnsuccessful;
    ULONG   ulTxInterrupt;
    ULONG   ulTxInterruptUnsuccessful;
    ULONG   ulPhyInterrupt;
}   ADAPTER_STATS, *PADAPTER_STATS;
//////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////
// For the ADAPTER_GetPowerCapabilities call.
typedef struct  _ADAPTER_POWERCAPS
{
    ULONG   ulPowerFlags;
    ULONG   ulMagicPacketWakeUpFlags;
    ULONG   ulPatternWakeUpFlags;
    ULONG   ulLinkChangeWakeUpFlags;
    int     iMaxWakeUpPatterns;
}   ADAPTER_POWERCAPS, *PADAPTER_POWERCAPS;

// For the ADAPTER_GetPowerState and ADAPTER_SetPowerState call.
typedef struct  _ADAPTER_POWERSTATE
{
    ULONG   ulPowerFlags;
    ULONG   ulMagicPacketWakeUpFlags;
    ULONG   ulPatternWakeUpFlags;
    ULONG   ulLinkChangeWakeUpFlags;
}   ADAPTER_POWERSTATE, *PADAPTER_POWERSTATE;

// Each of the flag fields in the POWERCAPS structure above can have
// any of the following bitflags set giving the capabilites of the
// adapter. In the case of the wake up fields, these flags mean that
// wake up can happen from the specified power state.

// For the POWERSTATE structure, the ulPowerFlags field should just
// have one of these bits set to go to that particular power state.
// The WakeUp fields can have one or more of these bits set to indicate
// what states should be woken up from.
#define POWER_STATE_D0          0x00000001
#define POWER_STATE_D1          0x00000002
#define POWER_STATE_D2          0x00000004
#define POWER_STATE_D3          0x00000008

#define POWER_STATE_ALL         (POWER_STATE_D0 | \
                                POWER_STATE_D1  | \
                                POWER_STATE_D2  | \
                                POWER_STATE_D3)
//////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////
// The ADAPTER_GetPacketFilterCaps call returns a ULONG that can
// have the following capability bits set.
#define ACCEPT_UNICAST_PACKETS      0x00000001
#define ACCEPT_MULTICAST_PACKETS    0x00000002
#define ACCEPT_BROADCAST_PACKETS    0x00000004
#define ACCEPT_ALL_PACKETS          0x00000008

#define ETH_LENGTH_OF_ADDRESS		6

// The ADAPTER_SetPacketFilter call uses this structure to know what
// packet filter to set. The ulPacketFilter field can contain some
// union of the bit flags above. The acMulticastMask array holds a
// 48 bit MAC address mask with a 0 in every bit position that should
// be ignored on compare and a 1 in every bit position that should
// be taken into account when comparing to see if the destination
// address of a packet should be accepted for multicast.
typedef struct  _PACKET_FILTER
{
    ULONG   ulFilterFlags;
    UCHAR   acMulticastAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR   acMulticastMask[ETH_LENGTH_OF_ADDRESS];
}   PACKET_FILTER, *PPACKET_FILTER;
//////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////
// A WAKE_UP_PATTERN is a 128-byte pattern that the adapter can
// look for in incoming packets to decide when to wake up.  Higher-
// level protocols can use this to, for example, wake up the
// adapter whenever it sees an IP packet that is addressed to it.
// A pattern consists of 128 bits of byte masks that indicate
// which bytes in the packet are relevant to the pattern, plus
// values for each byte.
#define WAKE_UP_PATTERN_SIZE 128

typedef struct _WAKE_UP_PATTERN
{
    ULONG   aulByteMask[WAKE_UP_PATTERN_SIZE/32];
    UCHAR   acData[WAKE_UP_PATTERN_SIZE];
}   WAKE_UP_PATTERN, *PWAKE_UP_PATTERN;



//
//
// Adapter offload
//
typedef struct _ADAPTER_OFFLOAD {

	ULONG Type;
	ULONG Value0;

} ADAPTER_OFFLOAD, *PADAPTER_OFFLOAD;

#define ADAPTER_OFFLOAD_VLAN		0x00000001
#define ADAPTER_OFFLOAD_IEEE802_1P	0x00000002
#define ADAPTER_OFFLOAD_IEEE802_1PQ_PAD	0x00000004

//////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////
// The functional typedefs for the ADAPTER Api
typedef int (* PFN_ADAPTER_CLOSE)  (PVOID pvContext);
typedef int (* PFN_ADAPTER_INIT)  (PVOID pvContext, USHORT usForcedSpeed, UCHAR ucForceDpx, UCHAR ucForceMode, UINT *puiLinkState);
typedef int (* PFN_ADAPTER_DEINIT)  (PVOID pvContext, UCHAR ucIsPowerDown);
typedef int (* PFN_ADAPTER_START)  (PVOID pvContext);
typedef int (* PFN_ADAPTER_STOP)   (PVOID pvContext, UCHAR ucIsPowerDown);
typedef int (* PFN_ADAPTER_QUERY_WRITE_SLOTS) (PVOID pvContext);
typedef int (* PFN_ADAPTER_WRITE) (PVOID pvContext, ADAPTER_WRITE_DATA *pADWriteData);

#if defined(_WIN32)
typedef int (* PFN_ADAPTER_WRITE1) (PVOID pvContext, ADAPTER_WRITE_DATA1 *pADWriteData1);
#endif

typedef int (* PFN_ADAPTER_QUERY_INTERRUPT) (PVOID pvContext);
typedef int (* PFN_ADAPTER_HANDLE_INTERRUPT) (PVOID pvContext);
typedef int (* PFN_ADAPTER_DISABLE_INTERRUPTS) (PVOID pvContext);
typedef int (* PFN_ADAPTER_ENABLE_INTERRUPTS) (PVOID pvContext);
typedef int (* PFN_ADAPTER_CLEAR_INTERRUPTS) (PVOID pvContext);
typedef int (* PFN_ADAPTER_CLEAR_TX_DESC) (PVOID pvContext);
typedef int (* PFN_ADAPTER_GET_LINK_SPEED) (PVOID pvContext);
typedef int (* PFN_ADAPTER_GET_LINK_STATE) (PVOID pvContext, ULONG *pulLinkState);
typedef int (* PFN_ADAPTER_IS_LINK_INITIALIZING) (PVOID pvContext);
typedef int (* PFN_ADAPTER_RESET_PHY_INIT_STATE) (PVOID pvContext);
typedef int (* PFN_ADAPTER_GET_TRANSMIT_QUEUE_SIZE) (PVOID pvContext);
typedef int (* PFN_ADAPTER_GET_RECEIVE_QUEUE_SIZE) (PVOID pvContext);
typedef int (* PFN_ADAPTER_GET_STATISTICS) (PVOID pvContext, PADAPTER_STATS pADStats);
typedef int (* PFN_ADAPTER_GET_POWER_CAPS) (PVOID pvContext, PADAPTER_POWERCAPS pADPowerCaps);
typedef int (* PFN_ADAPTER_GET_POWER_STATE) (PVOID pvContext, PADAPTER_POWERSTATE pADPowerState);
typedef int (* PFN_ADAPTER_SET_POWER_STATE) (PVOID pvContext, PADAPTER_POWERSTATE pADPowerState);
typedef int (* PFN_ADAPTER_GET_PACKET_FILTER_CAPS) (PVOID pvContext);
typedef int (* PFN_ADAPTER_SET_PACKET_FILTER) (PVOID pvContext, PPACKET_FILTER pPacketFilter);
typedef int (* PFN_ADAPTER_SET_WAKE_UP_PATTERN) (PVOID pvContext, int iPattern, PWAKE_UP_PATTERN pPattern);
typedef int (* PFN_ADAPTER_ENABLE_WAKE_UP_PATTERN) (PVOID pvContext, int iPattern, int iEnable);
typedef int (* PFN_SET_NODE_ADDRESS) (PVOID pvContext, UCHAR *pNodeAddress);
typedef int (* PFN_GET_NODE_ADDRESS) (PVOID pvContext, UCHAR *pNodeAddress);
typedef int (* PFN_GET_ADAPTER_INFO) (PVOID pvContext, PVOID pVoidPtr, int iType, int *piLength);
typedef int (* PFN_ADAPTER_READ_PHY)  (PVOID pvContext, ULONG ulPhyAddr, ULONG ulPhyReg, ULONG *pulValue);
typedef int (* PFN_ADAPTER_WRITE_PHY) (PVOID pvContext, ULONG ulPhyAddr, ULONG ulPhyReg, ULONG ulValue);
typedef void(* PFN_ADAPTER_SET_SPPED_DUPLEX) (PVOID pvContext);
typedef int (*PFN_REGISTER_OFFLOAD) (PVOID pvContext,  PADAPTER_OFFLOAD pOffload);
typedef int (*PFN_DEREGISTER_OFFLOAD) (PVOID pvContext, PADAPTER_OFFLOAD pOffload);
typedef int (*PFN_RX_BUFF_READY) (PVOID pvContext, PMEMORY_BLOCK pMemBlock, PVOID pvID);

typedef struct  _ADAPTER_API
{
    // The adapter context
    PVOID                                   pADCX;

    // The adapter interface
    PFN_ADAPTER_CLOSE                       pfnClose;
    PFN_ADAPTER_INIT                        pfnInit;
    PFN_ADAPTER_DEINIT                      pfnDeinit;
    PFN_ADAPTER_START                       pfnStart;
    PFN_ADAPTER_STOP                        pfnStop;
    PFN_ADAPTER_QUERY_WRITE_SLOTS           pfnQueryWriteSlots;
    PFN_ADAPTER_WRITE                       pfnWrite;

#if defined(_WIN32)
    PFN_ADAPTER_WRITE1                      pfnWrite1;
#endif
    PFN_ADAPTER_QUERY_INTERRUPT             pfnQueryInterrupt;
    PFN_ADAPTER_HANDLE_INTERRUPT            pfnHandleInterrupt;
    PFN_ADAPTER_DISABLE_INTERRUPTS          pfnDisableInterrupts;
    PFN_ADAPTER_ENABLE_INTERRUPTS           pfnEnableInterrupts;
    PFN_ADAPTER_CLEAR_INTERRUPTS            pfnClearInterrupts;
    PFN_ADAPTER_CLEAR_TX_DESC				pfnClearTxDesc;
    PFN_ADAPTER_GET_LINK_SPEED              pfnGetLinkSpeed;
    PFN_ADAPTER_GET_LINK_STATE              pfnGetLinkState;
    PFN_ADAPTER_IS_LINK_INITIALIZING        pfnIsLinkInitializing;
    PFN_ADAPTER_RESET_PHY_INIT_STATE		pfnResetPhyInitState;
    PFN_ADAPTER_GET_TRANSMIT_QUEUE_SIZE     pfnGetTransmitQueueSize;
    PFN_ADAPTER_GET_RECEIVE_QUEUE_SIZE      pfnGetReceiveQueueSize;
    PFN_ADAPTER_GET_STATISTICS              pfnGetStatistics;
    PFN_ADAPTER_GET_POWER_CAPS              pfnGetPowerCaps;
    PFN_ADAPTER_GET_POWER_STATE             pfnGetPowerState;
    PFN_ADAPTER_SET_POWER_STATE             pfnSetPowerState;
    PFN_ADAPTER_GET_PACKET_FILTER_CAPS      pfnGetPacketFilterCaps;
    PFN_ADAPTER_SET_PACKET_FILTER           pfnSetPacketFilter;
    PFN_ADAPTER_SET_WAKE_UP_PATTERN         pfnSetWakeUpPattern;
    PFN_ADAPTER_ENABLE_WAKE_UP_PATTERN      pfnEnableWakeUpPattern;
    PFN_SET_NODE_ADDRESS                    pfnSetNodeAddress;
    PFN_GET_NODE_ADDRESS                    pfnGetNodeAddress;
    PFN_GET_ADAPTER_INFO			        pfnGetAdapterInfo;
	PFN_ADAPTER_SET_SPPED_DUPLEX			pfnSetSpeedDuplex;
    PFN_ADAPTER_READ_PHY					pfnReadPhy;
    PFN_ADAPTER_WRITE_PHY					pfnWritePhy;
	PFN_REGISTER_OFFLOAD					pfnRegisterOffload;
	PFN_DEREGISTER_OFFLOAD					pfnDeRegisterOffload;
    PFN_RX_BUFF_READY						pfnRxBuffReady;
}   ADAPTER_API, *PADAPTER_API;
//////////////////////////////////////////////////////////////////

#define MAX_PACKET_TO_ACCUMULATE	16

typedef struct _ADAPTER_OPEN_PARAMS
{
	PVOID pOSApi; //pointer to OSAPI structure passed from higher layer
	PVOID pvHardwareBaseAddress; //memory mapped address passed from higher layer
	ULONG ulPollInterval; //poll interval in micro seconds. Used in polling mode
	ULONG MaxDpcLoop; //Maximum number of times we loop to in function ADAPTER_HandleInterrupt
	ULONG MaxRxPkt; //Maximum number of packet we process each time in function UpdateReceiveDescRingData
	ULONG MaxTxPkt; //Maximum number of packet we process each time in function UpdateTransmitDescRingData
	ULONG MaxRxPktToAccumulate; //maximum number of rx packet we accumulate in UpdateReceiveDescRingData before
								//indicating packets to OS.
	ULONG SentPacketStatusSuccess; //Status returned from adapter layer to higher layer when packet was sent successfully
	ULONG SentPacketStatusFailure; ////Status returned from adapter layer to higher layer when packet send was unsuccessful
	ULONG SetForcedModeEveryNthRxPacket; //NOT USED: For experiment with descriptor based interrupt
	ULONG SetForcedModeEveryNthTxPacket; //NOT USED: For experiment with descriptor based interrupt
	ULONG RxForcedInterrupt; //NOT USED: For experiment with descriptor based interrupt
	ULONG TxForcedInterrupt; //NOT USED: For experiment with descriptor based interrupt
	ULONG DeviceId; //Of MAC
	ULONG PollIntervalInusForThroughputMode; //Of MAC
}ADAPTER_OPEN_PARAMS, *PADAPTER_OPEN_PARAMS;

//////////////////////////////////////////////////////////////////
// This is the one function in the adapter interface that is publicly
// available. The rest of the interface is returned in the pAdapterApi.
// The first argument needs to be cast to a OSAPI structure pointer.
// The second argument should be cast to a ADPATER_API structure pointer.
int ADAPTER_Open (PADAPTER_OPEN_PARAMS pAdapterOpenParams, PVOID *pvpAdapterApi, ULONG *pulPhyAddr);

//////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////
// Here are the error codes the adapter function calls return.
#define ADAPTERERR_NONE                             0x0000
#define ADAPTERERR_COULD_NOT_ALLOC_CONTEXT          0x0001
#define ADAPTERERR_COULD_NOT_CREATE_CONTEXT         0x0002
#define ADAPTERERR_COULD_NOT_OPEN_PHY               0x0003
#define ADAPTERERR_TRANSMIT_QUEUE_FULL              0x0004
#define ADAPTERERR_COULD_NOT_INIT_PHY               0x0005
#define ADAPTERERR_PHYS_SIZE_SMALL					0x0006
//////////////////////////////////////////////////////////////////

#define REDUCE_LENGTH_BY 48
#define EXTRA_WRITE_SLOT_TO_REDUCE_PER_SEND	3
#define MAX_TX_DESCS                    256 //32 //256 //512 //64 //256 

typedef struct _TX_INFO_ADAP
{
	ULONG NoOfDesc; 
	PVOID pvVar2; 
}TX_INFO_ADAP, *PTX_INFO_ADAP;

#define WORKAROUND_FOR_MCP3_TX_STALL

#ifdef WORKAROUND_FOR_MCP3_TX_STALL
int ADAPTER_WorkaroundTXHang(PVOID pvContext);
#endif

//#define TRACK_INIT_TIME

#ifdef TRACK_INIT_TIME
//This routine is defined in entry.c adapter doesn't link int64.lib
//We defined here so that its easy to use it in phy as well as mswin

#define MAX_PRINT_INDEX		32
extern void PrintTime(ULONG ulIndex);
#define PRINT_INIT_TIME(_a) PrintTime((_a))
#else
#define PRINT_INIT_TIME(_a)
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _ADAPTER_H_
