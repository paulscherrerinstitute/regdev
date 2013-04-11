/* header for low-level drivers */

/* $Author: zimoch $ */ 
/* $Date: 2013/04/11 12:25:01 $ */ 
/* $Id: regDev.h,v 1.8 2013/04/11 12:25:01 zimoch Exp $ */  
/* $Name:  $ */ 
/* $Revision: 1.8 $ */ 

#ifndef regDev_h
#define regDev_h

#include <dbScan.h>
#include <callback.h>
#include <epicsVersion.h>
#include <errlog.h>

#ifdef BASE_VERSION
#define EPICS_3_13
#else
#define EPICS_3_14
#include <epicsExport.h>
#include <iocsh.h>
#endif

/* return states for driver functions */
#ifndef OK
#define OK 0
#endif
#ifndef ERROR
#define ERROR -1
#endif
#define ASYNC_COMPLETITION 1

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

/* Every device driver may define struct regDeviceAsyn as needed */
/* It's a handle to the device instance */

/* Every driver must provide this function table */
/* It may be constant and is used for all device instances */
/* Unimplemented functions may be NULL */

/** 
Here we have to add an "init" routine to the regDevAsyncSupport,
This will be then called at record initialization routine if it is provided.
We can the use this to pass a pointer to the (kernel) allocated memory which is 
suitable for DMA. In this routine we have to call pev_buf_dma().
**/

/* for backward compatibility */
#define regDeviceAsyn regDevice

typedef struct regDevAsyncSupport {
    void (*report)(
        regDevice *device,
        int level);
    
    IOSCANPVT (*getInScanPvt)(
        regDevice *device,
        unsigned int offset);
    
    IOSCANPVT (*getOutScanPvt)(
        regDeviceAsyn *device,
        unsigned int offset);

    int (*read)(
        regDevice *device,
        unsigned int offset,
        unsigned int datalength,
        unsigned int nelem,
        void* pdata,
	CALLBACK* cbStruct,
        int priority,
	int* status);
    
    int (*write)(
        regDevice *device,
        unsigned int offset,
        unsigned int datalength,
        unsigned int nelem,
        void* pdata,
	CALLBACK* cbStruct,
        void* pmask,
        int priority,
	int* status);
	
    int (*buff_alloc)(
       	void** usrBufPtr, 
       	void** busBufPtr, 
    	unsigned int size);  
        
} regDevAsyncSupport;

/* Every driver must create and register each device instance */
/* together with name and function table */
int regDevAsyncRegisterDevice(
    const char* name,
    const regDevAsyncSupport* support,
    regDeviceAsyn* device);

regDeviceAsyn* regDevAsynFind(
    const char* name);

extern int regDevDebug;
#define regDevDebugLog(level, fmt, args...) \
    do {if (level & regDevDebug) errlogSevPrintf(errlogInfo, fmt, ## args);} while(0)
#define DBG_INIT 1
#define DBG_IN   2
#define DBG_OUT  4


/* utility function for drivers to copy buffers */
void regDevCopy(unsigned int dlen, unsigned int nelem, volatile void* src, volatile void* dest, void* pmask, int swap);
#endif /* regDev_h */
