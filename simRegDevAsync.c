#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <devLib.h>
#include <regDev.h>
#include <epicsExit.h>
#include <epicsTimer.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsExport.h>

#define MAGIC 3259321301U /* crc("simRegDevAsyncAsync") */

static char cvsid_simRegDevAsync[] __attribute__((unused)) =
    "$Id: simRegDevAsync.c,v 1.2 2013/04/11 14:32:54 zimoch Exp $";

typedef struct simRegDevAsyncMessage {
    struct simRegDevAsyncMessage* next;
    regDevice* device;
    epicsTimerId timer;
    size_t dlen;
    size_t nelem;
    volatile void* src;
    volatile void* dest;
    void* pmask;
    CALLBACK* cbStruct;
    int prio;
    int* pstatus;
} simRegDevAsyncMessage;


struct regDevice {
    unsigned long magic;
    const char* name;
    size_t size;
    int swap;
    int status;
    IOSCANPVT ioscanpvt;
    epicsMutexId lock;
    epicsTimerQueueId queue[NUM_CALLBACK_PRIORITIES];
    simRegDevAsyncMessage* msgFreelist[NUM_CALLBACK_PRIORITIES];
    unsigned char buffer[1]; /* must be last */
};

int simRegDevAsyncDebug = 0;
epicsExportAddress(int, simRegDevAsyncDebug);

void simRegDevAsyncCallback(void* arg);

int simRegDevAsynStartTransfer(
    regDevice *device,
    size_t dlen,
    size_t nelem,
    void* src,
    void* dest,
    void* pmask,
    CALLBACK* cbStruct,
    int prio,
    int *pstatus)
{
    simRegDevAsyncMessage* msg;

    if (simRegDevAsyncDebug >= 1)
        printf ("simRegDevAsynStartTransfer %s: copy %d bytes * %d elements\n",
        device->name, dlen, nelem);

    epicsMutexLock(device->lock);
    if (device->msgFreelist[prio] == NULL)
    {
        msg = calloc(sizeof(simRegDevAsyncMessage),1);
        if (msg)
            msg->timer = epicsTimerQueueCreateTimer(device->queue[prio], simRegDevAsyncCallback, msg);
        if (msg == NULL || msg->timer == NULL)
        {
            errlogSevPrintf(errlogMajor,
                "simRegDevAllocMessage %s: out of memory\n", device->name);
            if (msg) free(msg);
            epicsMutexUnlock(device->lock);
            return ERROR;
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
    msg->cbStruct = cbStruct;
    msg->prio = prio;
    msg->pstatus = pstatus;
    epicsMutexUnlock(device->lock);
    epicsTimerStartDelay(msg->timer, nelem*0.01);
    return ASYNC_COMPLETITION;
}

void simRegDevAsyncCallback(void* arg)
{
    simRegDevAsyncMessage* msg = arg;
    regDevice *device = msg->device;
    CALLBACK* cbStruct = msg->cbStruct;

    if (simRegDevAsyncDebug >= 1)
        printf ("simRegDevAsyncCallback %s: copy %d bytes * %d elements\n",
        device->name, msg->dlen, msg->nelem);
    epicsMutexLock(device->lock);
    if (device->status == 0)
    {
        *msg->pstatus = ERROR;
    }
    else
    {
        regDevCopy(msg->dlen, msg->nelem, msg->src, msg->dest, msg->pmask, device->swap);
        *msg->pstatus = OK;
    }
    msg->next = device->msgFreelist[msg->prio];
    device->msgFreelist[msg->prio] = msg;
    if (msg->pmask)
    {
        /* We got new data: trigger all interested input records */
        scanIoRequest(device->ioscanpvt);
    }
    epicsMutexUnlock(device->lock);
    callbackRequest(cbStruct);
}

/******** Support functions *****************************/ 

void simRegDevAsyncReport(
    regDevice *device,
    int level)
{
    if (device && device->magic == MAGIC)
    {
        printf("simRegDevAsync driver: %d bytes, status=%s\n",
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

IOSCANPVT simRegDevAsyncGetInScanPvt(
    regDevice *device,
    unsigned int offset)
{
    if (!device || device->magic != MAGIC)
    { 
        errlogSevPrintf(errlogMajor,
            "simRegDevAsyncGetInScanPvt: illegal device handle\n");
        return NULL;
    }
    return device->ioscanpvt;
}

int simRegDevAsyncRead(
    regDevice *device,
    size_t offset,
    size_t dlen,
    size_t nelem,
    void* pdata,
    CALLBACK* cbStruct,
    int prio,
    int* pstatus)
{
    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevAsyncRead: illegal device handle\n");
        return ERROR;
    }
    if (device->status == 0)
    {
        return ERROR;
    }
    if (offset > device->size)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevAsyncRead %s: offset %d out of range (0-%d)\n",
            device->name, offset, device->size);
        return ERROR;
    }
    if (offset+dlen*nelem > device->size)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevAsyncRead %s: offset %d + %d bytes length exceeds mapped size %d by %d bytes\n",
            device->name, offset, dlen*nelem, device->size,
            offset+dlen*nelem - device->size);
        return ERROR;
    }
    if (simRegDevAsyncDebug >= 1)
        printf ("simRegDevAsyncRead %s:%d: %d bytes * %d elements, prio=%d\n",
        device->name, offset, dlen, nelem, prio);

    if (nelem > 1)
        return simRegDevAsynStartTransfer(device, dlen, nelem,
            device->buffer+offset, pdata, NULL, cbStruct, prio, pstatus);

    regDevCopy(dlen, nelem, device->buffer+offset, pdata, NULL, device->swap);
    return OK;
}

