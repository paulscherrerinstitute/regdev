#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <devLib.h>
#include <regDev.h>

#ifdef __unix
#warning Compiling for Unix/Linux
#include <sys/mman.h>
#define HAVE_MMAP
#endif

#ifdef __vxworks
#warning Compiling for vxWorks
#include <sysLib.h>
#include <intLib.h>
#include <vme.h>
#include <iv.h>
#define HAVE_VME
#endif

#ifdef EPICS_3_14
#include <epicsExit.h>
#include <epicsMutex.h>
#include <epicsExport.h>
#define MUTEX epicsMutexId
#define MUTEX_CREATE() epicsMutexCreate()
#define LOCK(m) epicsMutexLock(m)
#define UNLOCK(m) epicsMutexUnlock(m)
#else
#include <semLib.h>
#define MUTEX SEM_ID
#define MUTEX_CREATE() semMCreate(SEM_Q_FIFO)
#define LOCK(m) semTake(m, WAIT_FOREVER)
#define UNLOCK(m) semGive(m)
#endif

#define MAGIC 2661166104U /* crc("mmap") */

struct regDevice {
    unsigned long magic;
    MUTEX accessLock;
    char* localbaseaddress;
    int addrspace;
    unsigned long baseaddress;
    unsigned long size;
    unsigned int intrvector;
    unsigned int intrlevel;
    int (*intrhandler)(regDevice *device);
    void* userdata;
    IOSCANPVT ioscanpvt;
    int intrcount;
};

/******** Support functions *****************************/ 

void mmapReport(
    regDevice *device,
    int level)
{
    if (device && device->magic == MAGIC)
    {
        printf("mmap driver: regs @ %p - %p\n",
            device->localbaseaddress, device->localbaseaddress+device->size-1);
        if (level > 1)
        {
            printf("       Interrupt count: %d\n", device->intrcount);
        }
    }
}

IOSCANPVT mmapGetInScanPvt(
    regDevice *device,
    unsigned int offset)
{
    if (!device || device->magic != MAGIC)
    { 
        errlogSevPrintf(errlogMajor,
            "mmapGetInScanPvt: illegal device handle\n");
        return NULL;
    }
    return device->ioscanpvt;
}

#define mmapGetOutScanPvt mmapGetInScanPvt

int mmapRead(
    regDevice *device,
    unsigned int offset,
    unsigned int dlen,
    unsigned int nelem,
    void* pdata,
    int flags)
{
    int log;

    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "mmapRead: illegal device handle\n");
        return -1;
    }
    if (offset > device->size ||
        offset+dlen*(flags && REGDEV_FLAGS_FIFO ? 1 : nelem) > device->size)
    {
        errlogSevPrintf(errlogMajor,
            "mmapRead address out of range\n");
        return -1;
    }
    log = device->addrspace == -1 || regDevDebug >= 4;
    LOCK(device->accessLock);
    if (log) printf ("mmapRead %08lx:", device->baseaddress+offset);
    switch (dlen)
    {
        case 0:
            break;
        case 1:
        {
            unsigned char  x;
            unsigned char* s = (unsigned char*)(device->localbaseaddress+offset);
            unsigned char* d = pdata;
                
            while (nelem--)
            {
                x = *s;
                if (!(flags && REGDEV_FLAGS_FIFO)) s++;
                if (log) printf (" %02x", x);
                *d++ = x;
            }
            break;
        }
        case 2:
        {
            unsigned short  x;
            unsigned short* s = (unsigned short*)(device->localbaseaddress+offset);
            unsigned short* d = pdata;
                
            while (nelem--)
            {
                x = *s;
                if (!(flags && REGDEV_FLAGS_FIFO)) s++;
                if (log) printf (" %04x", x);
                *d++ = x;
            }
            break;
        }
        case 4:
        {
            unsigned long  x;
            unsigned long* s = (unsigned long*)(device->localbaseaddress+offset);
            unsigned long* d = pdata;
                
            while (nelem--)
            {
                x = *s;
                if (!(flags && REGDEV_FLAGS_FIFO)) s++;
                if (log) printf (" %08lx", x);
                *d++ = x;
            }
            break;
        }
        case 8:
        {
            unsigned long long  x;
            unsigned long long* s = (unsigned long long*)(device->localbaseaddress+offset);
            unsigned long long* d = pdata;
                
            while (nelem--)
            {
                x = *s;
                if (!(flags && REGDEV_FLAGS_FIFO)) s++;
                if (log) printf (" %016Lx", x);
                *d++ = x;
            }
            break;
        }
        default:
        {
            unsigned char  x;
            unsigned char* s = (unsigned char*)(device->localbaseaddress+offset);
            unsigned char* d = pdata;
            int i;
            
            while (nelem--)
            {
                if (log) printf (" ");
                if (flags && REGDEV_FLAGS_FIFO)
                    s = (unsigned char*)(device->localbaseaddress+offset);
                for (i=0; i<dlen; i++)
                {
                    x = *s++;
                    if (log) printf ("%02x", x);
                    *d++ = x;
                }
            }
        }
    }
    if (log) printf ("\n");
    UNLOCK(device->accessLock);
    return 0;
}

