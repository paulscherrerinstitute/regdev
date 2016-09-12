/* header for device supports */

#ifndef regDevSup_h
#define regDevSup_h

#include <devSup.h>
#include <drvSup.h>
#include <dbCommon.h>
#include <alarm.h>
#include <errlog.h>
#include <recGbl.h>
#include <devLib.h>

#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsTimer.h>

#include <math.h>
#include <sys/types.h>
#include <epicsTypes.h>
#if __STDC_VERSION__ < 199901L
typedef long long epicsInt64;
typedef unsigned long long epicsUInt64;
#endif

#include <epicsExport.h>
#include "regDev.h"

#ifndef S_dev_badArgument
#define S_dev_badArgument (M_devLib| 33)
#endif

#define DONT_INIT ((size_t)-1)

#define TYPE_INT    1
#define TYPE_FLOAT  2
#define TYPE_STRING 4
#define TYPE_BCD    8

#define ARRAY_CONVERT 1
#define DONT_CONVERT 2

typedef struct regDevDispatcher regDevDispatcher;

typedef struct regDeviceNode {                     /* per device data structure */
    epicsUInt32 magic;
    struct regDeviceNode* next;                    /* Next registered device */
    const char* name;                              /* Device name */
    size_t size;                                   /* Device size in bytes */
    const regDevSupport* support;                  /* Device function table */
    regDevice* driver;                             /* Generic device driver */
    epicsMutexId accesslock;                       /* Access semaphore */
    void* (*dmaAlloc) (regDevice*, void*, size_t); /* DMA memory allocator */
    regDevDispatcher* dispatcher;                  /* Serialize requests */
    epicsTimerQueueId updateTimerQueue;            /* For update timers */
    void* blockBuffer;                             /* For block mode */
    int blockSwap;
    IOSCANPVT blockReceived;
} regDeviceNode;

typedef union {
    epicsInt8 sval8;
    epicsUInt8 uval8;
    epicsInt16 sval16;
    epicsUInt16 uval16;
    epicsInt32 sval32;
    epicsUInt32 uval32;
    epicsInt64 val64;
    epicsUInt64 uval64;
    epicsFloat32 fval32;
    epicsFloat64 fval64;
    void* buffer;
} regDevAnytype;

typedef ptrdiff_t regDevSignedOffset_t; /* WIN has no ssize_t */

typedef struct regDevPrivate{          /* per record data structure */
    epicsUInt32 magic;
    regDeviceNode* device;
    size_t offset;                     /* Offset (in bytes) within device memory */
    size_t initoffset;                 /* Offset to initialize output records (or DONT_INIT) */
    struct dbAddr* offsetRecord;       /* record to read offset from */
    regDevSignedOffset_t offsetScale;  /* scaling of value from offsetRecord */
    epicsUInt8 bit;                    /* Bit number (0-15) for bi/bo */
    epicsUInt8 dtype;                  /* Data type */
    epicsUInt8 dlen;                   /* Data length (in bytes) */
    epicsUInt8 arraypacking;           /* Array: elelents in one register */
    epicsUInt8 fifopacking;            /* Fifo: elelents in one register */
    epicsInt32 L;                      /* Hardware Low limit */
    epicsInt32 H;                      /* Hardware High limit */
    epicsUInt32 invert;                /* Invert bits for bi,bo,mbbi,... */
    epicsUInt32 update;                /* Periodic update of output records (msec) */
    DEVSUPFUN updater;                 /* Update function */
    epicsTimerId updateTimer;          /* Update timer */
    enum {init, normal, update} state; /* Processing type */
    int status;                        /* For asynchonous drivers */
    size_t asyncOffset;                /* For asynchonous drivers */
    regDevAnytype data;                /* For asynchonous drivers and arrays */
    regDevAnytype mask;                /* For asynchonous drivers */
    epicsInt32 irqvec;                 /* Interrupt vector for I/O Intr */
} regDevPrivate;

struct devsup {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN io;
};

long regDevGetInIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt);
long regDevGetOutIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt);
regDevPrivate* regDevAllocPriv(dbCommon *record);
int regDevCheckFTVL(dbCommon* record, int ftvl);
int regDevIoParse(dbCommon* record, struct link* link);
int regDevCheckType(dbCommon* record, int ftvl, int nelm);
int regDevAssertType(dbCommon *record, int types);
const char* regDevTypeName(unsigned short dtype);
int regDevMemAlloc(dbCommon* record, void** bptr, size_t size);
int regDevInstallUpdateFunction(dbCommon* record, DEVSUPFUN updater);
int regDevGetOffset(dbCommon* record, int read, epicsUInt8 dlen, size_t nelem, size_t *poffset);

/* returns OK, ERROR, or ASYNC_COMPLETION */
/* here buffer must not point to local variable! */
int regDevRead(dbCommon* record, epicsUInt8 dlen, size_t nelem, void* buffer);
int regDevWrite(dbCommon* record, epicsUInt8 dlen, size_t nelem, void* buffer, void* mask);

int regDevReadNumber(dbCommon* record, epicsInt32* rval, double* fval);
int regDevWriteNumber(dbCommon* record, epicsInt32 rval, double fval);

int regDevReadBits(dbCommon* record, epicsUInt32* rval);
int regDevWriteBits(dbCommon* record, epicsUInt32 rval, epicsUInt32 mask);

/* returns OK or ERROR, or ASYNC_COMPLETION */
int regDevReadArray(dbCommon* record, size_t nelm);
int regDevWriteArray(dbCommon* record, size_t nelm);

int regDevScaleFromRaw(dbCommon* record, int ftvl, void* val, size_t nelm, double low, double high);
int regDevScaleToRaw(dbCommon* record, int ftvl, void* rval, size_t nelm, double low, double high);

#define regDevCommonInit(record, link, types) \
    int status; \
    regDevPrivate* priv; \
    priv = regDevAllocPriv((dbCommon*)record); \
    if (!priv) return S_dev_noMemory; \
    status = regDevIoParse((dbCommon*)record, &record->link); \
    if (status) return status; \
    status = regDevAssertType((dbCommon*)record, types); \
    if (status) return status

#define regDevCheckAsyncWriteResult(record) \
    regDevPrivate* priv = (regDevPrivate*)(record->dpvt); \
    if (priv == NULL) \
    { \
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM); \
        regDevDebugLog(DBG_OUT, "record %s not initialized\n", record->name); \
        return S_dev_badInit;\
    } \
    regDevDebugLog(DBG_OUT, "%s: status=%x pact=%d states=%s\n", \
        record->name, priv->status, record->pact, ((char*[]){"init","normal","update"})[priv->state]); \
    if (record->pact) \
    { \
        if (priv->status != S_dev_success) \
        { \
            if (priv->state == update) \
            { \
                recGblSetSevr(record, READ_ALARM, INVALID_ALARM); \
                regDevDebugLog(DBG_IN, "%s: asynchronous update error\n", record->name); \
            } \
            else \
            { \
                recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM); \
                regDevDebugLog(DBG_OUT, "%s: asynchronous write error\n", record->name); \
            } \
        } \
        regDevDebugLog(DBG_OUT, "%s: status=%x\n", record->name, priv->status); \
        return priv->status; \
    } \
    if (priv->state == update) \
    { \
        regDevDebugLog(DBG_IN, "%s: running updater\n", record->name); \
        recGblResetAlarms(record); \
        return priv->updater(record); \
    } \

#if defined(__GNUC__) && __GNUC__ < 3
 #define regDevPrintErr(f, args...) errlogPrintf("%s %s: " f "\n", _CURRENT_FUNCTION_, record->name , ## args)
#else
 #define regDevPrintErr(f, ...) errlogPrintf("%s %s: " f "\n", _CURRENT_FUNCTION_, record->name , ## __VA_ARGS__)
#endif

#endif