int simRegDevAsyncWrite(
    regDevice *device,
    size_t offset,
    size_t dlen,
    size_t nelem,
    void* pdata,
    CALLBACK* cbStruct,
    void* pmask,
    int prio,
    int *pstatus)
{
    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevAsyncWrite: illegal device handle\n");
        return ERROR;
    }
    if (device->status == 0)
    {
        return ERROR;
    }
    if (offset > device->size)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevAsyncWrite %s: offset %d out of range (0-%d)\n",
            device->name, offset, device->size);
        return ERROR;
    }
    if (offset+dlen*nelem > device->size)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevAsyncWrite %s: offset %d + %d bytes length exceeds mapped size %d by %d bytes\n",
            device->name, offset, dlen*nelem, device->size,
            offset+dlen*nelem - device->size);
        return ERROR;
    }
    if (simRegDevAsyncDebug >= 1)
        printf ("simRegDevAsyncWrite %s:%d: %d bytes * %d elements, prio=%d\n",
        device->name, offset, dlen, nelem, prio);

    if (nelem > 1)
        return simRegDevAsynStartTransfer(device, dlen, nelem, pdata,
            device->buffer+offset, pmask, cbStruct, prio, pstatus);

    regDevCopy(dlen, nelem, pdata, device->buffer+offset, pmask, device->swap);
    /* We got new data: trigger all interested input records */
    scanIoRequest(device->ioscanpvt);
    return OK;
}

#define simRegDevAsyncGetOutScanPvt NULL
#define simRegDevAsyncAlloc NULL

static regDevAsyncSupport simRegDevAsyncSupport = {
    simRegDevAsyncReport,
    simRegDevAsyncGetInScanPvt,
    simRegDevAsyncGetOutScanPvt,
    simRegDevAsyncRead,
    simRegDevAsyncWrite,
    simRegDevAsyncAlloc
};

/****** startup script configuration function ***********************/

#if defined(__vxworks) && !defined(_WRS_VXWORKS_MAJOR)
/* vxWorks 5 does not have strdup */
static char* strdup(const char* s)
{
    char* r = malloc(strlen(s)+1);
    if (!r) return NULL;
    return strcpy(r, s);
}
#endif

int simRegDevAsyncConfigure(
    const char* name,
    size_t size,
    int swapEndianFlag)
{
    regDevice* device;
    int i;

    if (name == NULL)
    {
        printf("usage: simRegDevAsyncConfigure(\"name\", size, swapEndianFlag)\n");
        printf("maps allocated memory block to device \"name\"");
        printf("\"name\" must be a unique string on this IOC\n");
        return OK;
    }
    device = (regDevice*)malloc(sizeof(regDevice)+size-1);
    if (device == NULL)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevAsyncConfigure %s: out of memory\n",
            name);
        return errno;
    }
    device->magic = MAGIC;
    device->name = strdup(name);
    device->size = size;
    device->status = 1;
    device->swap = swapEndianFlag;
    device->lock = epicsMutexCreate();
    for (i = 0; i < NUM_CALLBACK_PRIORITIES; i++)
    {
        const unsigned int prio [3] =
            {epicsThreadPriorityLow, epicsThreadPriorityMedium, epicsThreadPriorityHigh};
        device->queue[i] = epicsTimerQueueAllocate(FALSE, prio[i]);
    }
    scanIoInit(&device->ioscanpvt);
    regDevAsyncRegisterDevice(name, &simRegDevAsyncSupport, device);
    return OK;
}

