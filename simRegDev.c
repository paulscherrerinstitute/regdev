#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <devLib.h>
#include <regDev.h>
#include <errlog.h>
#include <callback.h>
#include <epicsExit.h>
#include <epicsTimer.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <epicsStdioRedirect.h>
#include "memDisplay.h"
#include "simRegDev.h"

#define MAGIC 322588966U /* crc("simRegDev") */

#ifndef __GNUC__
#define  __attribute__(x)  /*NOTHING*/
#endif

#ifndef S_dev_badArgument
#define S_dev_badArgument (M_devLib| 33)
#endif

typedef struct simRegDevMessage {
    struct simRegDevMessage* next;
    regDevice* device;
    epicsTimerId timer;
    unsigned int dlen;
    size_t nelem;
    volatile void* src;
    volatile void* dest;
    void* pmask;
    int prio;
    regDevTransferComplete callback;
    const char* user;
    int isOutput;
} simRegDevMessage;

struct regDevice {
    unsigned long magic;
    const char* name;
    size_t size;
    int swap;
    int connected;
    int blockDevice;
    IOSCANPVT ioscanpvt;
    epicsMutexId lock;
    epicsTimerQueueId queue[NUM_CALLBACK_PRIORITIES];
    simRegDevMessage* msgFreelist[NUM_CALLBACK_PRIORITIES];
    unsigned char buffer[1];
};

int simRegDevDebug = 0;
epicsExportAddress(int, simRegDevDebug);

/******** async processing *****************************/

void simRegDevCallback(void* arg);

int simRegDevAsynTransfer(
    regDevice *device,
    unsigned int dlen,
    size_t nelem,
    void* src,
    void* dest,
    void* pmask,
    int prio,
    regDevTransferComplete callback,
    const char* user,
    int isOutput)
{
    simRegDevMessage* msg;

    if (simRegDevDebug & (isOutput ? DBG_OUT : DBG_IN))
        printf ("simRegDevAsynTransfer %s %s: copy %s %u bytes * 0x%" Z "x elements\n",
        user, device->name, isOutput ? "out" : "in", dlen, nelem);

    epicsMutexLock(device->lock);
    if (device->msgFreelist[prio] == NULL)
    {
        msg = calloc(sizeof(simRegDevMessage),1);
        if (msg)
            msg->timer = epicsTimerQueueCreateTimer(device->queue[prio], simRegDevCallback, msg);
        if (msg == NULL || msg->timer == NULL)
        {
            errlogSevPrintf(errlogMajor,
                "simRegDevAllocMessage %s %s: out of memory\n", user, device->name);
            if (msg) free(msg);
            epicsMutexUnlock(device->lock);
            return S_dev_noMemory;
        }
    }
    else
    {
        msg = device->msgFreelist[prio];
        device->msgFreelist[prio] = device->msgFreelist[prio]->next;
    }
    msg->next = NULL;
    msg->device = device;
    msg->dlen = dlen;
    msg->nelem = nelem;
    msg->src = src;
    msg->dest = dest;
    msg->pmask = pmask;
    msg->prio = prio;
    msg->callback = callback;
    msg->user = user;
    msg->isOutput = isOutput;
    epicsMutexUnlock(device->lock);
    if (simRegDevDebug & (isOutput ? DBG_OUT : DBG_IN))
        printf ("simRegDevAsynTransfer %s %s: starting timer %g seconds\n",
        user, device->name, nelem*0.01);
    epicsTimerStartDelay(msg->timer, nelem*0.01);
    return ASYNC_COMPLETION;
}

