/* header for low-level drivers */

/* $Author: zimoch $ */ 
/* $Date: 2009/12/10 10:02:53 $ */ 
/* $Id: regDev.h,v 1.3 2009/12/10 10:02:53 zimoch Exp $ */  
/* $Name:  $ */ 
/* $Revision: 1.3 $ */ 

#ifndef regDev_h
#define regDev_h

#include <dbScan.h>
#include <epicsVersion.h>

#ifdef BASE_VERSION
#define EPICS_3_13
#else
#define EPICS_3_14
#include <epicsExport.h>
#include <iocsh.h>
#endif

/* Every device driver may define struct regDevice as needed */
/* It's a handle to the device instance */
typedef struct regDevice regDevice;

/* Every driver must provide this function table */
/* It may be constant and is used for all device instances */
/* Unimplemented functions may be NULL */

typedef struct regDevSupport {
    void (*report)(
        regDevice *device,
        int level);
    
    IOSCANPVT (*getInScanPvt)(
        regDevice *device,
        unsigned int offset);
    
    IOSCANPVT (*getOutScanPvt)(
        regDevice *device,
        unsigned int offset);

    int (*read)(
        regDevice *device,
        unsigned int offset,
        unsigned int datalength,
        unsigned int nelem,
        void* pdata,
        int priority);
    
    int (*write)(
        regDevice *device,
        unsigned int offset,
        unsigned int datalength,
        unsigned int nelem,
        void* pdata,
        void* pmask,
        int priority);
        
} regDevSupport;

/* Every driver must create and register each device instance */
/* together with name and function table */
int regDevRegisterDevice(
    const char* name,
    const regDevSupport* support,
    regDevice* device);

regDevice* regDevFind(
    const char* name);

extern int regDevDebug;
#define regDevDebugLog(level, fmt, args...) \
    if (level <= regDevDebug)  errlogSevPrintf(errlogInfo, fmt, ## args);

/* utility function for drivers to copy buffers */
void regDevCopy(unsigned int dlen, unsigned int nelem, void* src, void* dest, void* pmask, int swap);
#endif /* regDev_h */
