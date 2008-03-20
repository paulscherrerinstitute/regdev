#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

#include <devLib.h>
#include <regDev.h>

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

#define MAGIC 0xDA757E49

struct regDevice {
    unsigned long magic;
    unsigned long baseaddress;
    unsigned long size;
    char* base;
    int simulation;
    MUTEX accessLock;
};

#include <signal.h>

static int phase = 0;
unsigned int addr = 0;

static void sighandler (int num)
{
    printf ("Signal %d. phase=%d addr=%x\n", num, phase, addr);
    abort();
}

/******** Support functions *****************************/ 

void mmapReport(
    regDevice *device,
    int level)
{
    if (device && device->magic == MAGIC)
    {
        printf("mmapDev baseaddress=0x%08lx size=0x%08lx mapped to %p\n",
            device->baseaddress, device->size, device->base);
    }
}

IOSCANPVT mmapGetInScanPvt(
    regDevice *device,
    unsigned int offset)
{
    return NULL;
}

#define mmapGetOutScanPvt NULL

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
    log = device->simulation || regDevDebug >= 4;
    LOCK(device->accessLock);
    if (log) printf ("mmapRead %08lx:", device->baseaddress+offset);
    phase = 1;
    addr = offset;
    switch (dlen)
    {
        case 0:
            break;
        case 1:
        {
            unsigned char  x;
            unsigned char* s = (unsigned char*)(device->base+offset);
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
            unsigned short* s = (unsigned short*)(device->base+offset);
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
            unsigned long* s = (unsigned long*)(device->base+offset);
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
            unsigned long long* s = (unsigned long long*)(device->base+offset);
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
            unsigned char* s = (unsigned char*)(device->base+offset);
            unsigned char* d = pdata;
            int i;
            
            while (nelem--)
            {
                if (log) printf (" ");
                if (flags && REGDEV_FLAGS_FIFO)
                    s = (unsigned char*)(device->base+offset);
                for (i=0; i<dlen; i++)
                {
                    x = *s++;
                    if (log) printf ("%02x", x);
                    *d++ = x;
                }
            }
        }
    }
    phase = 0;
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
    log = device->simulation || regDevDebug >= 4;
    LOCK(device->accessLock);
    if (log) printf ("mmapWrite %08lx:", device->baseaddress+offset);
    phase = -1;
    addr = offset;
    switch (dlen)
    {
        case 0:
            break;
        case 1:
        {
            unsigned char  x;
            unsigned char* s = pdata;
            unsigned char* d = (unsigned char*)(device->base+offset);
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
            unsigned short* d = (unsigned short*)(device->base+offset);
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
            unsigned long* d = (unsigned long*)(device->base+offset);
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
            unsigned long long* d = (unsigned long long*)(device->base+offset);
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
            unsigned char* d = (unsigned char*)(device->base+offset);
            unsigned char* m;
            int i;
            
            while (nelem--)
            {
                m = pmask;
                if (log) printf (" ");
                if (flags && REGDEV_FLAGS_FIFO)
                    d = (unsigned char*)(device->base+offset);
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
    phase = 0;
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

/****** startup script configuration function ***********************/

int mmapConfigure(
    const char* name,
    unsigned long baseaddress,
    unsigned long size,
    int simulation)
{
    regDevice* device;
    int fd;
    char* base;
    
    if (simulation)
    {
        int i;
        base = calloc(1, size);
        if (base == NULL)
        {
            errlogSevPrintf(errlogFatal,
                "mmapConfigure(\"%s\",0x%08lx,0x%08lx): out of memory\n",
                name, baseaddress, size);
            return errno;
        }
        for (i = 0; i < size; i++) base[i] = i;
    }
    else
    {
        fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (fd < 0) {
            errlogSevPrintf(errlogFatal,
                "mmapConfigure(\"%s\",0x%08lx,0x%08lx): can't open /dev/mem: %s\n",
                name, baseaddress, size, strerror(errno));
            return errno;
        }
        base = mmap(NULL, size,
            PROT_READ|PROT_WRITE, MAP_SHARED,
            fd, baseaddress);
        close(fd);
        if (base == MAP_FAILED || base == NULL)
        {
            errlogSevPrintf(errlogFatal,
                "mmapConfigure(\"%s\",0x%08lx,0x%08lx): can't mmap /dev/mem: %s\n",
                name, baseaddress, size, strerror(errno));
            return errno;
        }
    }
    device = (regDevice*)calloc(1, sizeof(regDevice));
    if (device == NULL)
    {
        errlogSevPrintf(errlogFatal,
            "mmapConfigure(\"%s\",0x%08lx,0x%08lx): out of memory\n",
            name, baseaddress, size);
        return -1;
    }
    device->magic = MAGIC;
    device->size = size;
    device->baseaddress = baseaddress;
    device->base = base;
    device->simulation = simulation;
    device->accessLock = MUTEX_CREATE();
    regDevRegisterDevice(name, &mmapSupport, device);
    signal(SIGSEGV,sighandler);
    return 0;
}

#ifdef EPICS_3_14

#include <iocsh.h>
static const iocshArg mmapConfigureArg0 = { "name", iocshArgString };
static const iocshArg mmapConfigureArg1 = { "baseaddress", iocshArgInt };
static const iocshArg mmapConfigureArg2 = { "size", iocshArgInt };
static const iocshArg mmapConfigureArg3 = { "simulation", iocshArgInt };
static const iocshArg * const mmapConfigureArgs[] = {
    &mmapConfigureArg0,
    &mmapConfigureArg1,
    &mmapConfigureArg2,
    &mmapConfigureArg2
};

static const iocshFuncDef mmapConfigureDef =
    { "mmapConfigure", 4, mmapConfigureArgs };
    
static void mmapConfigureFunc (const iocshArgBuf *args)
{
    int status = mmapConfigure(
        args[0].sval, args[1].ival, args[2].ival, args[3].ival);
    if (status != 0) epicsExit(1);
}

static void mmapRegistrar ()
{
    iocshRegister(&mmapConfigureDef, mmapConfigureFunc);
}

epicsExportRegistrar(mmapRegistrar);

#endif