void simRegDevCallback(void* arg)
{
    simRegDevMessage* msg = arg;
    regDevice *device = msg->device;
    regDevTransferComplete callback = msg->callback;
    int status;

    if (simRegDevDebug & (msg->isOutput ? DBG_OUT : DBG_IN))
        printf ("simRegDevCallback %s %s: copy %u bytes * 0x%" Z "x elements\n",
        msg->user, device->name, msg->dlen, msg->nelem);
    epicsMutexLock(device->lock);
    if (device->connected == 0)
    {
        status = S_dev_noDevice;
    }
    else
    {
        regDevCopy(msg->dlen, msg->nelem, msg->src, msg->dest, msg->pmask, device->swap);
        status = S_dev_success;
    }
    msg->next = device->msgFreelist[msg->prio];
    device->msgFreelist[msg->prio] = msg;
    if (msg->isOutput)
    {
        /* We got new data: trigger all interested input records */
        if (simRegDevDebug & DBG_OUT)
            printf ("simRegDevCallback %s %s: trigger input records\n", msg->user, device->name);
        scanIoRequest(device->ioscanpvt);
    }
    epicsMutexUnlock(device->lock);
    if (simRegDevDebug & (msg->isOutput ? DBG_OUT : DBG_IN))
        printf ("simRegDevCallback %s %s: call back status=%d\n", msg->user, device->name, status);
    callback(msg->user, status);
}

/******** Support functions *****************************/

void simRegDevReport(
    regDevice *device,
    int level)
{
    if (device && device->magic == MAGIC)
    {
        printf("simRegDev driver: %" Z "u bytes, %ssync%s, status=%s\n",
            device->size,
            device->lock ? "a" : "",
            device->blockDevice ? " block" : "",
            device->connected ? "connected" : "disconnected");
        if (level > 0)
            memDisplay(0, device->buffer, 1, device->size);
    }
}

IOSCANPVT simRegDevGetInScanPvt(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    int ivec,
    const char* name)
{
    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevGetInScanPvt %s: illegal device handle\n", name);
        return NULL;
    }
    return device->ioscanpvt;
}

int simRegDevRead(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    int prio,
    regDevTransferComplete callback,
    const char* user)
{
    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevRead %s: illegal device handle\n", user);
        return S_dev_wrongDevice;
    }
    if (device->connected == 0)
    {
        return S_dev_noDevice;
    }
    if (simRegDevDebug & DBG_OUT)
    {
        printf ("simRegDevRead %s: pdata=%p, buffer=[%p ... %p]\n",
            user, pdata, device->buffer, device->buffer+device->size);
    }
    if (pdata == device->buffer+offset)
    {
        if (simRegDevDebug & DBG_IN)
            printf ("simRegDevRead %s %s:0x%" Z "x: %u bytes * 0x%" Z "x elements, direct map, no copy\n",
                user, device->name, offset, dlen, nelem);
        return S_dev_success;
    }
    if (simRegDevDebug & DBG_IN)
        printf ("simRegDevRead %s %s:0x%" Z "x: %u bytes * 0x%" Z "x elements, prio=%d\n",
            user, device->name, offset, dlen, nelem, prio);
    if (callback && nelem > 1 && device->lock)
        return simRegDevAsynTransfer(device, dlen, nelem,
            device->buffer+offset, pdata, NULL, prio, callback, user, FALSE);

    if (simRegDevDebug & DBG_IN)
        printf ("simRegDevRead %s %s:0x%" Z "x: copy values\n",
        user, device->name, offset);
    regDevCopy(dlen, nelem, device->buffer+offset, pdata, NULL, device->swap);
    return S_dev_success;
}

