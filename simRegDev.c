#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <devLib.h>
#include <regDev.h>

#ifdef EPICS_3_14
#include <epicsExit.h>
#endif

#define MAGIC 322588966U /* crc("simRegDev") */

static char cvsid_simRegDev[] __attribute__((unused)) =
    "$Id: simRegDev.c,v 1.1 2009/12/10 10:02:53 zimoch Exp $";

struct regDevice {
    unsigned long magic;
    const char* name;
    unsigned int size;
    int status;
    IOSCANPVT ioscanpvt;
    unsigned char buffer[1];
};

int simRegDevDebug = 0;

/******** Support functions *****************************/ 

void simRegDevReport(
    regDevice *device,
    int level)
{
    if (device && device->magic == MAGIC)
    {
        printf("simRegDev driver: %d bytes, status=%s\n",
            device->size,
            device->status ? "connected" : "disconnected");
        if (level > 0)
        {
            int i;
            for (i=0; i<device->size; i++)
            {
                if ((i&0xf) == 0)
                {
                    printf ("0x%04x:", i);
                }
                printf (" %02x",
                    device->buffer[i]);
                if ((i&0xf) == 0xf || i == device->size-1)
                {
                    printf ("\n");
                }
            }
        }
    }
}

IOSCANPVT simRegDevGetInScanPvt(
    regDevice *device,
    unsigned int offset)
{
    if (!device || device->magic != MAGIC)
    { 
        errlogSevPrintf(errlogMajor,
            "simRegDevGetInScanPvt: illegal device handle\n");
        return NULL;
    }
    return device->ioscanpvt;
}

int simRegDevRead(
    regDevice *device,
    unsigned int offset,
    unsigned int dlen,
    unsigned int nelem,
    void* pdata,
    int prio)
{
    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevRead: illegal device handle\n");
        return -1;
    }
    if (device->status == 0)
    {
        return -1;
    }
    if (offset > device->size)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevRead %s: offset %d out of range (0-%d)\n",
            device->name, offset, device->size);
        return -1;
    }
    if (offset+dlen*nelem > device->size)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevRead %s: offset %d + %d bytes length exceeds mapped size %d by %d bytes\n",
            device->name, offset, nelem, device->size,
            offset+dlen*nelem - device->size);
        return -1;
    }
    if (simRegDevDebug >= 1) printf ("simRegDevRead %s/%d: %d bytes * %d elements, prio=%d\n",
        device->name, offset, dlen, nelem, prio);
    regDevCopy(dlen, nelem, device->buffer+offset, pdata, NULL, 0);
    return 0;
}

int simRegDevWrite(
    regDevice *device,
    unsigned int offset,
    unsigned int dlen,
    unsigned int nelem,
    void* pdata,
    void* pmask,
    int prio)
{    
    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevWrite: illegal device handle\n");
        return -1;
    }
    if (device->status == 0)
    {
        return -1;
    }
    if (offset > device->size ||
        offset+dlen*nelem > device->size)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevWrite: address out of range\n");
        return -1;
    }
    if (!device || device->magic != MAGIC
        || offset+dlen*nelem > device->size) return -1;
    if (simRegDevDebug >= 1) printf ("simRegDevWrite %s/%d: %d bytes * %d elements, prio=%d\n",
        device->name, offset, dlen, nelem, prio);
    regDevCopy(dlen, nelem, pdata, device->buffer+offset, pmask, 0);
    /* We got new data: trigger all interested input records */
    scanIoRequest(device->ioscanpvt);
    return 0;
}

#define simRegDevGetOutScanPvt NULL

static regDevSupport simRegDevSupport = {
    simRegDevReport,
    simRegDevGetInScanPvt,
    simRegDevGetOutScanPvt,
    simRegDevRead,
    simRegDevWrite
};

/****** startup script configuration function ***********************/

