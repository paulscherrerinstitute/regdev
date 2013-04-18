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

#define MAGIC 322588966U /* crc("simRegDev") */

static char cvsid_simRegDev[] __attribute__((unused)) =
    "$Id: simRegDev.c,v 1.8 2013/04/18 15:52:45 zimoch Exp $";

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
    int isOutput;
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
    unsigned char buffer[1];
};

int simRegDevDebug = 0;
epicsExportAddress(int, simRegDevDebug);

/******** async processing *****************************/ 

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
    int *pstatus,
    int isOutput)
{
    simRegDevAsyncMessage* msg;

    if (simRegDevDebug >= 1)
        printf ("simRegDevAsynStartTransfer %s: copy %"Z"u bytes * %"Z"u elements\n",
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
    msg->isOutput = isOutput;
    epicsMutexUnlock(device->lock);
    epicsTimerStartDelay(msg->timer, nelem*0.01);
    return ASYNC_COMPLETITION;
}

void simRegDevAsyncCallback(void* arg)
{
    simRegDevAsyncMessage* msg = arg;
    regDevice *device = msg->device;
    CALLBACK* cbStruct = msg->cbStruct;

    if (simRegDevDebug >= 1)
        printf ("simRegDevAsyncCallback %s: copy %"Z"u bytes * %"Z"u elements\n",
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
    if (msg->isOutput)
    {
        /* We got new data: trigger all interested input records */
        if (simRegDevDebug >= 1)
            printf ("simRegDevAsyncCallback %s: trigger input records\n", device->name);
        scanIoRequest(device->ioscanpvt);
    }
    epicsMutexUnlock(device->lock);
    callbackRequest(cbStruct);
}

/******** Support functions *****************************/ 

void simRegDevReport(
    regDevice *device,
    int level)
{
    if (device && device->magic == MAGIC)
    {
        printf("simRegDev driver: %"Z"u bytes, status=%s\n",
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
    size_t offset)
{
    if (!device || device->magic != MAGIC)
    { 
        errlogSevPrintf(errlogMajor,
            "simRegDevGetInScanPvt: illegal device handle\n");
        return NULL;
    }
    return device->ioscanpvt;
}

int simRegDevAsyncRead(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    CALLBACK* cbStruct,
    int prio,
    int* pstatus)
{
    if (!device || device->magic != MAGIC)
    {
        errlogSevPrintf(errlogMajor,
            "simRegDevRead: illegal device handle\n");
        return ERROR;
    }
    if (device->status == 0)
    {
        return ERROR;
    }
    regDevCheckOffset("simRegDevRead", device->name, offset, dlen, nelm, device->size);
    if (simRegDevDebug >= 1)
        printf ("simRegDevRead %s:%"Z"u: %u bytes * %"Z"u elements, prio=%d\n",
        device->name, offset, dlen, nelem, prio);

    if (nelem > 1 && cbStruct != NULL)
        return simRegDevAsynStartTransfer(device, dlen, nelem,
            device->buffer+offset, pdata, NULL, cbStruct, prio, pstatus, FALSE);

    regDevCopy(dlen, nelem, device->buffer+offset, pdata, NULL, device->swap);
    return OK;
}

int simRegDevRead(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    int prio)
{
    return simRegDevAsyncRead(device, offset, dlen, nelem, pdata, NULL, prio, NULL);
}

int simRegDevAsyncWrite(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
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
            "simRegDevWrite: illegal device handle\n");
        return ERROR;
    }
    if (device->status == 0)
    {
        return ERROR;
    }
    regDevCheckOffset("simRegDevWrite", device->name, offset, dlen, nelm, device->size);
    if (simRegDevDebug >= 1)
        printf ("simRegDevWrite %s:%"Z"u: %u bytes * %"Z"u elements, prio=%d\n",
        device->name, offset, dlen, nelem, prio);

    if (nelem > 1 && cbStruct != NULL)
        return simRegDevAsynStartTransfer(device, dlen, nelem, pdata,
            device->buffer+offset, pmask, cbStruct, prio, pstatus, TRUE);

    regDevCopy(dlen, nelem, pdata, device->buffer+offset, pmask, device->swap);
    /* We got new data: trigger all interested input records */
    if (simRegDevDebug >= 1)
        printf ("simRegDevWrite %s: trigger input records\n", device->name);
    scanIoRequest(device->ioscanpvt);
    return OK;
}

int simRegDevWrite(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    void* pmask,
    int prio)
{
    return simRegDevAsyncWrite(device, offset, dlen, nelem, pdata, NULL, pmask, prio, NULL);
}

#define simRegDevGetOutScanPvt NULL

static regDevSupport simRegDevSupport = {
    simRegDevReport,
    simRegDevGetInScanPvt,
    simRegDevGetOutScanPvt,
    simRegDevRead,
    simRegDevWrite
};

#define simRegDevAlloc NULL

static regDevAsyncSupport simRegDevAsyncSupport = {
    simRegDevReport,
    simRegDevGetInScanPvt,
    simRegDevGetOutScanPvt,
    simRegDevAsyncRead,
    simRegDevAsyncWrite,
    simRegDevAlloc
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

int simRegDevConfigure(
    const char* name,
    size_t size,
    int swapEndianFlag,
    int async)
{
    regDevice* device;

    if (name == NULL)
    {
        printf("usage: simRegDevConfigure(\"name\", size, swapEndianFlag)\n");
        printf("maps allocated memory block to device \"name\"");
        printf("\"name\" must be a unique string on this IOC\n");
        return OK;
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
    device->status = 1;
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
        regDevAsyncRegisterDevice(name, &simRegDevAsyncSupport, device);
    }
    else
        regDevRegisterDevice(name, &simRegDevSupport, device);
    scanIoInit(&device->ioscanpvt);
    return OK;
}

int simRegDevAsyncConfigure(
    const char* name,
    size_t size,
    int swapEndianFlag)
{
    return simRegDevConfigure(name, size, swapEndianFlag, 1);
}

int simRegDevSetStatus(
    const char* name,
    int status)
{
    regDevice* device;
    
    if (!name)
    {
        printf ("usage: simRegDevSetStatus name, 0|1\n");
        return ERROR;
    }
    device = regDevFind(name);
    if (!device)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetStatus: %s not found\n",
            name);
        return ERROR;
    }
    if (device->magic != MAGIC)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetStatus: %s is not a simRegDev\n",
            name);
        return ERROR;
    }
    device->status = status;
    if (simRegDevDebug >= 1)
        printf ("simRegDevSetStatus %s: trigger input records\n", device->name);
    scanIoRequest(device->ioscanpvt);
    return OK;
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
        return ERROR;
    }
    device = regDevFind(name);
    if (!device)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetData: %s not found\n",
            name);
        return ERROR;
    }
    if (device->magic != MAGIC)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetData: %s is not a simRegDev\n",
            name);
        return ERROR;
    }
    if (offset < 0 || offset >= device->size)
    {
        errlogSevPrintf(errlogFatal,
            "simRegDevSetData %s: offset %"Z"u out of range\n",
            name, offset);
        return ERROR;
    }
    device->buffer[offset] = value;
    if (simRegDevDebug >= 1)
        printf ("simRegDevSetData %s: trigger input records\n", device->name);
    scanIoRequest(device->ioscanpvt);
    return OK;
}


#ifdef EPICS_3_14

static const iocshArg simRegDevConfigureArg0 = { "name", iocshArgString };
static const iocshArg simRegDevConfigureArg1 = { "size", iocshArgInt };
static const iocshArg simRegDevConfigureArg2 = { "swapEndianFlag", iocshArgInt };
static const iocshArg * const simRegDevConfigureArgs[] = {
    &simRegDevConfigureArg0,
    &simRegDevConfigureArg1,
    &simRegDevConfigureArg2
};

static const iocshFuncDef simRegDevConfigureDef =
    { "simRegDevConfigure", 3, simRegDevConfigureArgs };
    
static void simRegDevConfigureFunc (const iocshArgBuf *args)
{
    int status = simRegDevConfigure(
        args[0].sval, args[1].ival, args[2].ival, 0);
    if (status != 0) epicsExit(1);
}

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
    iocshRegister(&simRegDevAsyncConfigureDef, simRegDevAsyncConfigureFunc);
    iocshRegister(&simRegDevSetStatusDef, simRegDevSetStatusFunc);
    iocshRegister(&simRegDevSetDataDef, simRegDevSetDataFunc);
}

epicsExportRegistrar(simRegDevRegistrar);

#endif