int simRegDevWrite(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    void* pmask,
    int prio,
    regDevTransferComplete callback,
    const char* user)
{
    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevWrite: illegal device handle\n");
        return S_dev_wrongDevice;
    }
    if (device->connected == 0)
    {
        return S_dev_noDevice;
    }
    if (pdata == device->buffer+offset)
    {
        if (simRegDevDebug & DBG_OUT)
            printf ("simRegDevWrite %s %s:0x%" Z "x: direct map, no copy\n",
                user, device->name, offset);
        return S_dev_success;
    }
    if (simRegDevDebug & DBG_OUT)
    {
        size_t n;
        printf ("simRegDevWrite %s %s:0x%" Z "x: %u bytes * 0x%" Z "x elements, prio=%d\n",
        user, device->name, offset, dlen, nelem, prio);
        for (n=0; n<nelem && n<10; n++)
        {
            switch (dlen)
            {
                case 1:
                    printf (" %02x",((epicsUInt8*)pdata)[n*dlen]);
                    break;
                case 2:
                    printf (" %04x",((epicsUInt16*)pdata)[n*dlen]);
                    break;
                case 4:
                    printf (" %08x",((epicsUInt32*)pdata)[n*dlen]);
                    break;
                case 8:
                    printf (" %08llx",((unsigned long long*)pdata)[n*dlen]);
                    break;
            }
        }
        printf ("\n");
    }

    if (callback && nelem > 1 && device->lock)
        return simRegDevAsynTransfer(device, dlen, nelem, pdata,
            device->buffer+offset, pmask, prio, callback, user, TRUE);

    if (simRegDevDebug & DBG_OUT)
        printf ("simRegDevWrite %s %s:0x%" Z "x: copy values\n",
        user, device->name, offset);
    regDevCopy(dlen, nelem, pdata, device->buffer+offset, pmask, device->swap);
    /* We got new data: trigger all interested input records */
    if (simRegDevDebug & DBG_OUT)
        printf ("simRegDevWrite %s: trigger input records\n", device->name);
    scanIoRequest(device->ioscanpvt);
    return S_dev_success;
}

static regDevSupport simRegDevSupport = {
    simRegDevReport,
    simRegDevGetInScanPvt,
    NULL,
    simRegDevRead,
    simRegDevWrite,
};

/****** startup script configuration function ***********************/

int simRegDevConfigure(
    const char* name,
    size_t size,
    int swapEndianFlag,
    int async,
    int blockDevice)
{
    regDevice* device;

    if (name == NULL)
    {
        printf("usage: simRegDevConfigure(\"name\", size, swapEndianFlag)\n");
        printf("maps allocated memory block to device \"name\"");
        printf("\"name\" must be a unique string on this IOC\n");
        return S_dev_success;
    }
    device = (regDevice*)calloc(sizeof(regDevice)+size-1,1);
    if (device == NULL)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevConfigure %s: out of memory\n",
            name);
        return errno;
    }
    device->magic = MAGIC;
    device->name = strdup(name);
    device->size = size;
    device->connected = 1;
    device->swap = swapEndianFlag;
    if (async)
    {
        int i;
        device->lock = epicsMutexCreate();
        for (i = 0; i < NUM_CALLBACK_PRIORITIES; i++)
        {
            const unsigned int prio [3] =
                {epicsThreadPriorityLow, epicsThreadPriorityMedium, epicsThreadPriorityHigh};
            device->queue[i] = epicsTimerQueueAllocate(FALSE, prio[i]);
        }
    }
    regDevRegisterDevice(name, &simRegDevSupport, device, size);
    device->blockDevice = blockDevice;
    if (blockDevice)
        regDevMakeBlockdevice(device, REGDEV_BLOCK_READ | REGDEV_BLOCK_WRITE, REGDEV_NO_SWAP, device->buffer);
    scanIoInit(&device->ioscanpvt);
    return S_dev_success;
}

int simRegDevAsyncConfigure(
    const char* name,
    size_t size,
    int swapEndianFlag,
    int blockFlag)
{
    return simRegDevConfigure(name, size, swapEndianFlag, 1, blockFlag);
}

int simRegDevSetStatus(
    const char* name,
    int connected)
{
    regDevice* device;

    if (!name)
    {
        printf ("usage: simRegDevSetStatus name, 0|1\n");
        return S_dev_badArgument;
    }
    device = regDevFind(name);
    if (!device)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetStatus: %s not found\n",
            name);
        return S_dev_noDevice;
    }
    if (device->magic != MAGIC)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetStatus: %s is not a simRegDev\n",
            name);
        return S_dev_wrongDevice;
    }
    device->connected = connected;
    if (simRegDevDebug >= 1)
        printf ("simRegDevSetStatus %s: trigger input records\n", device->name);
    scanIoRequest(device->ioscanpvt);
    return S_dev_success;
}