int simRegDevAsyncSetStatus(
    const char* name,
    int status)
{
    regDevice* device;
    
    if (!name)
    {
        printf ("usage: simRegDevAsyncSetStatus name, 0|1\n");
        return ERROR;
    }
    device = regDevFind(name);
    if (!device)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevAsyncSetStatus: %s not found\n",
            name);
        return ERROR;
    }
    if (device->magic != MAGIC)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevAsyncSetStatus: %s is not a simRegDevAsync\n",
            name);
        return ERROR;
    }
    device->status = status;
    scanIoRequest(device->ioscanpvt);
    return OK;
}

int simRegDevAsyncSetData(
    const char* name,
    size_t offset,
    int value)
{
    regDevice* device;
    
    if (!name)
    {
        printf ("usage: simRegDevAsyncSetData name, offset, value\n");
        return ERROR;
    }
    device = regDevFind(name);
    if (!device)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevAsyncSetData: %s not found\n",
            name);
        return ERROR;
    }
    if (device->magic != MAGIC)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevAsyncSetData: %s is not a simRegDevAsync\n",
            name);
        return ERROR;
    }
    if (offset < 0 || offset >= device->size)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevAsyncSetData %s: offset %d out of range\n",
            name, offset);
        return ERROR;
    }
    device->buffer[offset] = value;
    scanIoRequest(device->ioscanpvt);
    return OK;
}


#ifdef EPICS_3_14

static const iocshArg simRegDevAsyncConfigureArg0 = { "name", iocshArgString };
static const iocshArg simRegDevAsyncConfigureArg1 = { "size", iocshArgInt };
static const iocshArg simRegDevAsyncConfigureArg2 = { "swapEndianFlag", iocshArgInt };
static const iocshArg * const simRegDevAsyncConfigureArgs[] = {
    &simRegDevAsyncConfigureArg0,
    &simRegDevAsyncConfigureArg1,
    &simRegDevAsyncConfigureArg2
};

static const iocshFuncDef simRegDevAsyncConfigureDef =
    { "simRegDevAsyncConfigure", 3, simRegDevAsyncConfigureArgs };
    
static void simRegDevAsyncConfigureFunc (const iocshArgBuf *args)
{
    int status = simRegDevAsyncConfigure(
        args[0].sval, args[1].ival, args[2].ival);
    if (status != 0) epicsExit(1);
}

static const iocshArg simRegDevAsyncSetStatusArg0 = { "name", iocshArgString };
static const iocshArg simRegDevAsyncSetStatusArg1 = { "status", iocshArgInt };
static const iocshArg * const simRegDevAsyncSetStatusArgs[] = {
    &simRegDevAsyncSetStatusArg0,
    &simRegDevAsyncSetStatusArg1
};

static const iocshFuncDef simRegDevAsyncSetStatusDef =
    { "simRegDevAsyncSetStatus", 2, simRegDevAsyncSetStatusArgs };
    
static void simRegDevAsyncSetStatusFunc (const iocshArgBuf *args)
{
    simRegDevAsyncSetStatus(
        args[0].sval, args[1].ival);
}

static const iocshArg simRegDevAsyncSetDataArg0 = { "name",   iocshArgString };
static const iocshArg simRegDevAsyncSetDataArg1 = { "offset", iocshArgInt };
static const iocshArg simRegDevAsyncSetDataArg2 = { "value",  iocshArgInt };
static const iocshArg * const simRegDevAsyncSetDataArgs[] = {
    &simRegDevAsyncSetDataArg0,
    &simRegDevAsyncSetDataArg1,
    &simRegDevAsyncSetDataArg2
};

static const iocshFuncDef simRegDevAsyncSetDataDef =
    { "simRegDevAsyncSetData", 3, simRegDevAsyncSetDataArgs };
    
static void simRegDevAsyncSetDataFunc (const iocshArgBuf *args)
{
    simRegDevAsyncSetData(
        args[0].sval, args[1].ival, args[2].ival);
}

static void simRegDevAsyncRegistrar ()
{
    iocshRegister(&simRegDevAsyncConfigureDef, simRegDevAsyncConfigureFunc);
    iocshRegister(&simRegDevAsyncSetStatusDef, simRegDevAsyncSetStatusFunc);
    iocshRegister(&simRegDevAsyncSetDataDef, simRegDevAsyncSetDataFunc);
}

epicsExportRegistrar(simRegDevAsyncRegistrar);

#endif