int simRegDevConfigure(
    const char* name,
    unsigned int size)
{
    regDevice* device;

    if (name == NULL)
    {
        printf("usage: simRegDevConfigure(\"name\", size)\n");
        printf("maps allocated memory block to device \"name\"");
        printf("\"name\" must be a unique string on this IOC\n");
        return 0;
    }
    device = (regDevice*)malloc(sizeof(regDevice)+size-1);
    if (device == NULL)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevConfigure %s: out of memory\n",
            name);
        return errno;
    }
    device->magic = MAGIC;
    device->name = name;
    device->size = size;
    device->status = 1;
    scanIoInit(&device->ioscanpvt);
    regDevRegisterDevice(name, &simRegDevSupport, device);
    return 0;
}

int simRegDevSetStatus(
    const char* name,
    int status)
{
    regDevice* device;
    
    device = regDevFind(name);
    if (!device)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetStatus: %s not found\n",
            name);
        return -1;
    }
    if (device->magic != MAGIC)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetStatus: %s is not a simRegDev\n",
            name);
        return -1;
    }
    device->status = status;
    scanIoRequest(device->ioscanpvt);
    return 0;
}

int simRegDevSetData(
    const char* name,
    int offset,
    int value)
{
    regDevice* device;
    
    device = regDevFind(name);
    if (!device)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetData: %s not found\n",
            name);
        return -1;
    }
    if (device->magic != MAGIC)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetData: %s is not a simRegDev\n",
            name);
        return -1;
    }
    if (offset < 0 || offset >= device->size)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetData %s: offset %d out of range\n",
            name, offset);
        return -1;
    }
    device->buffer[offset] = value;
    scanIoRequest(device->ioscanpvt);
    return 0;
}


#ifdef EPICS_3_14

static const iocshArg simRegDevConfigureArg0 = { "name", iocshArgString };
static const iocshArg simRegDevConfigureArg1 = { "size", iocshArgInt };
static const iocshArg * const simRegDevConfigureArgs[] = {
    &simRegDevConfigureArg0,
    &simRegDevConfigureArg1
};

static const iocshFuncDef simRegDevConfigureDef =
    { "simRegDevConfigure", 2, simRegDevConfigureArgs };
    
static void simRegDevConfigureFunc (const iocshArgBuf *args)
{
    int status = simRegDevConfigure(
        args[0].sval, args[1].ival);
    if (status != 0) epicsExit(1);
}

static const iocshArg simRegDevSetStatusArg0 = { "name", iocshArgString };
static const iocshArg simRegDevSetStatusArg1 = { "status", iocshArgInt };
static const iocshArg * const simRegDevSetStatusArgs[] = {
    &simRegDevSetStatusArg0,
    &simRegDevSetStatusArg1
};

static const iocshFuncDef simRegDevSetStatusDef =
    { "simRegDevSetStatus", 2, simRegDevSetStatusArgs };
    
static void simRegDevSetStatusFunc (const iocshArgBuf *args)
{
    simRegDevSetStatus(
        args[0].sval, args[1].ival);
}

static const iocshArg simRegDevSetDataArg0 = { "name",   iocshArgString };
static const iocshArg simRegDevSetDataArg1 = { "offset", iocshArgInt };
static const iocshArg simRegDevSetDataArg2 = { "value",  iocshArgInt };
static const iocshArg * const simRegDevSetDataArgs[] = {
    &simRegDevSetDataArg0,
    &simRegDevSetDataArg1,
    &simRegDevSetDataArg2
};

static const iocshFuncDef simRegDevSetDataDef =
    { "simRegDevSetData", 3, simRegDevSetDataArgs };
    
static void simRegDevSetDataFunc (const iocshArgBuf *args)
{
    simRegDevSetData(
        args[0].sval, args[1].ival, args[2].ival);
}

static void simRegDevRegistrar ()
{
    iocshRegister(&simRegDevConfigureDef, simRegDevConfigureFunc);
    iocshRegister(&simRegDevSetStatusDef, simRegDevSetStatusFunc);
    iocshRegister(&simRegDevSetDataDef, simRegDevSetDataFunc);
}

epicsExportRegistrar(simRegDevRegistrar);

#endif
