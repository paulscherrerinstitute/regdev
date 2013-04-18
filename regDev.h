/* header for low-level drivers */

/* $Author: zimoch $ */ 
/* $Date: 2013/04/18 15:54:56 $ */ 
/* $Id: regDev.h,v 1.14 2013/04/18 15:54:56 zimoch Exp $ */  
/* $Name:  $ */ 
/* $Revision: 1.14 $ */ 

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

/* Every sync driver must provide this function table */
/* It may be constant and is used for all device instances */
/* Unimplemented functions may be NULL */

typedef struct regDevSupport {
    void (*report)(
        regDevice *device,
        int level);
    
    IOSCANPVT (*getInScanPvt)(
        regDevice *device,
        size_t offset);
    
    IOSCANPVT (*getOutScanPvt)(
        regDevice *device,
        size_t offset);

    int (*read)(
        regDevice *device,
        size_t offset,
        unsigned int dlen,
        size_t nelem,
        void* pdata,
        int priority);
    
    int (*write)(
        regDevice *device,
        size_t offset,
        unsigned int dlen,
        size_t nelem,
        void* pdata,
        void* pmask,
        int priority);
        
} regDevSupport;

/* Every sync driver must create and register each device instance */
/* together with name and function table */
int regDevRegisterDevice(
    const char* name,
    const regDevSupport* support,
    regDevice* device);

regDevice* regDevFind(
    const char* name);

/* Every async driver must provide this function table */
/* It may be constant and is used for all device instances */
/* Unimplemented functions may be NULL */

/** 
Here we have to add an "init" routine to the regDevAsyncSupport,
This will be then called at record initialization routine if it is provided.
We can the use this to pass a pointer to memory which is suitable for DMA.
**/

typedef struct regDevAsyncSupport {
    void (*report)(
        regDevice *device,
        int level);
    
    IOSCANPVT (*getInScanPvt)(
        regDevice *device,
        size_t offset);
    
    IOSCANPVT (*getOutScanPvt)(
        regDevice *device,
        size_t offset);

    int (*read)(
        regDevice *device,
        size_t offset,
        unsigned int dlen,
        size_t nelem,
        void* pdata,
	CALLBACK* cbStruct,
        int priority,
	int* status);
    
    int (*write)(
        regDevice *device,
        size_t offset,
        unsigned int dlen,
        size_t nelem,
        void* pdata,
	CALLBACK* cbStruct,
        void* pmask,
        int priority,
	int* status);
	
    int (*buff_alloc)(
       	void** usrBufPtr,
       	volatile void** busBufPtr, 
    	size_t size);  
        
} regDevAsyncSupport;

/* Every async driver must create and register each device instance */
/* together with name and function table */
int regDevAsyncRegisterDevice(
    const char* name,
    const regDevAsyncSupport* support,
    regDevice* device);

/* For backward compatibility. Don't use any more */
typedef struct regDeviceAsyn regDeviceAsyn;

regDevice* regDevAsynFind(
    const char* name);

extern int regDevDebug;
#define regDevDebugLog(level, fmt, args...) \
    do {if ((level) & regDevDebug) errlogSevPrintf(errlogInfo, fmt, ## args);} while(0)
#define DBG_INIT 1
#define DBG_IN   2
#define DBG_OUT  4


/* utility function for drivers to copy buffers */
void regDevCopy(unsigned int dlen, size_t nelem, volatile void* src, volatile void* dest, void* pmask, int swap);
#endif /* regDev_h */

/* utility: size_t modifier for printf */
#ifdef __vxworks
#define Z ""
#else
#define Z "z"
#endif

/* utility macro to check offset */
#define regDevCheckOffset(function, name, offset, dlen, nelm, size) \
    do { \
        if (offset > size) \
        { \
            errlogSevPrintf(errlogMajor, \
                "%s %s: offset %"Z"u out of range (0-%"Z"u)\n", \
                function, name, offset, size); \
            return ERROR; \
        } \
        if (offset+dlen*nelem > size) \
        { \
            errlogSevPrintf(errlogMajor, \
                "%s %s: offset %"Z"u + %"Z"u bytes length exceeds mapped size %"Z"u by %"Z"u bytes\n", \
                function, name, offset, nelem, size, offset+dlen*nelem - size); \
            return ERROR; \
        }\
    } while(0)


