/*
    FILE:   phy.h
    DATE:   2/7/00

    This file contains the functional interface to the PHY.
*/
#ifndef _PHY_H_
#define _PHY_H_

#ifdef __cplusplus
extern "C" {
#endif

#define HOMEPNA_PHY_ADDRESS	0
#define DEFAULT_PHY_ADDRESS   1

#define AMD_HPNA_PHY_ID		0x6b94
#define LU_HPNA_PHY_ID		0xE0180000
#define CONX_HPNA_PHY_ID	0x0032CC00

#define HDP_VERSION_STRING "HDR P: $Revision: #23 $"

/////////////////////////////////////////////////////////////////////////
// The phy module knows the values that need to go into the phy registers
// but typically the method of writing those registers is controlled by
// another module (usually the adapter because it is really the hardware
// interface.) Hence, the phy needs routines to call to read and write the
// phy registers. This structure with appropriate routines will be provided
// in the PHY_Open call.

typedef int (* PFN_READ_PHY)  (PVOID pvData, ULONG ulPhyAddr, ULONG ulPhyReg, ULONG *pulValue);
typedef int (* PFN_WRITE_PHY) (PVOID pvData, ULONG ulPhyAddr, ULONG ulPhyReg, ULONG ulValue);

typedef struct  PHY_SUPPORT_API
{
    PVOID           pADCX;
    PFN_READ_PHY    pfnRead;
    PFN_WRITE_PHY   pfnWrite;
}   PHY_SUPPORT_API, *PPHY_SUPPORT_API;
/////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////
// The functional typedefs for the PHY Api
typedef int (* PFN_PHY_INIT) (PVOID pvContext, ULONG *pulLinkState);
typedef int (* PFN_PHY_DEINIT) (PVOID pvContext);
typedef int (* PFN_PHY_CLOSE) (PVOID pvContext);
typedef int (* PFN_GET_LINK_SPEED) (PVOID pvContext);
typedef int (* PFN_GET_LINK_MODE) (PVOID pvContext);
typedef int (* PFN_GET_LINK_STATE) (PVOID pvContext, ULONG *pulLinkState);
typedef int (* PFN_IS_LINK_INITIALIZING) (PVOID pvContext);
typedef int (* PFN_RESET_PHY_INIT_STATE) (PVOID pvContext);
typedef int (* PFN_FORCE_SPEED_DUPLEX) (PVOID pvContext, USHORT usSpeed, UCHAR ucForceDpx, UCHAR ucForceMode);
typedef int (* PFN_PHY_POWERDOWN) (PVOID pvContext);

typedef struct  _PHY_API
{
    // This is the context to pass back in as the first arg on all
    // the calls in the API below.
    PVOID               pPHYCX;

    PFN_PHY_INIT				pfnInit;
    PFN_PHY_INIT				pfnInitFast;
    PFN_PHY_DEINIT				pfnDeinit;
    PFN_PHY_CLOSE				pfnClose;
    PFN_GET_LINK_SPEED			pfnGetLinkSpeed;
    PFN_GET_LINK_MODE			pfnGetLinkMode;
    PFN_GET_LINK_STATE			pfnGetLinkState;
    PFN_IS_LINK_INITIALIZING	pfnIsLinkInitializing;
    PFN_RESET_PHY_INIT_STATE	pfnResetPhyInitState;
    PFN_FORCE_SPEED_DUPLEX		pfnForceSpeedDuplex;
    PFN_PHY_POWERDOWN			pfnPowerdown;
}   PHY_API, *PPHY_API;
/////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////
// This is the one function in the PHY interface that is publicly
// available. The rest of the interface is returned in the pPhyApi;
// The first argument needs to be cast to a POS_API structure ptr.
// On input the second argument is a ptr to a PPHY_SUPPORT_API.
// On output, the second argument should be treated as a ptr to a
// PPHY_API and set appropriately.
extern int PHY_Open (PVOID pvOSApi, PVOID pPhyApi, ULONG *pulIsHPNAPhy, ULONG *pulPhyAddr, ULONG *pulPhyConnected);
/////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////
// Here are the error codes the phy functions can return.
#define PHYERR_NONE                             0x0000
#define PHYERR_COULD_NOT_ALLOC_CONTEXT          0x0001
#define PHYERR_RESET_NEVER_FINISHED             0x0002
#define PHYERR_NO_AVAILABLE_LINK_SPEED          0x0004
#define PHYERR_INVALID_SETTINGS					0x0005
#define PHYERR_READ_FAILED						0x0006
#define PHYERR_WRITE_FAILED						0x0007
#define PHYERR_NO_PHY							0x0008
#define PHYERR_NO_RESOURCE						0x0009

#define PHY_INVALID_PHY_ADDR					0xFFFF;

/////////////////////////////////////////////////////////////////////////


#ifdef AMDHPNA10
/////////////////////////////////////////////////////////////////////////
// Here are the Phy Timer related structure. (For AMD HPNA)
typedef void (* PFN_PHY_TIMER) (PVOID pvData, ULONG macActive);

typedef struct  PHY_TIMER_API
{
    PVOID           pPhyCX;
    ULONG           ulPeriod;    // unit of MilliSeconds
    PFN_PHY_TIMER   pfnPhyTimer;
}   PHY_TIMER_API, *PPHY_TIMER_API;
/////////////////////////////////////////////////////////////////////////
#endif

// This value can be used in the ulPhyLinkSpeed field.
#define PHY_LINK_SPEED_UNKNOWN          0x0FFFFFFFF

typedef void (* PTIMER_FUNC) (PVOID pvContext);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //_PHY_H_
