/*
    FILE:   basetype.h
    DATE:   2/7/00

    This file contains the basetypes used by the network driver code.
*/

#ifndef _BASETYPE_H_
#define _BASETYPE_H_

#define HDB_VERSION_STRING "HDR B: $Revision: #4 $"

// Fundamental data types
// UCHAR    8 bit unsigned
// UINT     either 16bit or 32bit unsigned depending upon compiler
// USHORT   16 bit unsigned
// ULONG    32 bit unsigned

typedef unsigned char   UCHAR;
typedef unsigned int    UINT;
typedef unsigned short  USHORT;
typedef unsigned long   ULONG;
#define VOID            void

// Constructed types
typedef VOID            *PVOID;

// These are USEFUL "types"
#ifndef NULL
#define NULL            0
#endif

#ifndef TRUE
#define TRUE            1
#endif

#ifndef FALSE
#define FALSE           0
#endif

#endif // _BASETYPE_H_