int mmapWrite(
    regDevice *device,
    unsigned int offset,
    unsigned int dlen,
    unsigned int nelem,
    void* pdata,
    void* pmask,
    int flags)
{
    int log;
    
    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "mmapWrite: illegal device handle\n");
        return -1;
    }
    if (offset > device->size ||
        offset+dlen*(flags && REGDEV_FLAGS_FIFO ? 1 : nelem) > device->size)
    {
        errlogSevPrintf(errlogMajor,
            "mmapWrite: address out of range\n");
        return -1;
    }
    if (!device || device->magic != MAGIC
        || offset+dlen*nelem > device->size) return -1;
    log = device->addrspace == -1 || regDevDebug >= 4;
    LOCK(device->accessLock);
    if (log) printf ("mmapWrite %08lx:", device->baseaddress+offset);
    switch (dlen)
    {
        case 0:
            break;
        case 1:
        {
            unsigned char  x;
            unsigned char* s = pdata;
            unsigned char* d = (unsigned char*)(device->localbaseaddress+offset);
            unsigned char  m = pmask ? *(unsigned char*)pmask : 0;
            while (nelem--)
            {
                x = *s++;
                if (pmask)
                {
                    x &= m;
                    x |= *d & ~m;
                }
                if (log) printf (" %02x", x);
                *d = x;
                if (!(flags && REGDEV_FLAGS_FIFO)) d++;
            }
            break;
        }
        case 2:
        {
            unsigned short  x;
            unsigned short* s = pdata;
            unsigned short* d = (unsigned short*)(device->localbaseaddress+offset);
            unsigned short  m = pmask ? *(unsigned short*)pmask : 0;
            while (nelem--)
            {
                x = *s++;
                if (pmask)
                {
                    x &= m;
                    x |= *d & ~m;
                }
                if (log) printf (" %04x", x);
                *d = x;
                if (!(flags && REGDEV_FLAGS_FIFO)) d++;
            }
            break;
        }
        case 4:
        {
            unsigned long  x;
            unsigned long* s = pdata;
            unsigned long* d = (unsigned long*)(device->localbaseaddress+offset);
            unsigned long  m = pmask ? *(unsigned long*)pmask : 0;
            while (nelem--)
            {
                x = *s++;
                if (pmask)
                {
                    x &= m;
                    x |= *d & ~m;
                }
                if (log) printf (" %08lx", x);
                *d = x;
                if (!(flags && REGDEV_FLAGS_FIFO)) d++;
            }
            break;
        }
        case 8:
        {
            unsigned long long  x;
            unsigned long long* s = pdata;
            unsigned long long* d = (unsigned long long*)(device->localbaseaddress+offset);
            unsigned long long  m = pmask ? *(unsigned long long*)pmask : 0;
            while (nelem--)
            {
                x = *s++;
                if (pmask)
                {
                    x &= m;
                    x |= *d & ~m;
                }
                if (log) printf (" %016Lx", x);
                *d = x;
                if (!(flags && REGDEV_FLAGS_FIFO)) d++;
            }
            break;
        }
        default:
        {
            unsigned char  x;
            unsigned char* s = pdata;
            unsigned char* d = (unsigned char*)(device->localbaseaddress+offset);
            unsigned char* m;
            int i;
            
            while (nelem--)
            {
                m = pmask;
                if (log) printf (" ");
                if (flags && REGDEV_FLAGS_FIFO)
                    d = (unsigned char*)(device->localbaseaddress+offset);
                for (i=0; i<dlen; i++)
                {
                    x = *s++;
                    if (pmask)
                    {
                        x &= *m;
                        x |= *d & ~*m;
                        m++;
                    }
                    if (log) printf ("%02x", x);
                    *d++ = x;
                }
            }
        }
    }
    if (log) printf ("\n");
    UNLOCK(device->accessLock);
    return 0;
}

