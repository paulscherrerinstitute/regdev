/* header for low-level drivers */

/* $Author: zimoch $ */
/* $Date: 2014/02/18 16:09:18 $ */
/* $Id: regDev.h,v 1.18 2014/02/18 16:09:18 zimoch Exp $ */
/* $Name:  $ */
/* $Revision: 1.18 $ */

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
#include <iocsh.h>
#include <epicsExport.h>
#endif

/* vxWorks 5 does not have strdup */
#if defined(vxWorks) && !defined(_WRS_VXWORKS_MAJOR)
#include <string.h>
#include <stdlib.h>
extern __inline char* regDevStrdup(const char* s)
{
    char* r = malloc(strlen(s)+1);
    if (!r) return NULL;
    return strcpy(r, s);
}
#define strdup(s) regDevStrdup(s)
#endif

/* utility: size_t modifier for printf */
#if defined(vxWorks) && !defined(_WRS_VXWORKS_MAJOR)
#define Z ""
#else
#define Z "z"
#endif

/* Every device driver may define struct regDevice as needed
 * It's a handle to the device instance
 */
typedef struct regDevice regDevice;

/* Every driver must provide this function table
 * It may be constant and is used for all device instances
 * Unimplemented functions may be NULL
 *
 * Unless regDevRegisterDevice is called with a size of 0, offset+nelem*dlen will never be outside the valid range.
 * 
 * Synchronous read/write returns 0 on success or other values (but not 1) on failure.
 * Each call is protected with a per device mutex, thus no two calls to the same device will happen at the same time.
 *
 * Asynchronous read/write returns ASYNC_COMPLETION (=1) and arranges to call
 * callback(user, status) when the operation has finished (or failed).
 * The the background task may use regDevLock()/regDevUnlock() to protect access to the device.
 * Read and write functions may use priority (0=low, 1=medium, 2=high) to sort requests.
 * 
 * A driver can choose at each call to work synchronously or asynchronously.
 * The user argument can be used to print messages. (It contains the record name).
 *
 * A driver may call regDevInstallWorkQueue at initialization to leave asynchonous handling to regDev.
 * Once a work queue is installed, all read/write calls are executed in a separate thread which may block.
 * In that case callback will allways be NULL.
 *
 */
 
/* return states for driver functions */
#define SUCCESS 0
#define ASYNC_COMPLETION 1

typedef void (*regDevTransferComplete) (char* user, int status);

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
        int priority,
        regDevTransferComplete callback,
        char* user);

    int (*write)(
        regDevice *device,
        size_t offset,
        unsigned int dlen,
        size_t nelem,
        void* pdata,
        void* pmask,
        int priority,
        regDevTransferComplete callback,
        char* user);

} regDevSupport;

/* Every driver must create and register each device instance
 * together with name and function table.
 */
int regDevRegisterDevice(
    const char* name,
    const regDevSupport* support,
    regDevice* device,
    size_t size);

/* find the device instance by its name */
regDevice* regDevFind(
    const char* name);

/* lock/unlock access to the device (for asynchronous work threads) */
int regDevLock(
    regDevice* device);
    
const char* regDevUnock(
    regDevice* device);

/* A driver can call regDevInstallWorkQueue to serialize all read/write requests.
 * Once it is installed, record processing is asynchronous but the driver read and
 * write functions are synrchonous in a separate thread (with callback=NULL).
 * One thread per driver and prioriy level is created to handle the reads and writes.
 * The queue has space for maxEntries pending requests on each of the 3 priority levels.
 */
int regDevInstallWorkQueue(
    regDevice* device,
    size_t maxEntries);

/*
A driver can call regDevRegisterDmaAlloc to register an allocator for DMA enabled memory.
The allocator shall work similar to realloc (ptr=NULL: alloc, size=0: free, otherwise: resize)
but need not copy nor initialize any content.
If ptr has not previously been allocated by the allocator, it shall be treated as NULL.
The function shall return NULL on failure.
It is used for the array buffer by aai and aao records.
*/
int regDevRegisterDmaAlloc(
    regDevice* device,
    void* (*dmaAlloc) (regDevice *device, void* ptr, size_t size));

/* Use this variable to control debugging messages */
extern int regDevDebug;

#define DBG_INIT 1
#define DBG_IN   2
#define DBG_OUT  4

#if defined __GNUC__ && __GNUC__ < 3
#define regDevDebugLog(level, fmt, args...) \
    do {if ((level) & regDevDebug) printf(fmt, ## args);} while(0)
#else
#define regDevDebugLog(level, fmt, ...) \
    do {if ((level) & regDevDebug) printf(fmt, ## __VA_ARGS__);} while(0)
#endif

/* utility function for drivers to copy buffers */
void regDevCopy(unsigned int dlen, size_t nelem, volatile void* src, volatile void* dest, void* pmask, int swap);
#endif /* regDev_h */

