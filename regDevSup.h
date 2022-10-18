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
#include <epicsVersion.h>
#include <epicsTypes.h>

#define EPICSVER EPICS_VERSION*10000+EPICS_REVISION*100+EPICS_MODIFICATION

#if EPICSVER < 31409 || (EPICSVER < 31500 && __STDC_VERSION__ < 199901L)
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
    char* blockBuffer;                             /* For block mode */
    int blockModes;
    int swap;                                      /* Data swap mode */
    IOSCANPVT blockReceived;
    IOSCANPVT blockSent;
    struct regDevPrivate* triggeredUpdates;        /* For triggered update */
} regDeviceNode;

typedef union {
    epicsInt8 sval8;
    epicsUInt8 uval8;
    epicsInt16 sval16;
    epicsUInt16 uval16;
    epicsInt32 sval32;
    epicsUInt32 uval32;
    epicsInt64 sval64;
    epicsUInt64 uval64;
    epicsFloat32 fval32;
    epicsFloat64 fval64;
    void* buffer;
} regDevAnytype;

typedef struct regDevPrivate {         /* per record data structure */
    epicsUInt32 magic;
    regDeviceNode* device;
    size_t offset;                     /* Offset (in bytes) within device memory */
    size_t rboffset;                   /* Offset to read back output records (or DONT_INIT) */
    struct dbAddr* offsetRecord;       /* Record to read offset from */
    ptrdiff_t offsetScale;             /* Scaling of value from offsetRecord */
    epicsUInt8 bit;                    /* Bit number (0-15) for bi/bo */
    epicsUInt8 dtype;                  /* Data type */
    epicsUInt8 dlen;                   /* Data length (in bytes) */
    epicsUInt8 fifopacking;            /* Fifo: elelents in one register */
    epicsInt32 update;                 /* Periodic update of output records (msec) */
    epicsInt64 L;                      /* Hardware Low limit */
    epicsInt64 H;                      /* Hardware High limit */
    epicsUInt64 invert;                /* Invert bits */
    epicsUInt64 mask;                  /* Mask bits */
    DEVSUPFUN updater;                 /* Update function */
    epicsTimerId updateTimer;          /* Update timer */
    int updating;                      /* Processing type */
    struct regDevPrivate* nextUpdate;  /* For triggered update */
    int status;                        /* For asynchonous drivers */
    size_t asyncOffset;                /* For asynchonous drivers */
    regDevAnytype data;                /* For asynchonous drivers and arrays */
    epicsInt32 irqvec;                 /* Interrupt vector for I/O Intr */
    size_t nelm;                       /* Array size */
    ptrdiff_t interlace;               /* Relative offset of next array element */
} regDevPrivate;

struct devsup {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN io;
};

long regDevInit(int finished);
long regDevGetInIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt);
long regDevGetOutIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt);
regDevPrivate* regDevAllocPriv(dbCommon *record);
int regDevCheckFTVL(dbCommon* record, int ftvl);
int regDevIoParse(dbCommon* record, struct link* link, int types);
int regDevCheckType(dbCommon* record, int ftvl, int nelm);
int regDevAssertType(dbCommon *record, int types);
const char* regDevTypeName(unsigned short dtype);
int regDevMemAlloc(dbCommon* record, void** bptr, size_t size);
int regDevInstallUpdateFunction(dbCommon* record, DEVSUPFUN updater);

/* returns OK, ERROR, or ASYNC_COMPLETION */
/* here buffer must not point to local variable! */
int regDevRead(dbCommon* record, epicsUInt8 dlen, size_t nelem, void* buffer);

int regDevReadNumber(dbCommon* record, epicsInt64* rval, double* fval);
int regDevWriteNumber(dbCommon* record, epicsInt64 rval, double fval);

int regDevReadBits(dbCommon* record, epicsUInt32* rval);
int regDevReadBits64(dbCommon* record, epicsUInt64* rval);
int regDevWriteBits(dbCommon* record, epicsUInt64 rval, epicsUInt64 mask);

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
    status = regDevIoParse((dbCommon*)record, &record->link, types); \
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
    regDevDebugLog(DBG_OUT, "%s: status=%x pact=%d\n", \
        record->name, priv->status, record->pact); \
    if (record->pact) \
    { \
        if (priv->status != S_dev_success) \
        { \
            recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM); \
            regDevDebugLog(DBG_OUT, "%s: asynchronous write error\n", record->name); \
        } \
        regDevDebugLog(DBG_OUT, "%s: status=%x\n", record->name, priv->status); \
        return priv->status; \
    }

#if defined(__GNUC__) && __GNUC__ < 3
 #define regDevPrintErr(f, args...) errlogPrintf("%s %s: " f "\n", _CURRENT_FUNCTION_, record->name , ## args)
#else
 #define regDevPrintErr(f, ...) errlogPrintf("%s %s: " f "\n", _CURRENT_FUNCTION_, record->name , ## __VA_ARGS__)
#endif

#endif