static regDevSupport mmapSupport = {
    mmapReport,
    mmapGetInScanPvt,
    mmapGetOutScanPvt,
    mmapRead,
    mmapWrite
};

int mmapIntAckSetBits16(regDevice *device)
{
    unsigned int offset = (int) device->userdata >> 16;
    unsigned int bits   = (int) device->userdata & 0xFFFF;
    *(epicsInt16*)(device->localbaseaddress+offset) |= bits;
#if defined (__GNUC__) && defined (_ARCH_PPC)
    __asm__ volatile ("eieio;sync");
#endif
    return 0;
}

int mmapIntAckClearBits16(regDevice *device)
{
    unsigned int offset = (int) device->userdata >> 16;
    unsigned int bits   = (int) device->userdata & 0xFFFF;
    *(epicsInt16*)(device->localbaseaddress+offset) &= ~bits;
#if defined (__GNUC__) && defined (_ARCH_PPC)
    __asm__ volatile ("eieio;sync");
#endif
    return 0;
}

void mmapInterrupt(regDevice *device)
{
    device->intrcount++;
    if (device->intrhandler)
    {
        if (device->intrhandler(device) != 0) return;
    }
    scanIoRequest(device->ioscanpvt);
}

/****** startup script configuration function ***********************/

int mmapConfigure(
    const char* name,
    unsigned long baseaddress,
    unsigned long size,
    int addrspace,
    unsigned int intrvector,
    unsigned int intrlevel,
    int (*intrhandler)(regDevice *device),
    void* userdata)
{
    regDevice* device;
    char* localbaseaddress;
    int addressModifier = 0;

    if (name == NULL)
    {
        printf("usage: mmapConfigure(\"name\", baseaddress, size, addrspace)\n");
        printf("maps register block to device \"name\"");
        printf("\"name\" must be a unique string on this IOC\n");
        printf("addrspace = -1: simulation on allocated memory\n");
#ifdef HAVE_MMAP
        printf("addrspace = 0 (default): physical address space using /dev/mem\n");
#endif
#ifdef HAVE_VME
        printf("addrspace = 16, 24 or 32: VME address space\n");
#endif
        return 0;
    }
    switch (addrspace)
    {
        case -1:  /* Simulation runs on allocated memory */
        {
            
            int i;
            localbaseaddress = calloc(1, size);
            if (localbaseaddress == NULL)
            {
                errlogSevPrintf(errlogFatal,
                    "mmapConfigure \"%s\": out of memory\n",
                    name);
                return errno;
            }
            for (i = 0; i < size; i++) localbaseaddress[i] = i;
            break;
        }
#ifdef HAVE_MMAP
        case 0: /* map physical address space with mmap() */
        {
            int fd;
            fd = open("/dev/mem", O_RDWR | O_SYNC);
            if (fd < 0) {
                errlogSevPrintf(errlogFatal,
                    "mmapConfigure \"%s\": can't open /dev/mem: %s\n",
                    name, strerror(errno));
                return errno;
            }
            localbaseaddress = mmap(NULL, size,
                PROT_READ|PROT_WRITE, MAP_SHARED,
                fd, baseaddress);
            close(fd);
            if (localbaseaddress == MAP_FAILED || localbaseaddress == NULL)
            {
                errlogSevPrintf(errlogFatal,
                    "mmapConfigure \"%s\": can't mmap /dev/mem: %s\n",
                    name, strerror(errno));
                return errno;
            }
        }
#endif
#ifdef __vxworks
        case 16:
            addressModifier = VME_AM_USR_SHORT_IO;
            break;
        case 24:
            addressModifier = VME_AM_STD_SUP_DATA;
            break;
        case 32:
            addressModifier = VME_AM_EXT_SUP_DATA;
            break;
#endif
        default:
        {
            errlogSevPrintf(errlogFatal,
                "mmapConfigure \"%s\": illegal VME address space %d must be 16, 24 or 32\n",
                name, addrspace);
            return -1;
        }
    }
#ifdef __vxworks
    if (sysBusToLocalAdrs(addressModifier, (char*)baseaddress, &localbaseaddress) != OK)
    {
        errlogSevPrintf(errlogFatal,
            "mmapConfigure \"%s\": can't map address 0x%08lx on A%d address space\n",
            name, baseaddress, addrspace);
        return -1;
    }
#endif
    if (intrvector > 0)
    {
        if (addressModifier == 0)
        {
            errlogSevPrintf(errlogFatal,
                "mmapConfigure \"%s\": interrupts not supported on addrspace %d\n",
                name, addrspace);
            return -1;
        }
#ifdef HAVE_VME
        if (intrlevel < 1 || intrlevel > 7)
        {
            errlogSevPrintf(errlogFatal, 
                "mmapConfigure \"%s\": illegal interrupt level %d must be 1...7\n",
                name, intrlevel);
            return -1;
        }
#endif
    }
        
    device = (regDevice*)malloc(sizeof(regDevice));
    if (device == NULL)
    {
        errlogSevPrintf(errlogFatal,
            "mmapConfigure \"%s\": out of memory\n",
            name);
        return errno;
    }
    device->magic = MAGIC;
    device->size = size;
    device->addrspace = addrspace;
    device->baseaddress = baseaddress;
    device->localbaseaddress = localbaseaddress;
    device->accessLock = MUTEX_CREATE();
    device->intrlevel = intrlevel;
    device->intrvector = intrvector;
    device->intrhandler = intrhandler;
    device->userdata = userdata;
    device->ioscanpvt = NULL;
    device->intrcount = 0;
    
#ifdef __vxworks
    if (intrvector)
    {
        if (intConnect(INUM_TO_IVEC(intrvector), mmapInterrupt, (int)device) != OK)
        {
            errlogSevPrintf(errlogFatal,
                "vmemapConfigure \"%s\": cannot connect to interrupt vector %d\n",
                name, intrvector);
            return -1;
        }
        if (sysIntEnable(intrlevel) != OK)
        {
            errlogSevPrintf(errlogFatal,
                "vmemapConfigure \"%s\": cannot enable interrupt level %d\n",
                name, intrlevel);
            return -1;
        }
        scanIoInit(&device->ioscanpvt);
    }
#endif
    regDevRegisterDevice(name, &mmapSupport, device);
    return 0;
}

