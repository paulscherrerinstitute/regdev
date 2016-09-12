/* header for low-level drivers */

#ifndef regDev_h
#define regDev_h

#include <dbScan.h>
#include <epicsVersion.h>

#ifdef BASE_VERSION
#define EPICS_3_13
#else
#define EPICS_3_14
#include <iocsh.h>
#include "shareLib.h"
#endif

/* vxWorks 5 does not have strdup */
#if defined(vxWorks) && !defined(_WRS_VXWORKS_MAJOR)
#define strdup(s) ({ char* __r=(char*)malloc(strlen(s)+1); __r ? strcpy(__r, s) : NULL; })
#endif

/* utility: size_t modifier for printf. Example usage: printf("%"Z"x", size) */
#if defined __GNUC__ && __GNUC__ >= 3
#define Z "z"
#elif defined _WIN32
#define Z "I"
#else
#define Z
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

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*regDevTransferComplete) (const char* user, int status);

typedef struct regDevSupport {
    void (*report)(
        regDevice *device,
        int level);

    IOSCANPVT (*getInScanPvt)(
        regDevice *device,
        size_t offset,
        int intvec,
        const char* user);

    IOSCANPVT (*getOutScanPvt)(
        regDevice *device,
        size_t offset,
        int intvec,
        const char* user);

    int (*read)(
        regDevice *device,
        size_t offset,
        unsigned int dlen,
        size_t nelem,
        void* pdata,
        int priority,
        regDevTransferComplete callback,
        const char* user);

    int (*write)(
        regDevice *device,
        size_t offset,
        unsigned int dlen,
        size_t nelem,
        void* pdata,
        void* pmask,
        int priority,
        regDevTransferComplete callback,
        const char* user);

} regDevSupport;

/* Every driver must create and register each device instance
 * together with name and function table.
 */
epicsShareFunc int regDevRegisterDevice(
    const char* name,
    const regDevSupport* support,
    regDevice* device,
    size_t size);

/* find the device instance by its name */
epicsShareFunc regDevice* regDevFind(
    const char* name);

/* get device name by handle */
epicsShareFunc const char* regDevName(
    regDevice* device);

/* lock/unlock access to the device (for asynchronous work threads) */
epicsShareFunc int regDevLock(
    regDevice* device);

epicsShareFunc int regDevUnlock(
    regDevice* device);

/* A driver can call regDevInstallWorkQueue to serialize all read/write requests.
 * Once it is installed, record processing is asynchronous but the driver read and
 * write functions are synrchonously called in a separate thread (with callback=NULL).
 * One thread per driver and prioriy level is created to handle the reads and writes.
 * The queue has space for maxEntries pending requests on each of the 3 priority levels.
 */
epicsShareFunc int regDevInstallWorkQueue(
    regDevice* device,
    unsigned int maxEntries);

/*
A driver can call regDevRegisterDmaAlloc to register an allocator for DMA enabled memory.
The allocator shall work similar to realloc (ptr=NULL: alloc, size=0: free, otherwise: resize)
but need not copy nor initialize any content.
If ptr has not previously been allocated by the allocator, it shall be treated as NULL.
The function shall return NULL on failure.
It is used for the array buffer by aai and aao records.
*/
epicsShareFunc int regDevRegisterDmaAlloc(
    regDevice* device,
    void* (*dmaAlloc) (regDevice *device, void* ptr, size_t size));

#define REGDEV_BLOCK_READ 1
#define REGDEV_BLOCK_WRITE 2
epicsShareFunc int regDevMakeBlockdevice(
    regDevice* device,
    unsigned int modes, /* any of REGDEV_BLOCK_READ | REGDEV_BLOCK_WRITE */
    int swap,           /* any of REGDEV*SWAP* below */
    void* buffer);      /* NULL or buffer space provided by the driver */

/* Use this variable to control debugging messages */
epicsShareExtern int regDevDebug;

#define REGDEV_DBG_INIT 1
#define REGDEV_DBG_IN   2
#define REGDEV_DBG_OUT  4

/* for backward compatibility: */
#define DBG_INIT REGDEV_DBG_INIT
#define DBG_IN   REGDEV_DBG_IN
#define DBG_OUT  REGDEV_DBG_OUT

#if defined __GNUC__ && __GNUC__ < 3
/* old GCC style */
 #define _CURRENT_FUNCTION_ __FUNCTION__
 #define regDevDebugLog(level, fmt, args...) \
    do {if ((level) & regDevDebug) printf("%s " fmt, _CURRENT_FUNCTION_ , ## args);} while(0)
#else
/* new posix style */
 #if defined(__GNUC__) || (defined(__MWERKS__) && (__MWERKS__ >= 0x3000)) || (defined(__ICC) && (__ICC >= 600)) || defined(__ghs__)
  #define _CURRENT_FUNCTION_ __PRETTY_FUNCTION__
 #elif defined(__DMC__) && (__DMC__ >= 0x810)
  #define _CURRENT_FUNCTION_ __PRETTY_FUNCTION__
 #elif defined(__FUNCSIG__)
  #define _CURRENT_FUNCTION_ __FUNCSIG__
 #elif (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 600)) || (defined(__IBMCPP__) && (__IBMCPP__ >= 500))
  #define _CURRENT_FUNCTION_ __FUNCTION__
 #elif defined(__BORLANDC__) && (__BORLANDC__ >= 0x550)
  #define _CURRENT_FUNCTION_ __FUNC__
 #elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901)
  #define _CURRENT_FUNCTION_ __func__
 #elif defined(__cplusplus) && (__cplusplus >= 201103)
  #define _CURRENT_FUNCTION_ __func__
 #else
  #define LINETOSTR(L) LINETOSTR2(L)
  #define LINETOSTR2(L) #L
  #define _CURRENT_FUNCTION_ __FILE__ ":" LINETOSTR(__LINE__)
 #endif
 #define regDevDebugLog(level, fmt, ...) \
    do {if ((level) & regDevDebug) printf("%s " fmt, _CURRENT_FUNCTION_ , ## __VA_ARGS__);} while(0)
#endif

/* utility function for drivers to copy buffers with correct data size, swapping support, and optional bit mask */
#define REGDEV_NO_SWAP      0              /* never swap */
#define REGDEV_DO_SWAP      1              /* always swap */
#define REGDEV_BE_SWAP      2              /* swap only on big endian systems */
#define REGDEV_LE_SWAP      3              /* swap only on little endian systems */
#define REGDEV_SWAP_TO_LE   REGDEV_BE_SWAP /* swap host byte order to little endian if necessary */  
#define REGDEV_SWAP_TO_BE   REGDEV_LE_SWAP /* swap host byte order to big endian if necessary */     
#define REGDEV_SWAP_FROM_LE REGDEV_BE_SWAP /* swap from little endian to host byte order if necessary */
#define REGDEV_SWAP_FROM_BE REGDEV_LE_SWAP /* swap from big endian to host byte order if necessary */
/* for backward compatibility: */
#define NO_SWAP REGDEV_NO_SWAP
#define DO_SWAP REGDEV_DO_SWAP
#define BE_SWAP REGDEV_BE_SWAP
#define LE_SWAP REGDEV_LE_SWAP
epicsShareFunc  void regDevCopy(unsigned int dlen, size_t nelem, const volatile void* src, volatile void* dest, const void* pmask, int swap);
#endif /* regDev_h */

#ifdef __cplusplus
}
#endif
