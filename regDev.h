/* header for low-level drivers */

/* $Author: zimoch $ */
/* $Date: 2014/06/18 13:59:34 $ */
/* $Id: regDev.h,v 1.22 2014/06/18 13:59:34 zimoch Exp $ */
/* $Name:  $ */
/* $Revision: 1.22 $ */

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
#define strdup(s) ({ char* __r=malloc(strlen(s)+1); __r ? strcpy(__r, s) : NULL; })
#endif

/* utility: size_t modifier for printf. Example usage: printf("%"Z"x", s) */
#if defined(vxWorks) && !defined(_WRS_VXWORKS_MAJOR)
/* vxWorks 5 does not know the z modifier */
#define Z ""
#else
#define Z "z"
#endif

/* Every device driver may define struct regDevice as needed
 * It's a handle to the device instance
 */
typedef struct regDevice regDevice;

/* return states for driver functions */
#define SUCCESS 0
#define ASYNC_COMPLETION 1

/* Every driver must provide a regDevSupport table.
 * It may be constant and is used for all device instances.
 * The functions from this table are called from the device support.
 * Unimplemented functions may be NULL.
 *
 * Unless regDevRegisterDevice is called with a size of 0,
 * offset+nelem*dlen will never be larger than size in any call of any
 * function in this table. Thus the driver normally does not need to
 * check if the range is valid.
 * 
 * Synchronous read/write calls shall return 0 on success or other values
 * (but not 1) on failure. They can safely ignore the callback argument.
 *
 * The calls are thread-safe as they are protected with a device specific
 * mutex. Thus a synchronous driver normally does not need to care about
 * concurency.
 *
 * Asynchronous read/write shall return ASYNC_COMPLETION (1) and arrange
 * to call the provided callback when the operation has finished or failed
 * and give back the passed user argument as well as the status (0 on success).
 *
 * Typically an asynchronous driver will create a work thread. In this thread
 * it shall use regDevLock()/regDevUnlock() to protect access to the device
 * from concurrent access by other calls.
 *
 * Read and write functions may use priority (0=low, 1=medium, 2=high) to sort
 * requests.
 * 
 * A driver can choose at each call to work synchronously or asynchronously.
 * The user argument can be used to print messages. (It point to the record name).
 *
 * A driver may call regDevInstallWorkQueue at initialization to leave asynchonous
 * handling to regDev. Once a work queue is installed, all read/write calls are
 * executed in priority order and are allowed to bock.
 * In that case the callback argument will allways be NULL and the driver function
 * shall not return ASYNC_COMPLETION (1).
 *
 */
 
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
 * write functions are synrchonously called in a separate thread (with callback=NULL).
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
#define NO_SWAP 0 /* never swap */
#define DO_SWAP 1 /* always swap */
#define BE_SWAP 2 /* swap only on big endian systems */
#define LE_SWAP 3 /* swap only on little endian systems */
void regDevCopy(unsigned int dlen, size_t nelem, const volatile void* src, volatile void* dest, void* pmask, int swap);
#endif /* regDev_h */