#ifdef EPICS_3_14

#include <iocsh.h>
static const iocshArg mmapConfigureArg0 = { "name", iocshArgString };
static const iocshArg mmapConfigureArg1 = { "baseaddress", iocshArgInt };
static const iocshArg mmapConfigureArg2 = { "size", iocshArgInt };
static const iocshArg mmapConfigureArg3 = { "addrspace (-1=simulation;16,24,32=VME)", iocshArgInt };
static const iocshArg mmapConfigureArg4 = { "intrvector (default:0)", iocshArgInt };
static const iocshArg mmapConfigureArg5 = { "intrlevel (default:0)", iocshArgInt };
static const iocshArg mmapConfigureArg6 = { "intrhandler (default:NULL)", iocshArgString };
static const iocshArg mmapConfigureArg7 = { "userdata (default:NULL)", iocshArgString };
static const iocshArg * const mmapConfigureArgs[] = {
    &mmapConfigureArg0,
    &mmapConfigureArg1,
    &mmapConfigureArg2,
    &mmapConfigureArg3,
    &mmapConfigureArg4,
    &mmapConfigureArg5,
    &mmapConfigureArg6,
    &mmapConfigureArg7
};

static const iocshFuncDef mmapConfigureDef =
    { "mmapConfigure", 8, mmapConfigureArgs };
    
static void mmapConfigureFunc (const iocshArgBuf *args)
{
    int status = mmapConfigure(
        args[0].sval, args[1].ival, args[2].ival, args[3].ival, args[4].ival,
        args[5].ival, NULL, NULL);
    if (status != 0) epicsExit(1);
}

static void mmapRegistrar ()
{
    iocshRegister(&mmapConfigureDef, mmapConfigureFunc);
}

epicsExportRegistrar(mmapRegistrar);

#endif