int simRegDevSetData(
    const char* name,
    size_t offset,
    int value)
{
    regDevice* device;

    if (!name)
    {
        printf ("usage: simRegDevSetData name, offset, value\n");
        return S_dev_badArgument;
    }
    device = regDevFind(name);
    if (!device)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetData: %s not found\n",
            name);
        return S_dev_noDevice;
    }
    if (device->magic != MAGIC)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetData: %s is not a simRegDev\n",
            name);
        return S_dev_wrongDevice;
    }
    if (offset > device->size)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetData: %s offset %" Z "d out of range (0-%" Z "d)\n",
            name, offset, device->size);
        return S_dev_badSignalNumber;
    }

    device->buffer[offset] = value;
    if (simRegDevDebug >= 1)
        printf ("simRegDevSetData %s: trigger input records\n", device->name);
    scanIoRequest(device->ioscanpvt);
    return S_dev_success;
}

int simRegDevGetData(
    const char* name,
    size_t offset,
    int *value)
{
    regDevice* device;

    if (!name)
    {
        printf ("usage: simRegDevGetData name, offset, &value\n");
        return S_dev_badArgument;
    }
    device = regDevFind(name);
    if (!device)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevGetData: %s not found\n",
            name);
        return S_dev_noDevice;
    }
    if (device->magic != MAGIC)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevGetData: %s is not a simRegDev\n",
            name);
        return S_dev_wrongDevice;
    }
    if (offset > device->size)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevGetData: %s offset %" Z "d out of range (0-%" Z "d)\n",
            name, offset, device->size);
        return S_dev_badSignalNumber;
    }

    *value = device->buffer[offset];
    return S_dev_success;
}


#ifndef EPICS_3_13

static const iocshArg simRegDevConfigureArg0 = { "name", iocshArgString };
static const iocshArg simRegDevConfigureArg1 = { "size", iocshArgInt };
static const iocshArg simRegDevConfigureArg2 = { "swapEndianFlag", iocshArgInt };
static const iocshArg simRegDevConfigureArg3 = { "blockDevice", iocshArgInt };
static const iocshArg * const simRegDevConfigureArgs[] = {
    &simRegDevConfigureArg0,
    &simRegDevConfigureArg1,
    &simRegDevConfigureArg2,
    &simRegDevConfigureArg3
};

static const iocshFuncDef simRegDevConfigureDef =
    { "simRegDevConfigure", 4, simRegDevConfigureArgs };

static void simRegDevConfigureFunc (const iocshArgBuf *args)
{
    int status = simRegDevConfigure(
        args[0].sval, args[1].ival, args[2].ival, 0, args[3].ival);
    if (status != 0) epicsExit(1);
}

static const iocshFuncDef simRegDevAsyncConfigureDef =
    { "simRegDevAsyncConfigure", 4, simRegDevConfigureArgs };

static void simRegDevAsyncConfigureFunc (const iocshArgBuf *args)
{
    int status = simRegDevAsyncConfigure(
        args[0].sval, args[1].ival, args[2].ival, args[3].ival);
    if (status != 0) epicsExit(1);
}

static const iocshArg simRegDevSetStatusArg0 = { "name", iocshArgString };
static const iocshArg simRegDevSetStatusArg1 = { "connected", iocshArgInt };
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
    iocshRegister(&simRegDevAsyncConfigureDef, simRegDevAsyncConfigureFunc);
    iocshRegister(&simRegDevSetStatusDef, simRegDevSetStatusFunc);
    iocshRegister(&simRegDevSetDataDef, simRegDevSetDataFunc);
}

epicsExportRegistrar(simRegDevRegistrar);

#endif
